#include "kernel/types.h"
#include "user/user.h"

int main(){
  int p1[2],p2[2];
  pipe(p1);
  pipe(p2);

  char buf[5];
  int pid=fork();
  if(pid==0){ // 子进程，从p1[0]读，向p2[1]写
    close(p1[1]);
    close(p2[0]);
    read(p1[0],buf,1);
    printf("%d: received ping\n",getpid());
    write(p2[1],buf,1);
  }
  else{ // 父进程，从p2[0]读，向p1[1]写
    close(p2[1]);
    close(p1[0]);
    write(p1[1],"a",1);
    read(p2[0],buf,1);
    printf("%d: received pong\n",getpid());
    wait(0);
  }
  exit(0);
}

// 这里是把管道本身当作文件描述符指向的对象，p[0]即为从管道中读取，而非从管道对面的对象读取
// 如果按照后面的思维进行的话，那么对于A，p[0]是从B读取，对于B呢？

// 错误：当我只使用一个管道时，由于父进程和子进程可能从同一个管道那里读入数据
// 所以，我们看到的是控制台只打印了3:received pong，这是因为父进程自己向管道写入后读取了自己向管道写入的内容
    // 而子进程根本什么都没有读到
// 根本上是无法避免自己从管道读入自己向管道写入的内容
// 所以我们使用双管道