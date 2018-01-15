#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

#define CACHE_SIZE 64

/* write back every second */
#define WRITE_BACK_INTERVAL 1000

// TODO additional hashmap from disk_sector -> cache_block to speed up lookup

struct lock filesys_cache_lock;

struct cache_block {
  /* METADATA */
  bool accessed;
  bool dirty;
  int accessed_counter;

  struct lock cache_block_lock;

  /* Cached content + disk sector */
  uint8_t cached_content[BLOCK_SECTOR_SIZE];
  block_sector_t disk_sector;
};

struct cache_block *cache_array[CACHE_SIZE];

int next_free_cache;
int next_evict_cache;


void filesys_cache_init(void);
void filesys_cache_read(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size);
void filesys_cache_write(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size);
void filesys_cache_writeback(void);

#endif
