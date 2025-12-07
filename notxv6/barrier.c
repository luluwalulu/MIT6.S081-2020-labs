#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread++;
  int last=0;
  
  // 最后一个到达屏障的进程负责唤醒
  if(bstate.nthread==nthread){
    pthread_cond_broadcast(&bstate.barrier_cond);
    last=1;
  }
  else{
    // 其他进程在这里阻塞
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }

  // 醒来之后只有最后一个到达的线程持有锁，由该线程将初始化nthread并递增round
  // 这个最后一个到达的线程可能以极快的速度到达下一轮屏障并获取锁，然后将nthread++
  // 导致上一轮醒来的线程可能看到nthread不为0，然后误以为nthread还没有清零，因此必须引入last
  if(bstate.nthread!=0&&last){
    bstate.nthread=0;
    bstate.round++;
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    // if (i != t) {
    // // 必须用 stderr，防止因为缓冲区没刷新导致打印不出来
    // fprintf(stderr, "ASSERT FAIL: i = %d, t = %d\n", i, t);
    // }
    // if(i==t){
    //   fprintf(stderr, "SUCCESS: i = %d, t = %d\n", i, t);
    // }
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
