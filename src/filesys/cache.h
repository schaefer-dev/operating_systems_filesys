#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

/* defines the maximal number of cache entries */
#define CACHE_SIZE 64

/* write back every second */
#define WRITE_BACK_INTERVAL 1000

/* lock to lock the complete cache used e.g. to ensure that create is
atomic and the is no race to set the next free cache*/
struct lock filesys_cache_lock;
/* used to ensure that only 1 thread at a time can evict a page */
struct lock filesys_cache_evict_lock;

struct cache_block {
  /* METADATA */
  /* inidicates if the cache block is accessed */
  bool accessed;
  /* inidicates if the cache block is dirty (used for write behind) */ 
  bool dirty;
  /* counts how often the page is accessed increment on every read, write and
  access */ 
  int accessed_counter;

  /* indicates if a reader or writer is currently using the block and how many;
  increment at start of read/write and decrement on exit of read/write */
  int read_writer_working;

  /* lock used to lock metadata updates */
  struct lock cache_field_lock;

  /* Cached content + disk sector */
  uint8_t cached_content[BLOCK_SECTOR_SIZE];
  block_sector_t disk_sector;
};

/* structure(array) to store cache blocks */
struct cache_block *cache_array[CACHE_SIZE];

/* indicates the next free block in the cache */
int next_free_cache;
/* points to the block which is next considered in clock algorithm during
eviction */
int next_evict_cache;


void filesys_cache_init(void);
void filesys_cache_read(block_sector_t disk_sector, void *buffer,
 off_t sector_offset, int chunk_size);
void filesys_cache_write(block_sector_t disk_sector, void *buffer,
 off_t sector_offset, int chunk_size);
void filesys_cache_writeback(void);

#endif
