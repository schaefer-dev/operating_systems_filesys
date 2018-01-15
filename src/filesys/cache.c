#include "filesys/cache.h"

/* initializes cache structure */
void
filesys_cache_init(){
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    cache_array[i] = NULL;
  }

  lock_init(&filesys_cache_lock);
  next_free_cache = 0;
}

/* returns reference to cache_block of disk_sector or NULL if this
   disk_sector is currently not cached. the returned cache_block_lock is HELD! */
struct cache_block*
filesys_cache_lookup(block_sector_t disk_sector) {
  // TODO think about synchronization, cache block might have changed after lookup
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) 
    if (cache_array[i] != NULL) 
      lock_acquire(&cache_array[i]->cache_block_lock);
      if (cache_array[i]->disk_sector == disk_sector)
        return cache_array[i];
      lock_release(&cache_array[i]->cache_block_lock);

  return NULL;
}


struct cache_block*
filesys_cache_block_allocate(block_sector_t disk_sector) {
  // TODO Assert for not contained!

  lock_acquire(&filesys_cache_lock);
  if (next_free_cache < CACHE_SIZE) {
    /* case for new cache entry allocation */
    struct cache_block *new_cache_block =  (struct cache_block*) malloc(sizeof(struct cache_block));
    new_cache_block->accessed = true;
    new_cache_block->dirty = false;
    new_cache_block->disk_sector = disk_sector;
    lock_init(&new_cache_block->cache_block_lock);
    /* write content of disk_sector to cached_content array */
    block_read(fs_device, disk_sector, new_cache_block->content);

    cache_array[next_free_cache] = new_cache_block;
    next_free_cache += 1;
    lock_release(&filesys_cache_lock);

  } else {
    /* case for eviction */
    lock_release(&filesys_cache_lock);
    struct cache_block *replace_cache_block = filesys_cache_block_evict();
    ASSERT(lock_held_by_current_thread(&replace_cache_block->cache_block_lock));

    replace_cache_block->accessed = true;
    replace_cache_block->dirty = false;
    replace_cache_block->disk_sector = disk_sector;
    /* write content of disk_sector to cached_content array */
    block_read(fs_device, disk_sector, replace_cache_block->content);

    lock_release(&replace_cache_block->cache_block_lock);
  }
}


/* write cache back to disk if dirty, runs into OPPOSITE direction of 
   evict to avoid slowdown caused by locking iteratively over array */
void
filesys_cache_writeback() {
  int iterator = next_free_cache;

  while (iterator != 0) {
    struct cache_block *iterator_block = cache_array[iterator];

    lock_acquire(&iterator_block->cache_block_lock);

    if (iterator_block->dirty){ 
      block_write(fs_device, iterator_block->disk_sector, iterator_block->content);
      iterator_block->dirty = false;
    }

    lock_release(&iterator_block->cache_block_lock);
    iterator -= 1;
  }

}


struct cache_block*
filesys_cache_block_evict() {
  while(true) {
    struct cache_block *iter_cache_block;
    lock_acquire(&iter_cache_block->cache_block_lock);
    if (iter_cache_block->accessed) {
      iter_cache_block->accessed = false;
      iter_cache_block->accessed_counter = 0;
      lock_release(&iter_cache_block->cache_block_lock);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
    } else {
      if (iter_cache_block->dirty)
        block_write(fs_device, iter_cache_block->disk_sector, iter_cache_block->content);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
      return iter_cache_block;
    }
  }
}
