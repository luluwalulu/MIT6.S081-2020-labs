// 该文件截取部分lec17.7有关用户虚拟内存的一些重要函数
// 当然，只是伪代码

void flip(){
  swap(To,From);
  collecting=1; // 表明GC正在运行
  scanned = to;

  #ifdef VM // 如果用户虚拟内存机制打开，那么将To空间设置为不可访问
    mprotect(to,SPACESZ,PROT_NONE);
  #endif

  root_head=(struct elem *) forward((struct obj*)root_head);
  root_last=(struct elem *) forward((struct obj*)root_last);
}

struct obj* forward(struct obj* o){
  struct obj *n=o;
  if(in_from(o)){ // 如果仍在from中，则拷贝到To中
    n=copy(o);
  }else{ // 说明已经拷贝到了To中，用To空间的指针代替对象指针
    n=o->newaddr;
  }
  return n;
}

struct elem* readptr(struct elem **p){
// 如果没有使用虚拟内存，就对指针进行forward操作，并将指针指向的虚拟地址修改
#ifndef VM 
  struct obj *o=forward((struct obj*)(*p));
  *p=(struct elem *) o;
#endif
  return *p;
}

// 初始化内存空间和信号处理器
static void setup_spaces(void){
  struct sigaction act;
  // 注册 SIGSEGV 处理器
  // ...

  // 使用shm_open和mmap创建共享内存区域
  int shm=shm_open("baker",O_CREAT|O_RDWR|O_TRUNC,S_IRWXU); //申请一块共享内存对象
  shm_unlink(...) // 立即删除这个文件的名字，确保程序退出后内存自动释放
  ftruncate(shm,TOFROM); // 设定内存大小

  // 两次调用mmap将同一块物理内存映射到两个不同虚拟地址
  // 对mutator，这块内存权限会发生变化
  mutator=mmap(...);
  // 对collector，内存权限为可读可写
  collector=mmap(...);

  // 设定from和to的初始位置
  from=mutator;
  to=(char*)mutator+SPACESZ;
}

static void handle_sigsegv(...){
  // 计算故障地址属于哪一页的起始位置
  uintptr_t fault_addr=(uintptr_t)si->si_addr;
  double *page_base=(double*) align_down(fault_addr,PGSIZE);

  // 此时可能有后台的GC进程或其他应用线程正在分配内存，需要防止竞争
  pthread_mutex_lock(&lock);
  // scan会遍历这一页内存中的所有对象，检查对象里的每个指针，如果指针指向From，就搬运到To，并更新指针
  scan(page_base);
  pthread_mutex_unlock(&lock);

  // 修改权限
  mprotect(page_base,PGSIZE,PROT_READ|PROT_WRITE);
}