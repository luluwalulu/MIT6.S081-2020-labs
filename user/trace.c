#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 当我们调用 $ trace 32 grep hello README 时发生什么：我们让trace跟踪read系统调用，然后执行grep hello README，打印执行grep过程中read的系统调用

// 当我们在用户态调用一个系统调用fork时，实际上我们调用的是user/user.h中的声明，它链接到user/usys.S
// user/usys.S调用ecall陷入内核，最终返回系统调用值
// ecall最终让我们调用syscall函数
// syscall调用比如sys_fork()函数，sys_fork又调用fork

// 而sys_trace不需要再调用trace，sys_trace直接可以完成工作

int
main(int argc, char *argv[])
// 第一个参数是字符串"trace"，第二个参数是掩码，第三个参数是可执行文件名
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  if (trace(atoi(argv[1])) < 0) { // trace函数在这里执行，该条if语句对应trace失败
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv); // 在这里才调用exec
  exit(0);
}
