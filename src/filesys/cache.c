#include "filesys/cache.h"
#include "devices/timer.h"
#include "devices/block.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include <stdio.h>
#include <string.h>


struct cache_block *filesys_cache_lookup(block_sector_t disk_sector);
struct cache_block *filesys_cache_block_allocate(block_sector_t disk_sector, bool write_access);
struct cache_block *filesys_cache_block_evict(void);
void filesys_cache_thread_read_ahead(block_sector_t disk_sector);
void filesys_cache_periodic_writeback(void* aux UNUSED);
struct cache_block *filesys_cache_access(block_sector_t disk_sector, bool write_access, bool recoursive);


/* initializes cache structure */
void
filesys_cache_init(){
  //printf("DEBUG: init executed\n");
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    cache_array[i] = NULL;
  }

  lock_init(&filesys_cache_lock);
  next_free_cache = 0;
  //printf("DEBUG: init terminated\n");

  // TODO enable periodic writeback again!
  thread_create("periodic_writeback", 0, filesys_cache_periodic_writeback , NULL);
}


/* returns reference to cache_block of disk_sector or NULL if this
   disk_sector is currently not cached. the returned cache_block_lock is HELD! */
struct cache_block*
filesys_cache_lookup(block_sector_t disk_sector) {
  // TODO think about synchronization, cache block might have changed after lookup
  //printf("DEBUG: start lookup\n");
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    //printf("DEBUG: iter\n");
    if (cache_array[i] != NULL) {
      lock_acquire(&cache_array[i]->cache_block_lock);
      if (cache_array[i]->disk_sector == disk_sector)
        return cache_array[i];
      lock_release(&cache_array[i]->cache_block_lock);
    }
  }

  return NULL;
}


/* access the disk sector at disk_sector, which causes this disk_sector to be
   loaded into the cache structure (if not loaded already) also updates METADATA
   necessary for eviciton.
   ALWAYS CALL WITH RECOURSIVE=FALSE! */
struct cache_block*
filesys_cache_access(block_sector_t disk_sector, bool write_access, bool recoursive){
  struct cache_block *lookup_cache_block = filesys_cache_lookup(disk_sector);
  if (lookup_cache_block == NULL){
    lookup_cache_block = filesys_cache_block_allocate(disk_sector, write_access);
    lock_release(&lookup_cache_block->cache_block_lock);
  } else {
    /* lookup has to hold the returned block cache lock */
    ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_block_lock));
    lookup_cache_block->accessed = true;
    lookup_cache_block->accessed_counter += 1;
    lookup_cache_block->dirty |= write_access;
    lock_release(&lookup_cache_block->cache_block_lock);
  }

  if (!recoursive){
    //filesys_cache_thread_read_ahead(disk_sector + 1);
  }
  return lookup_cache_block;
}


/* read from disk starting at sector_offset chunk_size amount of bytes into buffer */
void
filesys_cache_read(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size) {
  //printf("DEBUG: read cache at sektor %i and offset %i with chunk_size %i BEGIN\n", disk_sector, sector_offset, chunk_size);
  struct cache_block *lookup_cache_block = filesys_cache_lookup(disk_sector);

  if (lookup_cache_block == NULL) {
    lookup_cache_block = filesys_cache_block_allocate(disk_sector, false);
  } else {
    lookup_cache_block->accessed_counter += 1;
  }

  /* lookup has to hold the returned block cache lock */
  ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_block_lock));
  lookup_cache_block->accessed = true;
  memcpy(buffer, (uint8_t *) &lookup_cache_block->cached_content + sector_offset, chunk_size);
  lock_release(&lookup_cache_block->cache_block_lock);
  //printf("DEBUG: read cache END \n");

  // read ahead
  //filesys_cache_thread_read_ahead(disk_sector + 1);
}


/* read from disk starting at sector_offset chunk_size amount of bytes into buffer */
void
filesys_cache_write(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size) {
  //printf("DEBUG: write cache into sector %i at offset %i with chunk_size %i BEGIN\n", disk_sector, sector_offset, chunk_size);
  struct cache_block *lookup_cache_block = filesys_cache_lookup(disk_sector);

  if (lookup_cache_block == NULL) {
    //printf("DEBUG: write cache step 2\n");
    lookup_cache_block = filesys_cache_block_allocate(disk_sector, false);
    //printf("DEBUG: write cache step 3\n");
  } else {
    lookup_cache_block->accessed_counter += 1;
  }
  
  //printf("DEBUG: write cache step 4\n");

  /* lookup has to hold the returned block cache lock */
  ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_block_lock));
  ASSERT(lookup_cache_block->disk_sector == disk_sector);
  lookup_cache_block->accessed = true;
  lookup_cache_block->dirty = true;
  memcpy((uint8_t *) &lookup_cache_block->cached_content + sector_offset, buffer, chunk_size);
  lock_release(&lookup_cache_block->cache_block_lock);
  //printf("DEBUG: write cache END\n");

  // read ahead
  //filesys_cache_thread_read_ahead(disk_sector + 1);
}


/* allocate new cache for disk_sector, which might call eviction to instead replace a
   currently cached sector holds cache_block lock afterwards*/
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
    block_read(fs_device, disk_sector, new_cache_block->cached_content);

    cache_array[next_free_cache] = new_cache_block;
    next_free_cache += 1;
    lock_release(&filesys_cache_lock);
    lock_acquire(&new_cache_block->cache_block_lock);
    return new_cache_block;

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
    block_read(fs_device, disk_sector, replace_cache_block->cached_content);
    return replace_cache_block;
  }
}


/* simple clock algorithm to evict cache */
struct cache_block*
filesys_cache_block_evict() {
  while(true) {
    struct cache_block *iter_cache_block = cache_array[next_evict_cache];
    lock_acquire(&iter_cache_block->cache_block_lock);
    if (iter_cache_block->accessed) {
      iter_cache_block->accessed = false;
      iter_cache_block->accessed_counter = 0;
      lock_release(&iter_cache_block->cache_block_lock);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
    } else {
      if (iter_cache_block->dirty)
        block_write(fs_device, iter_cache_block->disk_sector, &iter_cache_block->cached_content);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
      return iter_cache_block;
    }
  }
}

/* function used by thread to read ahead */
void
filesys_cache_read_ahead(void *aux) {
  block_sector_t sector = *( (block_sector_t*) aux);
  filesys_cache_access(sector, false, true);
  free (aux);
}


/* create thread which reads ahead asynchronously */
void
filesys_cache_thread_read_ahead (block_sector_t disk_sector) {
  block_sector_t *aux = malloc(sizeof(block_sector_t));
  *aux = disk_sector;
  thread_create("async_read_ahead", 0, filesys_cache_read_ahead, aux);
}


/* write cache back to disk if dirty, runs into OPPOSITE direction of 
   evict to avoid slowdown caused by locking iteratively over array */
void
filesys_cache_writeback() {
  //printf("DEBUG writeback BEGIN\n");

  int iterator = next_free_cache - 1;

  while (iterator >= 0) {
    //printf("DEBUG writeback iter\n");
    struct cache_block *iterator_block = cache_array[iterator];
    //if (iterator_block==NULL){
	//printf("DEBUG writeback entry null\n");
    //}
    //printf("DEBUG writeback step1\n");
    
    lock_acquire(&iterator_block->cache_block_lock);
    //printf("DEBUG writeback step2\n");

    if (iterator_block->dirty){
      //printf("DEBUG writeback step3\n");
      block_write(fs_device, iterator_block->disk_sector, &iterator_block->cached_content);
      //printf("DEBUG writeback step4\n");
      iterator_block->dirty = false;
      //printf("DEBUG writeback step5\n");
    }

    lock_release(&iterator_block->cache_block_lock);
    iterator -= 1;
  }
  //printf("DEBUG writeback END\n");
}


/* function which is executed by asynchronous thread and causes a call of
   filesys_cache_writeback every WRITE_BACK_INTERVAL milliseconds */
void filesys_cache_periodic_writeback(void* aux UNUSED) {
  while (true) {
    timer_msleep(WRITE_BACK_INTERVAL);
    filesys_cache_writeback();
  }
}
