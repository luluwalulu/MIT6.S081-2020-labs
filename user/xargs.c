#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/param.h"

// 命令中的|表示管道，会把|前面命令的输出直接作为后一个命令的输入
// echo hello too | echo bye:完全不从stdin读任何内容，只使用argv接受的参数（bye）。前者的输出被丢弃。
// echo hello too | xargs echo bye:从标准输入中读取内容，然后调用后面的命令，并把这些参数附加在后面

int main(int argc,char* argv[]){
  char* argv2[MAXARG];
  // 新的argv2中需要把参数xargs去掉，然后执行真正的可执行文件
  for(int i=1;i<argc;i++) argv2[i-1]=argv[i];
  argc--;

  char buffer[500];
  char *p=buffer;
  while(read(0,p,1)>0){
    if(*p=='\n'){
      *p=0;
      
      char *temp=malloc(strlen(buffer)+1);
      strcpy(temp,buffer);
      argv2[argc++]=temp;

      p=buffer;
      continue;
    }
    p++;
  }

  int pid=fork();
  if(pid==0){
    exec(argv2[0],argv2);
    exit(0);
  }
  else{
    wait(0);
    exit(0);
  }
}