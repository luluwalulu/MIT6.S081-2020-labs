#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// path是函数当前所处的路径，filename是我们查找的目标
// 找到了就立刻打印路径
void func(char* path,char *filename){
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){ // 将当前所处路径的详细信息存储在st中
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  strcpy(buf,path);
  p=buf+strlen(path);
  *p++='/';
  while(read(fd,&de,sizeof(de))==sizeof(de)){
    if(de.inum==0) continue;
    if(de.name[0]=='.') continue;
    // de.name被name部分和0填充
    memmove(p,de.name,DIRSIZ);
    p[DIRSIZ]=0;
    if(stat(buf,&st)<0){
      printf("find: cannot stat %s\n",buf);
      continue;
    }
    
    if(st.type==T_FILE){
      char temp[DIRSIZ+5];
      memmove(temp,de.name,DIRSIZ);
      temp[DIRSIZ]=0;
      if(strcmp(temp,filename)==0){
        printf("%s\n",buf);
      }
    }
    else if(st.type==T_DIR){
      func(buf,filename);
    }
  }
  close(fd);
}

int main(int argc,char* argv[]){
  if(argc!=3){
    printf("argc should be 3!\n");
    exit(-1);
  }
  func(argv[1],argv[2]);
  exit(0);
}