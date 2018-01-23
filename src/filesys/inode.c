#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "devices/block.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


static block_sector_t byte_to_sector_indirect (const struct inode *inode, off_t pos);
static block_sector_t byte_to_sector_double_indirect (const struct inode *inode, off_t pos);

size_t number_of_sectors (off_t size);
size_t number_of_direct_sectors (off_t size);
size_t number_of_indirect_sectors (off_t size);
size_t number_of_double_indirect_sectors (off_t size);
size_t compute_index_double_indirect (off_t size);
size_t compute_index_indirect_from_double (off_t size);



bool inode_allocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator, size_t index_offset);
bool inode_allocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors, size_t start_index_indirect, size_t  start_index_double_indirect);

void inode_deallocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator);
void inode_deallocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors);

bool inode_grow(struct inode *inode, struct inode_disk *inode_disk, off_t size, off_t offset);

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}



bool
inode_allocate (struct inode_disk *inode_disk)
{
  if (inode_disk->length > MAX_FILESIZE)
    return false;

  size_t num_of_sectors = number_of_sectors(inode_disk->length);

  uint8_t zero_sector[BLOCK_SECTOR_SIZE]; 
  int zero_iterator = 0;
  for (zero_iterator = 0; zero_iterator < BLOCK_SECTOR_SIZE; zero_iterator++){
    zero_sector[zero_iterator] = 0;
  }


  int current_index = 0;
  int indirect_index = 0;
  int double_indirect_index = 0;

  bool success = true;

  while (num_of_sectors > 0 && current_index < NUMBER_DIRECT_BLOCKS) {
    success &= free_map_allocate (1, &inode_disk->direct_pointers[current_index]);
    block_write(fs_device, inode_disk->direct_pointers[current_index], zero_sector);
    inode_disk->current_index = current_index;
    num_of_sectors -= 1;
    current_index += 1;
  }

  /* still stuff to write after direct blocks filled */
  if (current_index == NUMBER_DIRECT_BLOCKS && num_of_sectors > 0){
    current_index = 0;
    inode_disk->current_index = current_index;
    inode_disk->index_level = 1;
  }

  while (num_of_sectors > 0 && current_index < NUMBER_INDIRECT_BLOCKS) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS){
      max_iterator = NUMBER_INDIRECT_POINTERS;
    } else {
      max_iterator = num_of_sectors;
    }

    success &= inode_allocate_indirect_sectors(&inode_disk->indirect_pointers[current_index], max_iterator, 0);
    inode_disk->current_index = current_index;
    inode_disk->indirect_index = max_iterator;

    num_of_sectors -= max_iterator;
    current_index += 1;
  }

  /* still stuff to write after indirect blocks filled */
  if (current_index == NUMBER_INDIRECT_BLOCKS && num_of_sectors > 0){
    current_index = 0;
    indirect_index = 0;
    inode_disk->current_index = current_index;
    inode_disk->indirect_index = indirect_index;
    inode_disk->index_level = 2;
  }

  if (num_of_sectors > 0) {
    inode_disk->double_indirect_index = num_of_sectors % NUMBER_INDIRECT_POINTERS;
    inode_disk->indirect_index = num_of_sectors / NUMBER_INDIRECT_POINTERS;
    success &= inode_allocate_double_indirect_sectors(&inode_disk->double_indirect_pointers[current_index], num_of_sectors, 0 , 0);
  }

  /* index has to be at the same location as stored on inode_disk */
  //ASSERT(current_index = inode_disk->current_index);

  return success;
}


bool
inode_allocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator, size_t index_offset)
{
  bool success = true;
  size_t indirect_iterator = index_offset;
  struct indirect_block indirect_block;

  uint8_t zero_sector[BLOCK_SECTOR_SIZE]; 
  int zero_iterator = 0;
  for (zero_iterator = 0; zero_iterator < BLOCK_SECTOR_SIZE; zero_iterator++){
    zero_sector[zero_iterator] = 0;
  }

  if (index_offset == 0){
    success &= free_map_allocate (1, sectors);
  } else {
    block_read(fs_device, *sectors, &indirect_block);
  }

  for (indirect_iterator = index_offset; indirect_iterator < max_iterator; indirect_iterator++) {
    success &= free_map_allocate (1, &indirect_block.block_pointers[indirect_iterator]);
    block_write(fs_device, indirect_block.block_pointers[indirect_iterator], zero_sector);
  }

  block_write(fs_device, *sectors, &indirect_block);

  return success;
}


/* ONLY SUPPORT FOR ONE DOUBLE INDIRECT SECTOR! */
bool
inode_allocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors, size_t start_index_indirect, size_t  start_index_double_indirect)
{
  ASSERT(num_of_sectors > 0);

  bool success = true;
  // TODO: has to be set via parameter 
  size_t indirect_iterator = start_index_indirect;
  struct indirect_block double_indirect_block;

  uint8_t zero_sector[BLOCK_SECTOR_SIZE]; 
  int zero_iterator = 0;
  for (zero_iterator = 0; zero_iterator < BLOCK_SECTOR_SIZE; zero_iterator++){
    zero_sector[zero_iterator] = 0;
  }

  if (start_index_indirect == 0 && start_index_double_indirect == 0){
    success &= free_map_allocate (1, sectors);
  } else {
    block_read(fs_device, *sectors, &double_indirect_block);
  }

  int counter = 0;


  while (number_of_sectors > 0) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_sectors;

    //TODO: check if 0 as start is always correct
    if (counter == 0){
      inode_allocate_indirect_sectors(&double_indirect_block.block_pointers[indirect_iterator], max_iterator, start_index_double_indirect); 
    } else {
      inode_allocate_indirect_sectors(&double_indirect_block.block_pointers[indirect_iterator], max_iterator, 0); 
    }

    num_of_sectors -= max_iterator;
    indirect_iterator += 1;
    counter += 1;
    if (indirect_iterator > NUMBER_INDIRECT_POINTERS){
      printf("DEBUG: double indirect is getting overfilled!\n");
      break;
    }
  }

  block_write(fs_device, *sectors, &double_indirect_block);

  return success;
}


bool
debug_verify_sector(block_sector_t sector){
  return verify_sector(fs_device, sector);
}


/* verifies all sectors which are allocated in this inode (only supports direct / indirect) */
bool
debug_verify_inode(struct inode *inode, struct inode_disk *inode_disk)
{
  off_t length;
  block_sector_t *direct_pointers;
  block_sector_t *indirect_pointers;
  block_sector_t *double_indirect_pointers;
  if (inode != NULL){
    length = inode->data_length;
    direct_pointers = inode->direct_pointers;
    indirect_pointers = inode->indirect_pointers;
    double_indirect_pointers = inode->double_indirect_pointers;
  } else {
    length = inode_disk->length;
    direct_pointers = inode->direct_pointers;
    indirect_pointers = inode->indirect_pointers;
    double_indirect_pointers = inode->double_indirect_pointers;
  }
  off_t sectors_to_check = number_of_sectors(length);

  off_t current_index = 0;
  off_t indirect_index = 0;
  off_t double_indirect_index = 0;

  bool success = true;

  /* verification of all direct blocks */
  while (sectors_to_check > 0 && current_index < NUMBER_DIRECT_BLOCKS){
    bool check = debug_verify_sector(direct_pointers[current_index]);
    success &= check;
    if (!check)
      printf("     DEBUG: fail at: DIRECT: %u %u \n", current_index, indirect_index);
    current_index += 1;
  }

  current_index = 0;

  /* verification of all indirect blocks */
  while (sectors_to_check > 0 && current_index < NUMBER_INDIRECT_BLOCKS) {
    indirect_index = 0;
    struct indirect_block indirect_block;
    block_read(fs_device, indirect_pointers[current_index], &indirect_block);
    while (sectors_to_check > 0 && indirect_index < NUMBER_INDIRECT_POINTERS) {
      bool check = debug_verify_sector(indirect_block.block_pointers[indirect_index]);
      success &= check;
      if (!check)
        printf("     DEBUG: fail at: INDIRECT: %u %u \n", current_index, indirect_index);
      indirect_index += 1;
    }
    current_index += 1;
  }
  return success;
}



/* TODO remember to set length after every call to inode_grow */
bool
inode_grow(struct inode *inode, struct inode_disk *inode_disk, off_t size, off_t offset)
{
  // TODO: assert lock is already hold
  //printf("DEBUG: call grow with size: %i, and offset: %i\n", size, offset);
  uint8_t zero_sector[BLOCK_SECTOR_SIZE]; 
  int zero_iterator = 0;
  for (zero_iterator = 0; zero_iterator < BLOCK_SECTOR_SIZE; zero_iterator++){
    zero_sector[zero_iterator] = 0;
  }

  off_t length;
  int index_level;
  off_t current_index;
  off_t indirect_index;
  off_t double_indirect_index;
  block_sector_t *direct_pointers;
  block_sector_t *indirect_pointers;
  block_sector_t *double_indirect_pointers;


  if (inode != NULL) {
    length = inode->data_length;
    index_level = inode->index_level;
    direct_pointers = inode->direct_pointers;
    indirect_pointers = inode->indirect_pointers;
    double_indirect_pointers = inode->double_indirect_pointers;
    current_index = inode->current_index;
    indirect_index = inode->indirect_index;
    double_indirect_index = inode->double_indirect_index;
  } else {
    length = inode_disk->length;
    index_level = inode_disk->index_level;
    direct_pointers = inode_disk->direct_pointers;
    indirect_pointers = inode_disk->indirect_pointers;
    double_indirect_pointers = inode_disk->double_indirect_pointers;
    current_index = inode_disk->current_index;
    indirect_index = inode_disk->indirect_index;
    double_indirect_index = inode_disk->double_indirect_index;
  }

  //printf("DEBUG: call grow with initial size: %i\n", length);

  size_t num_of_used_sectors = number_of_sectors(length);

  off_t new_size = size + offset;

  size_t num_of_sectors_after = number_of_sectors(new_size);

  size_t num_of_add_sectors = num_of_sectors_after - num_of_used_sectors;

  if (num_of_add_sectors == 0)
    return true;

  bool success = true;

  if (index_level == 0){
    while (num_of_add_sectors > 0 && current_index < NUMBER_DIRECT_BLOCKS) {
      success &= free_map_allocate (1, &direct_pointers[current_index]);
      block_write(fs_device, direct_pointers[current_index], zero_sector);
      num_of_add_sectors -= 1;
      current_index += 1;
      num_of_used_sectors += 1;
      length += BLOCK_SECTOR_SIZE;
    }

    if (num_of_add_sectors > 0 && current_index == NUMBER_DIRECT_BLOCKS){
      index_level = 1;
      current_index = 0;
    }
  }

  bool first_iter = true;
  if (index_level == 1){
    while (num_of_add_sectors > 0 && current_index < NUMBER_INDIRECT_BLOCKS) {
      size_t max_iterator = 0;
      if (!first_iter)
        indirect_index = 0;
      if (num_of_add_sectors > NUMBER_INDIRECT_POINTERS)
        max_iterator = NUMBER_INDIRECT_POINTERS;
      else
        max_iterator = num_of_add_sectors;
      
      success &= inode_allocate_indirect_sectors(&indirect_pointers[current_index], max_iterator, indirect_index);

      first_iter = false;
      num_of_add_sectors -= max_iterator;
      length += (max_iterator * BLOCK_SECTOR_SIZE);
      current_index += 1;
      indirect_index = max_iterator;
    }

    if (num_of_add_sectors > 0 && current_index == NUMBER_INDIRECT_BLOCKS && indirect_index == NUMBER_INDIRECT_POINTERS){
      index_level = 2;
      current_index = 0;
      indirect_index = 0;
    }
  }

  if (index_level == 2){
    if (num_of_add_sectors > 0){
      //printf("DEBUG: double indirect Grow start \n");
      inode_allocate_double_indirect_sectors(&double_indirect_pointers[current_index], num_of_add_sectors, indirect_index, double_indirect_index);
      length += num_of_add_sectors * BLOCK_SECTOR_SIZE;
      //printf("DEBUG: double indirect Grow end \n");
    }
  }

  ASSERT(new_size==length);

  /* updating struct values */
  if (inode != NULL) {
    inode->data_length = length;
    inode->index_level = index_level;
    inode->current_index = current_index;
    inode->indirect_index = indirect_index;
    inode->double_indirect_index = double_indirect_index;
  } else {
    inode_disk->length = length;
    inode_disk->index_level = index_level;
    inode_disk->current_index = current_index;
    inode_disk->indirect_index = indirect_index;
    inode_disk->double_indirect_index = double_indirect_index;
  }

  //printf("DEBUG: grow end\n");
  return success;
}


void
inode_deallocate (struct inode *inode)
{
  off_t length = inode->data_length;
  int index_level = inode->index_level;
  block_sector_t *direct_pointers = inode->direct_pointers;
  block_sector_t *indirect_pointers = inode->indirect_pointers;
  block_sector_t *double_indirect_pointers = inode->double_indirect_pointers;
  off_t current_index = inode->current_index;
  off_t indirect_index = inode->indirect_index;
  off_t double_indirect_index = inode->double_indirect_index;

  size_t num_of_sectors = number_of_sectors(length);

  if (index_level > 0) {
    /* free all direct ones */
    int iter = 0;
    while (iter < NUMBER_DIRECT_BLOCKS) {
      free_map_release (direct_pointers[current_index], 1);
      num_of_sectors -= 1;
      iter += 1;
    }
  } else {
    /* free all allocated direct ones */
    int iter = 0;
    while (iter <= current_index) {
      free_map_release (direct_pointers[current_index], 1);
      num_of_sectors -= 1;
      iter += 1;
    }
  }

  if (index_level > 1) {
    /* free all indirect ones */
    int iter = 0;
    while (iter < NUMBER_DIRECT_BLOCKS) {
      inode_deallocate_indirect_sectors(&indirect_pointers[iter], NUMBER_INDIRECT_POINTERS);
      num_of_sectors -= NUMBER_INDIRECT_POINTERS;
      iter += 1;
    }
  } else {
    /* free all allocated indirect ones */
    int iter = 0;
    while (iter <= current_index) {
      size_t max_iterator = 0;
      if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
        max_iterator = NUMBER_INDIRECT_POINTERS;
      else
        max_iterator = num_of_sectors;
      inode_deallocate_indirect_sectors(&indirect_pointers[current_index], max_iterator);
      num_of_sectors -= max_iterator;
      iter += 1;
    }
  }

  if (index_level == 2 && num_of_sectors > 0) {
    inode_deallocate_double_indirect_sectors(&double_indirect_pointers[current_index], num_of_sectors);
  }
}


void
inode_deallocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator)
{
  size_t iterator = 0;
  struct indirect_block indirect_block;

  block_read(fs_device, *sectors, &indirect_block);

  for (iterator = 0; iterator < max_iterator; iterator++) {
    free_map_release (indirect_block.block_pointers[iterator], 1);
  }

  free_map_release (*sectors, 1);
}

void
inode_deallocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors)
{
  size_t iterator = 0;
  struct indirect_block double_indirect_block;

  block_read(fs_device, *sectors, &double_indirect_block);

  while (num_of_sectors > 0) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_sectors;

    inode_deallocate_indirect_sectors(&double_indirect_block.block_pointers[iterator], max_iterator);

    num_of_sectors -= max_iterator;
    iterator += 1;
  }

  free_map_release (*sectors, 1);
}



/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
size_t
number_of_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}


/* returns the number of direct sectors */
size_t
number_of_direct_sectors (off_t size)
{
  size_t sectors = number_of_sectors(size);

  if (sectors > NUMBER_DIRECT_BLOCKS)
    return NUMBER_DIRECT_BLOCKS;
  else
    return sectors;
}


/* returns the current_index of the currently used indirect array (which may be partly empty) */
size_t
number_of_indirect_sectors (off_t size)
{
  if (size < NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
    return 0;

  size = size - NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE;

  size_t number_indirect_sectors = size / (BLOCK_SECTOR_SIZE * NUMBER_INDIRECT_POINTERS);

  if (number_indirect_sectors > NUMBER_INDIRECT_BLOCKS)
    return NUMBER_INDIRECT_BLOCKS;
  else
    return number_indirect_sectors;
}

// assumes there is only 1 double indirect sector
size_t compute_index_double_indirect (off_t size){
  if (size < NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE)
    return 0;

  size = size - NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE;

  return DIV_ROUND_UP (size, NUMBER_INDIRECT_POINTERS);
}


size_t compute_index_indirect_from_double (off_t size){
  size_t index = compute_index_double_indirect (size);
  size = size - NUMBER_DIRECT_BLOCKS + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS + index * NUMBER_INDIRECT_POINTERS * NUMBER_INDIRECT_POINTERS;

  return number_of_sectors(size);

}


/* ONLY SUPPORT FOR ONE double indirect sector !!! */
size_t
number_of_double_indirect_sectors (off_t size)
{
  // TODO assert for over maximum size

  if (size < NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE)
    return 0;

  return 1;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos >= inode->data_length)
    return -1;

  uint32_t index;

  /* check inside direct blocks */
  if (pos < DIRECT_BLOCKS_END) {
    index = (pos / BLOCK_SECTOR_SIZE);
    return inode->direct_pointers[index];
  }

  /* check inside indirect blocks */
  if (pos < INDIRECT_BLOCKS_END) {
    return byte_to_sector_indirect(inode, pos);
  }

  /* check inside double indirect blocks */
  if (pos < DOUBLE_INDIRECT_BLOCKS_END) {
    return byte_to_sector_double_indirect(inode, pos);
  } else {
    return -1;
  }
}


/* return the block sector from indirect block */
static block_sector_t
byte_to_sector_indirect (const struct inode *inode, off_t pos)
{
  struct indirect_block temp_indirect_block;
  uint32_t index;

  off_t indirect_pos = pos - (NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE);

  off_t current_index = (indirect_pos / BLOCK_SECTOR_SIZE) / NUMBER_INDIRECT_POINTERS;
  off_t indirect_index = (indirect_pos / BLOCK_SECTOR_SIZE) % NUMBER_INDIRECT_POINTERS;

  /* read the indirect block into temporary representation */
  block_read(fs_device, inode->indirect_pointers[current_index], &temp_indirect_block);

  return temp_indirect_block.block_pointers[indirect_index];
}


/* return the block sector from double indirect block */
static block_sector_t
byte_to_sector_double_indirect (const struct inode *inode, off_t pos)
{
  struct indirect_block temp_indirect_block;
  struct indirect_block temp_double_indirect_block;

  off_t double_indirect_pos = pos - (NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) - (NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE);

  off_t current_index = 0;
  off_t indirect_index = (double_indirect_pos / BLOCK_SECTOR_SIZE) / NUMBER_INDIRECT_POINTERS;
  off_t double_indirect_index = (double_indirect_pos / BLOCK_SECTOR_SIZE) % NUMBER_INDIRECT_POINTERS;

  /* read the double indirect block into temporary representation */
  block_read(fs_device, inode->double_indirect_pointers[current_index], &temp_double_indirect_block);

  /* read the correct indirect block into temporary representation */
  block_read(fs_device, temp_double_indirect_block.block_pointers[indirect_index], &temp_indirect_block);

  return temp_indirect_block.block_pointers[double_indirect_pos];
}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = 0;
      disk_inode->index_level = 0;
      disk_inode->current_index = 0;
      disk_inode->indirect_index = 0;
      disk_inode->double_indirect_index = 0;
      inode_grow(NULL, disk_inode, length, 0);
      if (length > MAX_FILESIZE)
        disk_inode->length = MAX_FILESIZE;
      else
        disk_inode->length = length;

      disk_inode->magic = INODE_MAGIC;

      if (inode_allocate (disk_inode))
        {
          block_write (fs_device, sector, disk_inode);
          success = true; 
          //debug_verify_inode(NULL, disk_inode);
        } 
      else
        {
          printf("DEBUG: inode_allocate failed!\n");

        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->index_level = 0;
  inode->current_index = 0;
  inode->indirect_index = 0;
  inode->double_indirect_index = 0;
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_extend_lock);

  /* read inode from disk into disk_data */
  struct inode_disk disk_data;
  block_read (fs_device, inode->sector, &disk_data);
  inode->data_length = disk_data.length;
  inode->index_level = disk_data.index_level;
  inode->current_index = disk_data.current_index;
  inode->indirect_index = disk_data.indirect_index;
  inode->double_indirect_index = disk_data.double_indirect_index;

  // TODO make this stacis somewhere
  int bytes_per_block_sector = sizeof(block_sector_t);

  /* copy the entire segment of block_points from disk_data into inode */
  memcpy(&inode->direct_pointers, &disk_data.direct_pointers, NUMBER_DIRECT_BLOCKS * bytes_per_block_sector);
  memcpy(&inode->indirect_pointers, &disk_data.indirect_pointers, NUMBER_INDIRECT_BLOCKS * bytes_per_block_sector);
  memcpy(&inode->double_indirect_pointers, &disk_data.double_indirect_pointers, NUMBER_DOUBLE_INDIRECT_BLOCKS * bytes_per_block_sector);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_deallocate(inode);
          // TODO: think about removing entries from cache here
        }
      else
        { 
          /* write back to disk */
          struct inode_disk inode_disk;
          inode_disk.length = inode->data_length;
          inode_disk.index_level = inode->index_level;
          inode_disk.current_index = inode->current_index;
          inode_disk.indirect_index = inode->indirect_index;
          inode_disk.double_indirect_index = inode->double_indirect_index;
          inode_disk.directory = inode->directory;
          inode_disk.magic = INODE_MAGIC;
          memcpy(&inode_disk.direct_pointers, &inode->direct_pointers, NUMBER_DIRECT_BLOCKS * sizeof(block_sector_t));
          memcpy(&inode_disk.indirect_pointers, &inode->indirect_pointers, NUMBER_INDIRECT_BLOCKS * sizeof(block_sector_t));
          memcpy(&inode_disk.double_indirect_pointers, &inode->double_indirect_pointers, NUMBER_DOUBLE_INDIRECT_BLOCKS * sizeof(block_sector_t));
          block_write(fs_device, inode->sector, &inode_disk);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
  // TODO: think about removing entries from cache here
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  /* cant read past size of inode */
  if (inode_length(inode) <= offset) {
    return 0;
  }

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      filesys_cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{

  // TODO: insert inode_grow
  if (size + offset > inode->data_length){
    //TODO: require lock
    lock_acquire(&inode->inode_extend_lock);
    inode_grow (inode, NULL, size, offset);
    inode->data_length = size + offset;
    lock_release(&inode->inode_extend_lock);
    //debug_verify_inode(inode, NULL);
    //TODO: release lock
  }

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      filesys_cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  return inode->data_length;
}
