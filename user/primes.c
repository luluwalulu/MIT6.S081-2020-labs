#include "kernel/types.h"
#include "user/user.h"

void func(int pread){
  int received_num,shaizi;
  int yes=0;
  if(read(pread,&shaizi,sizeof(int))==0) exit(0);
  printf("prime %d\n",shaizi);
  while(read(pread,&received_num,sizeof(int))!=0){
    if(received_num%shaizi!=0){ // 则读到第一个需要传输给下一个进程的数，需要创建下一个进程
      yes=1;
      break;
    }
    // printf("%d被丢弃\n",received_num);
  }
  
  if(yes){
    int p[2];
    pipe(p);
    // 应该在创建新分支之前pipe
    int pid=fork();
    if(pid==0){
      close(p[1]);
      func(p[0]);
      exit(0);
    }
    else{
      close(p[0]);
      write(p[1],&received_num,sizeof(int));
      while(read(pread,&received_num,sizeof(int))!=0){
        if(received_num%shaizi!=0){
          // printf("%d被发送给下一个进程\n",received_num);
          write(p[1],&received_num,sizeof(int));
        }
      }
      close(pread);
      close(p[1]);
      wait((int*)0);
      exit(0);
    }
  }
  exit(0);
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
    wait(0);
  }
  exit(0);
}

// 这是一个很巧妙的并发的素数筛，不同于每个数分别进行一次素数判别
// 该实现中，我们先将所有2的倍数筛除一遍，然后将所有3的倍数筛除一遍，直到最后只剩一个数，这个数一定是素数
// 这样每次筛选的时候，我们的候选集数量都在减少，提高了效率
// https://swtch.com/~rsc/thread/

// 对于一个文件描述符来说，在父子进程中同一文件描述符的一方关闭了，对于另一方的该文件描述符完全没有影响
// 对于管道而言，有这条规则：read 从管道的p[0]读取时，它返回 0 (EOF) 的唯一条件是：所有指向该管道p[1]的文件描述符都已被关闭。
// 但是返回的这个eof同样是需要从管道中读取到的，不是在符合条件的一瞬间马上就返回0。所以read仍然会按照次序先将eof之前的数据先进行读取
// 注：需要注意一个进程在从管道读取时，它自己的p[1]也指向管道。所以可能需要关闭它自己指向管道的p[1]