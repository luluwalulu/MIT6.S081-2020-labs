#include "kernel/types.h"
#include "user/user.h"

int main(int argc,char *argv[]){
  if(argc==0){
    printf("Error:参数的数量为0");
    exit(1);
  }

  int status=0;
  int pid=fork();

  if(pid==0){ // 子进程
    sleep(atoi(argv[1]));
    exit(0);
  }
  else{ // 父进程
    wait(&status);
  }

  exit(0);
}

// user.h里面提供的是用户态的sleep的代码，它保证了用户空间有一个名叫sleep的代码，而usys.S中则是user.h的汇编代码。
// sysproc.c中则是内核态对sleep真正实现的代码。
// 很重要的一点：代码的抽象层次”（C vs 汇编）和“CPU 的运行模式”（内核态 vs 用户态）是两个完全独立、互不相干的概念。

// atoi:将字符串转换为整数，并返回整数