#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"


/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

extern struct proc proc[NPROC]; // proc.h

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  // 为最高一级page directory分配物理page
  kernel_pagetable = (pagetable_t) kalloc();
  // 将这段内存初始化为0
  memset(kernel_pagetable, 0, PGSIZE);

  // etext是一个链接器符号，当你编译内核时，链接器(kernel.ld)会把所有机器指令拼在一起，最后在指令结束的地方打个标记即为etext
  // (uint64)etext-KERNBASE刚好就是内核代码的总长度

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // 设置SATP寄存器，下一条指令被执行时，地址翻译就会开始生效，而在这条指令之前，我们是直接使用物理地址
  w_satp(MAKE_SATP(kernel_pagetable));
  // 一旦加载page table到SATP寄存器上，我们的世界观就会完全改变。如果page table设置错误，将会有各种奇怪的bug和错误发生

  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// walk是内核在软件层面模拟硬件MMU的函数
// 他从根页表开始一级一级往下找，如果路径通畅，它返回虚拟地址对应的最低级PTE的指针
// 如果中间某一级页表不存在，且参数 alloc 被设置，它会自动申请新的物理页来创建缺失的页表，并建立链接
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    // PX从虚拟地址va中提取当前层级的9位索引值
    pte_t *pte = &pagetable[PX(level, va)];
    // PTE_V检查Valid位，*pte&PTE_V用来检查PTE是否有效
    if(*pte & PTE_V) {
      // 取出PTE中的PA，作为下一级页表的物理地址
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 如果alloc没有置位或者不允许分配内存的话
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      // 初始化新的物理页
      memset(pagetable, 0, PGSIZE);
      // 将PTE指向新分配的物理页并将其有效位置位
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  // 此时找到最后一级页表
  // 然后返回最后一级页表中对应PTE的地址
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// va必须是页对齐的虚拟地址
// walkaddr只负责查找，同时还带有安全检查，它会找到va映射的物理页的起始地址
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  // 注意PTE2PA宏不会将va中的offset带入计算，PTE2PA得到的是物理页的起始地址
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pagetable_t kpt = kernel_pagetable;
  struct proc *p = myproc();
  if(p != 0) {
    kpt = p->kpagetable; // 切换查询目标
  }

  pte = walk(kpt, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// 它在指定的 pagetable 中，为一段连续的虚拟地址范围 [va, va + size) 建立到一段连续物理地址范围 [pa, pa + size) 的映射，并设置权限 perm
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// 在页表pagetable定义的地址空间中，从虚拟地址va开始，将npages个页表项置0（或者说删除npages个映射关系），如果需要的话释放对应的物理内存
// 作用于pagetable的底层
// 起始的va必须页对齐
// do_free如果为1，同时释放对应的物理内存页，如果为0，只删除页表中的映射关系
// 该函数要求页表管理的虚拟地址空间必须是连续的空间，va到va + npages*PGSIZE的所有虚拟地址空间的映射关系都必须被切断
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    // 结束映射并将pte标记为了无效
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
// 创建一张新的，空的用户页表
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 用于增加用户进程的内存空间
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 减少用户空间
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
// 先将页表项置零（如果需要的话释放对应的物理内存），然后将页表本身占用的内存释放
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // 申请内存
    if((mem = kalloc()) == 0)
      goto err;
    // 粘贴内容
    memmove(mem, (char*)pa, PGSIZE);
    // 建立映射
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
// 将指定虚拟地址对应的页表项标记为用户不可访问
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// 将内容从内核拷贝到用户进程中
// 需要先通过dstva找到对应的物理地址，再把数据从内核拷贝到物理地址中
// 所有虚拟地址都需要先转换为物理地址才能进行操作
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    // 先把va0页对齐
    va0 = PGROUNDDOWN(dstva);
    // pa0是对应物理页的地址
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    // dstva-va0就是偏移量，n就是这张页表能够当作拷贝目标的量
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    // 不要忘了移动dstva
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

void vmprinter(pagetable_t pagetable,int dep){
  if(dep==3) return;
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(!pte&PTE_V) continue;
    uint64 child = PTE2PA(pte);

    for(int i=0;i<dep;i++)
      printf(".. ");
    printf("..%d: pte %p pa %p\n",i,pte,child);

    vmprinter((pagetable_t)child,dep+1);
  }
}

void vmprint(pagetable_t pagetable){
  printf("page table %p\n",pagetable);
  vmprinter(pagetable,0);
}


void
proc_kvmmap(struct proc *p,uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(p->kpagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

void
proc_kvminit(struct proc* p)
{
  // 为最高一级page directory分配物理page
  uint64* pa=(pagetable_t) kalloc();
  if(pa==0){
    panic("proc_kvminit1\n");
  } 
  p->kpagetable = pa;
  // 将这段内存初始化为0
  memset(p->kpagetable, 0, PGSIZE);

  // etext是一个链接器符号，当你编译内核时，链接器(kernel.ld)会把所有机器指令拼在一起，最后在指令结束的地方打个标记即为etext
  // (uint64)etext-KERNBASE刚好就是内核代码的总长度

  // uart registers
  proc_kvmmap(p,UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  proc_kvmmap(p,VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  proc_kvmmap(p,CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  proc_kvmmap(p,PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  proc_kvmmap(p,KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  proc_kvmmap(p,(uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  proc_kvmmap(p,TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  pagetable_t pa2=kalloc();
  uint64 va = KSTACK((int) (p - proc)); 
  proc_kvmmap(p, va, (uint64)pa2, PGSIZE, PTE_R | PTE_W);
  p->kstack = va; // 记录虚拟地址
}

void
proc_freewalk(struct proc* p)
{
  // 首先，需要遍历页表，将所有专门分配给用户进程的内存彻底释放，然后删除所有映射关系
  kfree((void*)p->kstack);
  freeUserPage(p->kpagetable);
  freewalk(p->kpagetable);
}

void freeUserPage(pagetable_t pagetable){
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // child就是下一级pagetable的物理地址
      uint64 child = PTE2PA(pte);
      freeUserPage((pagetable_t)child);
    }
    // 如果有效，且属于用户，并且是最后一级，那就释放它
    else if((pte & (PTE_V | PTE_U))){
      uint64 pa = PTE2PA(pte);
      kfree((void*)pa);
    }
  }
}