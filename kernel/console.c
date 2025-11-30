//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
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

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf, and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    // uartputc_sync('\b')会使光标回退一格
    // uartputc_sync(' ')会在当前位置打印空格覆盖当前字符

    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  
  // input
#define INPUT_BUF 128
  char buf[INPUT_BUF];
  uint r;  // Read index  消费者指针，consoleread已经读到了这里
  uint w;  // Write index 提交指针，这之前的数据是确定的，consoleread可以读这部分数据了
  uint e;  // Edit index  编辑指针，用户当前正在打字的位置
} cons;

//
// user write()s to the console go here.
//
// 将用户空间中的n个字符直接拷贝到位于UART中的输出缓冲区内
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  acquire(&cons.lock);
  for(i = 0; i < n; i++){
    char c;
    // either_copyin是一个能够安全处理内核/用户地址区分的拷贝函数
    // 它尝试从src+i读取1个字节到变量c中
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    // 把字符存入UART中的输出缓冲区
    uartputc(c);
  }
  release(&cons.lock);

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//

// user_dst标记dst是在用户空间(1)还是内核空间(0)
// 对console中的输入缓冲区执行一次read
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      // 如果输入缓冲区为空，进入睡眠状态
      if(myproc()->killed){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        // 保留^D，使用户下次调用该函数时才消耗掉这个^D
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
// 接收uartintr传来的原始字符，进行加工，存入缓冲区，并决定何时调用consoleread
void
consoleintr(int c)
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list. 一个内核调试功能
    procdump();
    break;
  case C('U'):  // Kill line. cons.e直到删到cons.w，这部分也可以称为草稿区
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f':
    if(cons.e != cons.w){ // 删除草稿区中的一个字符
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user. 把消息回传给UART，然后显示到屏幕上
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF] = c;

      if(c == '\n' || c == C('D') || cons.e == cons.r+INPUT_BUF){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
// 初始化控制台子系统
{
  initlock(&cons.lock, "cons");

  // 初始化UART硬件
  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.

  // devsw负责维护系统中所有设备的驱动程序入口
  // 当有人拿着设备号CONSOLE来访问read时就会访问consoleread
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
