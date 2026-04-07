#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void panic(char *s) {
  fprintf(2, "%s\n", s);
  exit(1);
}

int fork1(void) {
  int pid;
  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

int main(int argc, char*argv[]) {
  if(argc<4){
    printf("Usage: %s <number-of-child-processes> <workload> <max-ticks-at-bottom>\nHere, workload is indicated as a positive integer\n", argv[0]);
    exit(0);
  };

  int fd[2];
  int nChild=0;
  nChild=atoi(argv[1]);

  int workload=10;
  workload=atoi(argv[2]);

  //call to start Priority scheduler
  int numLevel = 4;
  int maxTickToBoost = 200;
  maxTickToBoost = atoi(argv[3]);

  int callResult=startPriority(numLevel,maxTickToBoost);
  printf("Return from startPriority: %d\n", callResult);

  //create a pipe for mutual exclusion
  pipe(fd);
  char msg = 'A';
  // write(fd[1],&msg,1);

  //create child processes
  int ret=0;
  for(int i=0; i<nChild; i++){
    ret=fork1();
    if(ret==0) break;
  }

  //computation task
  int t=0;
  while(t++<workload){
    double x=987654321.9;
    for(int i=0; i<100000000; i++){
      x /= 12345.6789;
    }
  }

  if(ret>0){//only parent proceed without waiting
    callResult=stopPriority();
    printf("Return from stopPriority: %d\n", callResult);
  } else {
    read(fd[0],&msg,1); //similar to acquire a lock
  }

  printf("\n\nThis is process: %d\n", getpid());
  struct PriorityInfoReport priorityInfo;
  callResult = getPriorityInfo(&priorityInfo);
  for(int i=0; i<numLevel; i++){
    printf("tickCounts[%d]: %d\n", i, priorityInfo.tickCounts[i]);
  }

  write(fd[1],&msg,1); //similar to unlock
  return 0;
}