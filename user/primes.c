#include "kernel/types.h"
#include "user/user.h"
#include <math.h>

int is_prime(int n){
  for(int i=2;i<=sqrt(n);i++){
    if(n%i==0){
      return 0;
    }
  }
  return 1;
}

void func(int pread){
  // 这是当前主进程连接右侧子进程的管子，pread是从左侧管子读的文件描述符
  int p[2];
  pipe(p);
  int pid=fork();
  if(pid==0){
    close(p[1]); // 子进程需要关闭p[1]，然后从p[0]读取
    func(p[0]);
  }
  else{ // 主进程从左侧管道读取内容，然后从右侧管道向子进程写出内容
    close(p[0]); // 主进程需要关闭p[0]
    int received_num=1;
    int shaizi=0;
    int total=0;
    while(read(pread,&received_num,sizeof(int))!=0){
      total++;
      if(shaizi==0){
        shaizi=received_num;
        printf("prime %d\n",received_num);
        continue;
      }
      if(received_num%shaizi!=0){
        write(p[1],&received_num,sizeof(int));
      }
    }
    // 循环结束后，主进程已经从左边读取了所有数据，并向右边发送了所有数据
    close(pread);
    close(p[1]);
    wait((int*)0);
    exit(0);
  }
}

int main(){
  int p[2];
  pipe(p);
  int pid=fork();
  if(pid==0){
    close(p[1]);
    func(p[0]);
  }
  else{
    close(p[0]);
    for(int i=2;i<=35;i++){
      write(p[1],&i,sizeof(int));
    }
    close(p[1]);
  }
  return 0;
}

// 这是一个很巧妙的并发的素数筛，不同于每个数分别进行一次素数判别
// 该实现中，我们先将所有2的倍数筛除一遍，然后将所有3的倍数筛除一遍，直到最后只剩一个数，这个数一定是素数
// 这样每次筛选的时候，我们的候选集数量都在减少，提高了效率
// https://swtch.com/~rsc/thread/

// 对于一个文件描述符来说，在父子进程中同一文件描述符的一方关闭了，对于另一方的该文件描述符完全没有影响
// 对于管道而言，有这条规则：read 从管道的p[0]读取时，它返回 0 (EOF) 的唯一条件是：所有指向该管道p[1]的文件描述符都已被关闭。
// 但是返回的这个eof同样是需要从管道中读取到的，不是在符合条件的一瞬间马上就返回0。所以read仍然会按照次序先将eof之前的数据先进行读取
// 注：需要注意一个进程在从管道读取时，它自己的p[1]也指向管道。所以可能需要关闭它自己指向管道的p[1]