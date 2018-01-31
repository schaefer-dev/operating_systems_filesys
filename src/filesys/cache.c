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
void filesys_read_ahead_thread(void* aux UNUSED);
void filesys_cache_queue_read_ahead(block_sector_t disk_sector);
void filesys_cache_periodic_writeback(void* aux UNUSED);
struct cache_block *filesys_cache_access(block_sector_t disk_sector, bool write_access, bool recoursive);

#define MAX_READ_AHEAD_SIZE 32

struct lock read_ahead_lock;
struct list read_ahead_queue;
struct semaphore read_ahead_semaphore;
int read_ahead_queue_size;

struct read_ahead_entry {
  block_sector_t disk_sector;
  struct list_elem elem;
};

/* initializes cache structure */
void
filesys_cache_init(){
  //printf("DEBUG: init executed\n");
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    cache_array[i] = NULL;
  }

  lock_init(&filesys_cache_lock);
  lock_init(&filesys_cache_evict_lock);
  lock_init(&read_ahead_lock);
  list_init(&read_ahead_queue);
  sema_init(&read_ahead_semaphore, 0);
  read_ahead_queue_size = 0;

  next_free_cache = 0;
  next_evict_cache = 0;
  //printf("DEBUG: init terminated\n");

  // TODO enable periodic writeback again!
  thread_create("periodic_writeback", 0, filesys_cache_periodic_writeback , NULL);
  thread_create("read_ahead", 0, filesys_read_ahead_thread, NULL);
}


/* returns reference to cache_block of disk_sector or NULL if this
   disk_sector is currently not cached. the returned cache_field_lock is HELD! */
struct cache_block*
filesys_cache_lookup(block_sector_t disk_sector) {
  // TODO think about synchronization, cache block might have changed after lookup
  //printf("DEBUG: start lookup\n");
  int i = 0;
  for (i = 0; i < CACHE_SIZE; i++) {
    //printf("DEBUG: iter\n");
    if (cache_array[i] != NULL) {
      lock_acquire(&cache_array[i]->cache_field_lock);
      if (cache_array[i]->disk_sector == disk_sector){
        /* lock is held after return !! */
        return cache_array[i];
      }
      lock_release(&cache_array[i]->cache_field_lock);
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
    lock_release(&lookup_cache_block->cache_field_lock);
  } else {
    /* lookup has to hold the returned block cache lock */
    ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_field_lock));
    lookup_cache_block->accessed = true;
    lookup_cache_block->accessed_counter += 1;
    lookup_cache_block->dirty |= write_access;
    lock_release(&lookup_cache_block->cache_field_lock);
  }

  if (!recoursive){
    filesys_cache_queue_read_ahead(disk_sector + 1);
  }
  return lookup_cache_block;
}


/* read from disk starting at sector_offset chunk_size amount of bytes into buffer */
void
filesys_cache_read(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size) {
  //printf("DEBUG: read cache at sektor %i and offset %i with chunk_size %i BEGIN\n", disk_sector, sector_offset, chunk_size);
  ASSERT(buffer != NULL);
  struct cache_block *lookup_cache_block = filesys_cache_lookup(disk_sector);

  if (lookup_cache_block == NULL) {
    lookup_cache_block = filesys_cache_block_allocate(disk_sector, false);
  } else {
    lookup_cache_block->accessed_counter += 1;
  }

  /* lookup has to hold the returned block cache lock */
  ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_field_lock));
  lookup_cache_block->accessed = true;
  lookup_cache_block->read_writer_working += 1;
  lock_release(&lookup_cache_block->cache_field_lock);
  memcpy(buffer, (uint8_t *) &lookup_cache_block->cached_content + sector_offset, chunk_size);
  //printf("DEBUG: read cache END \n");


  lock_acquire(&lookup_cache_block->cache_field_lock);
  lookup_cache_block->read_writer_working -= 1;
  lock_release(&lookup_cache_block->cache_field_lock);

  // read ahead
  filesys_cache_queue_read_ahead(disk_sector + 1);
}


/* read from disk starting at sector_offset chunk_size amount of bytes into buffer */
void
filesys_cache_write(block_sector_t disk_sector, void *buffer, off_t sector_offset, int chunk_size) {
  //printf("DEBUG: write cache into sector %i at offset %i with chunk_size %i BEGIN\n", disk_sector, sector_offset, chunk_size);
  ASSERT(buffer != NULL);
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
  ASSERT(lock_held_by_current_thread(&lookup_cache_block->cache_field_lock));
  ASSERT(lookup_cache_block->disk_sector == disk_sector);
  lookup_cache_block->accessed = true;
  lookup_cache_block->dirty = true;
  lookup_cache_block->read_writer_working += 1;
  lock_release(&lookup_cache_block->cache_field_lock);
  memcpy((uint8_t *) &lookup_cache_block->cached_content + sector_offset, buffer, chunk_size);
  //printf("DEBUG: write cache END\n");
  lock_acquire(&lookup_cache_block->cache_field_lock);
  lookup_cache_block->read_writer_working -= 1;
  lock_release(&lookup_cache_block->cache_field_lock);

  // read ahead
  filesys_cache_queue_read_ahead(disk_sector + 1);
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
    new_cache_block->read_writer_working = 0;
    lock_init(&new_cache_block->cache_field_lock);
    /* write content of disk_sector to cached_content array */
    block_read(fs_device, disk_sector, new_cache_block->cached_content);

    cache_array[next_free_cache] = new_cache_block;
    next_free_cache += 1;
    lock_release(&filesys_cache_lock);
    lock_acquire(&new_cache_block->cache_field_lock);
    return new_cache_block;

  } else {
    /* case for eviction */
    // TODO probably not neccesary to keep evict lock, think about it
    lock_acquire(&filesys_cache_evict_lock);
    struct cache_block *replace_cache_block = filesys_cache_block_evict();
    ASSERT(lock_held_by_current_thread(&replace_cache_block->cache_field_lock));

    replace_cache_block->accessed = true;
    replace_cache_block->dirty = write_access;
    replace_cache_block->disk_sector = disk_sector;
    replace_cache_block->accessed_counter = 0;
    replace_cache_block->read_writer_working = 0;
    /* write content of disk_sector to cached_content array */
    block_read(fs_device, disk_sector, replace_cache_block->cached_content);
    lock_release(&filesys_cache_evict_lock);
    lock_release(&filesys_cache_lock);
    return replace_cache_block;
  }
}


/* simple clock algorithm to evict cache */
struct cache_block*
filesys_cache_block_evict() {
  ASSERT(lock_held_by_current_thread(&filesys_cache_evict_lock));
  ASSERT(lock_held_by_current_thread(&filesys_cache_lock));

  while(true) {
    struct cache_block *iter_cache_block = cache_array[next_evict_cache];
    lock_acquire(&iter_cache_block->cache_field_lock);
    if (iter_cache_block->accessed_counter > 0) {
      iter_cache_block->accessed = false;
      iter_cache_block->accessed_counter -= 1;
      lock_release(&iter_cache_block->cache_field_lock);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
    } else {
      if (iter_cache_block->read_writer_working > 0){
        /* dont evict entry if no readers / writers are currently working on it */
        lock_release(&iter_cache_block->cache_field_lock);
        next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
        continue;
      }
      if (iter_cache_block->dirty)
        block_write(fs_device, iter_cache_block->disk_sector, &iter_cache_block->cached_content);
      next_evict_cache = (next_evict_cache + 1) % CACHE_SIZE;
      /* holds iter_cache_block lock on return */
      return iter_cache_block;
    }
  }
}

/* function used by thread to read ahead */
void
filesys_read_ahead_thread(void *aux UNUSED) {
  while (true) {
      sema_down(&read_ahead_semaphore);
      lock_acquire(&read_ahead_lock);
      struct list_elem *head = list_pop_front(&read_ahead_queue);
      read_ahead_queue_size -= 1;
      lock_release(&read_ahead_lock);
      struct read_ahead_entry *read_ahead_entry = list_entry(head, struct read_ahead_entry, elem);
      filesys_cache_access(read_ahead_entry->disk_sector, false, true);
      free(read_ahead_entry);
    }
}


/* function used to add entry to the read ahead list */
void
filesys_cache_queue_read_ahead(block_sector_t disk_sector) {
  /* return if invalid sector */
  if (!verify_sector(fs_device, disk_sector) && disk_sector != 0)
      return;

  /* return if queue is already full */
  lock_acquire(&read_ahead_lock);
  if (read_ahead_queue_size >= MAX_READ_AHEAD_SIZE) {
    lock_release(&read_ahead_lock);
    return;
  }

  struct read_ahead_entry *read_ahead_entry = malloc(sizeof(struct read_ahead_entry));
  read_ahead_entry->disk_sector = disk_sector;
  list_push_back(&read_ahead_queue, &read_ahead_entry->elem);
  read_ahead_queue_size += 1;
  lock_release(&read_ahead_lock);
  sema_up(&read_ahead_semaphore);
}


/* function called periodically
   to write cache back to disk if dirty, runs into OPPOSITE direction of 
   evict to avoid slowdown caused by locking iteratively over array */
void
filesys_cache_writeback() {
  //printf("DEBUG writeback BEGIN\n");

  lock_acquire(&filesys_cache_lock);
  int iterator = next_free_cache - 1;
  lock_release(&filesys_cache_lock);

  while (iterator >= 0) {
    //printf("DEBUG writeback iter\n");
    struct cache_block *iterator_block = cache_array[iterator];
    //if (iterator_block==NULL){
	//printf("DEBUG writeback entry null\n");
    //}
    //printf("DEBUG writeback step1\n");
    
    lock_acquire(&iterator_block->cache_field_lock);
    //printf("DEBUG writeback step2\n");

    if (iterator_block->dirty){
      //printf("DEBUG writeback step3\n");
      block_write(fs_device, iterator_block->disk_sector, &iterator_block->cached_content);
      //printf("DEBUG writeback step4\n");
      iterator_block->dirty = false;
      //printf("DEBUG writeback step5\n");
    }

    lock_release(&iterator_block->cache_field_lock);
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
