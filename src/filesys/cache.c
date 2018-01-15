#include "filesys/cache.h"


struct cache_block *filesys_cache_lookup(block_sector_t disk_sector);
struct cache_block *filesys_cache_block_allocate(block_sector_t disk_sector, bool write_access);
struct cache_block *filesys_cache_block_evict();
void filesys_cache_thread_read_ahead(block_sector_t disk_sector);
void filesys_cache_periodic_writeback(void* aux UNUSED);


/* initializes cache structure */
void
filesys_cache_init(){
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    cache_array[i] = NULL;
  }

  lock_init(&filesys_cache_lock);
  next_free_cache = 0;

  thread_create("periodic_writeback", 0, filesys_cache_periodic_writeback , NULL);
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


/* access the disk sector at disk_sector, which causes this disk_sector to be
   loaded into the cache structure (if not loaded already) also updates METADATA
   necessary for eviciton.
   ALWAYS CALL WITH RECOURSIVE=FALSE! */
struct cache_block*
filesys_cache_access(block_sector_t disk_sector, bool write_access, bool recoursive){
  struct cache_block *lookup_cache_block = lookup_cache_block(disk_sector);
  if (lookup_cache_block == NULL){
    lookup_cache_block = filesys_cache_block_allocate(disk_sector, write_access);
  } else {
    /* lookup has to hold the returned block cache */
    ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_block_lock));
    lookup_cache_block->accessed = true;
    lookup_cache_block->accessed_counter += 1;
    lookup_cache_block->dirty |= write_access;
    lock_release(&lookup_cache_block->cache_block_lock);
  }
  if (!recoursive){
    filesys_cache_thread_periodic_writeback(disk_sector + 1);
  }
  return lookup_cache_block;
}


/* allocate new cache for disk_sector, which might call eviction to instead replace a
   currently cached sector */
struct cache_block*
filesys_cache_block_allocate(block_sector_t disk_sector, bool write_access) {

  lock_acquire(&filesys_cache_lock);
  if (next_free_cache < CACHE_SIZE) {
    /* case for new cache entry allocation */
    struct cache_block *new_cache_block =
      (struct cache_block*) malloc(sizeof(struct cache_block));
    new_cache_block->accessed = true;
    new_cache_block->dirty = write_access;
    new_cache_block->disk_sector = disk_sector;
    new_cache_block->accessed_counter = 0;
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
    replace_cache_block->dirty = write_access;
    replace_cache_block->disk_sector = disk_sector;
    replace_cache_block->accessed_counter = 0;
    /* write content of disk_sector to cached_content array */
    block_read(fs_device, disk_sector, replace_cache_block->content);

    lock_release(&replace_cache_block->cache_block_lock);
  }
}


/* simple clock algorithm to evict cache */
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

/* function used by thread to read ahead */
void
filesys_cache_read_ahead(void *aux) {
  // TODO maybe we have to pass sector using a reference to sector instead!
  block_sector_t sector = (block_sector_t) aux;
  filesys_cache_access(sector, false, true);
}


/* create thread which reads ahead asynchronously */
void
filesys_cache_thread_read_ahead (block_sector_t disk_sector) {
  void *aux = (void *) disk_sector;
  thread_create("async_read_ahead", 0, filesys_cache_read_ahead, aux);
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


void filesys_cache_periodic_writeback(void* aux UNUSED) {
  while (true) {
    timer_msleep(WRITE_BACK_INTERVAL);
    filesys_cache_writeback();
  }
}
