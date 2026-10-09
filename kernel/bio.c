// Buffer cache.
//
// The buffer cache is a hash table of buf structures holding
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

#define NBUCKETS 13

struct {
  struct spinlock lock;
  struct buf *head;
} buckets[NBUCKETS];

struct {
  // Serialize misses, including their second lookup and buffer migration.
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache.evict");
  for(int i = 0; i < NBUCKETS; i++)
    initlock(&buckets[i].lock, "bcache.bucket");

  for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->dev = (uint)-1;
    b->next = buckets[0].head;
    buckets[0].head = b;
  }
}

// Caller holds the bucket lock. Reserve a hit before dropping that lock.
static struct buf*
blookup(uint dev, uint blockno)
{
  for(struct buf *b = buckets[blockno % NBUCKETS].head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      return b;
    }
  }
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = blockno % NBUCKETS;

  acquire(&buckets[h].lock);
  b = blookup(dev, blockno);
  release(&buckets[h].lock);
  if(b){
    acquiresleep(&b->lock);
    return b;
  }

  acquire(&bcache.lock);
  // Another CPU may have installed the block while we waited.
  acquire(&buckets[h].lock);
  b = blookup(dev, blockno);
  release(&buckets[h].lock);
  if(b){
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }

  // Remove an unused buffer under its source lock. No bucket locks nest.
  for(int i = 0; i < NBUCKETS; i++){
    int source = (h + i) % NBUCKETS;
    acquire(&buckets[source].lock);
    struct buf **link = &buckets[source].head;
    for(b = *link; b; link = &b->next, b = *link){
      if(b->refcnt == 0){
        *link = b->next;
        break;
      }
    }
    release(&buckets[source].lock);
    if(b){
      acquire(&buckets[h].lock);
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->next = buckets[h].head;
      buckets[h].head = b;
      release(&buckets[h].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
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

// Release a locked buffer. Referenced buffers cannot migrate between buckets.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int h = b->blockno % NBUCKETS;
  acquire(&buckets[h].lock);
  b->refcnt--;
  release(&buckets[h].lock);
}

void
bpin(struct buf *b) {
  int h = b->blockno % NBUCKETS;
  acquire(&buckets[h].lock);
  b->refcnt++;
  release(&buckets[h].lock);
}

void
bunpin(struct buf *b) {
  int h = b->blockno % NBUCKETS;
  acquire(&buckets[h].lock);
  b->refcnt--;
  release(&buckets[h].lock);
}
