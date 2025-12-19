// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf bucket[13];
  struct spinlock locks[13];
} bcache;

void
binit(void)
{
  struct buf *b;

  for(int i=0;i<13;i++) initlock(&bcache.locks[i],"bcache");

  // 初始化所有桶中的首节点
  for(int i=0;i<12;i++){
    bcache.bucket[i].next=0;
  }
  struct buf* prev=&bcache.bucket[0];
  // 一开始把所有buf都放在bucket[0]当中
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock,"buffer");
    prev->next=b;
    prev=b;
  }
  prev->next=0;

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b,*prev;
  int bucket=blockno%13;
  acquire(&bcache.locks[bucket]);

  // 查找的块是否已经存在于哈希表中
  for(b = bcache.bucket[bucket].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果未存在，当前桶是否有空闲块可以直接存储数据
  for(b = bcache.bucket[bucket].next; b != 0; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.locks[bucket]);


  // 如果当前桶也没有空闲块，那么遍历整个哈希表寻找空闲块

  for(int i=(bucket+1)%13;i!=bucket;i=(i+1%13)){
    acquire(&bcache.locks[i]);
    for(b = bcache.bucket[i].next,prev=&bcache.bucket[i]; b != 0; prev=b,b = b->next){
      // 如果在其他桶中发现空闲块，那么直接将空闲块移到bucket中
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // 将当前空闲块移动到bucket当中
        acquire(&bcache.locks[bucket]);
        prev->next=b->next;
        struct buf* temp;
        for(temp=&bcache.bucket[bucket];temp->next!=0;temp=temp->next);
        temp->next=b;
        b->next=0;

        acquiresleep(&b->lock);
        release(&bcache.locks[i]);
        release(&bcache.locks[bucket]);
        return b;
      }
    }
    release(&bcache.locks[i]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  // 在这一步中，b不会从一个桶移动到另一个桶中
  // 如果brelse正确使用的话，那么b的refcnt一定不会为0，b一定不会被移动
  // 我一定获取的是正确的锁
  acquire(&bcache.locks[b->blockno%13]);
  b->refcnt--;
  release(&bcache.locks[b->blockno%13]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno%13]);
  b->refcnt++;
  release(&bcache.locks[b->blockno%13]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno%13]);
  b->refcnt--;
  release(&bcache.locks[b->blockno%13]);
}


