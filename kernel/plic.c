#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  // PLIC是PLIC硬件在内存中的基地址，UART0_IRQ是设备的中断号
  // 下面的代码就是在修改PLIC优先级寄存器的值
  // 优先级为0表示禁用，优先级大于等于1，PLIC就会认为这个中断有效

  // 这个函数的操作只是设定了PLIC对于中断处理的优先级，PLIC最终还是要把中断转发给CPU的，所以我们还需要设定CPU的中断处理优先级
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set uart's enable bit for this hart's S-mode. 
  *(uint32*)PLIC_SENABLE(hart)= (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  // 设定核心收到的中断的阈值，其优先级必须大于等于这个阈值
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();

  // 读取PLIC中的SCLAIM寄存器
  // 读取过程中PLIC硬件会自动返回当前优先级最高且等待处理的中断号
  // 自动清除该中断源的等待状态，意味着这个中断已经被CPU接管
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  // 把中断号写回SCLAIM寄存器中
  // 当把中断号写回时PLIC硬件会认为OS已经完成了对该中断的服务，硬件可以重新评估是否有新的中断需要发给这个CPU
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
