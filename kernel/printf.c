//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&pr.lock);
}

void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  backtrace();
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

void backtrace(){
  printf("backtrace:\n");

  // 当前帧的fp
  uint64 fp=r_fp();
  uint64 stackTop=PGROUNDUP(fp);
  uint64 stackButtom=PGROUNDDOWN(fp);
  uint64 ra;

  // 我们在每一轮循环中，fp都指向当前栈帧，然后我们读取当前栈帧中ra的值并打印
  // 接着把fp指向上一个栈帧
  // 也就是在打印时，fp指向上一个栈帧，ra指向当前栈帧中的ra信息
  while(fp<stackTop&&fp>stackButtom){
    ra=fp-8;
    printf("%p\n",*(uint64*)ra);

    // fp本身是偏移量，fp-=8仍是偏移量，fp-=16这个地址的数据存储的才是我们想要的前一帧的偏移量
    fp=*(uint64*)(fp-16);
  }
}
// 我们发现，当fp==stackTop，即fp指向最上面的栈帧时，其中存储的ra是无效值
// 这是因为当我们通过uservec调用usertrap时，我们通过汇编代码jr t0跳转到usertrap
// jr指令，jr指令不会修改ra寄存器，它只是单纯改变PC值。所以此时ra可以认为是垃圾值


