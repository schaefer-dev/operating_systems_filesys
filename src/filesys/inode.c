#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

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
bool inode_allocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors, size_t start_index_double_indirect, size_t  start_index_indirect);

void inode_deallocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator);
void inode_deallocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors);

bool inode_grow(struct inode *inode, off_t size, off_t offset);

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
  // TODO 
  size_t num_of_sectors = number_of_sectors(inode_disk->length);
  size_t num_direct_sectors = number_of_direct_sectors(inode_disk->length);
  //UNUSED NOW: size_t num_indirect_sectors = number_of_indirect_sectors(inode_disk->length);
  size_t num_double_indirect_sectors = number_of_double_indirect_sectors(inode_disk->length);

  // TODO make sure this is actually initialized with zero bytes
  char zero_sector[BLOCK_SECTOR_SIZE]; 
  int current_index = 0;
  bool success = true;

  while (num_direct_sectors > 0 && current_index < INDEX_INDIRECT_BLOCKS) {
    success &= free_map_allocate (1, &inode_disk->block_pointers[current_index]);
    block_write(fs_device, inode_disk->block_pointers[current_index], zero_sector);
    num_direct_sectors -= 1;
    num_of_sectors -= 1;
    current_index += 1;
  }

  while (num_of_sectors > 0 && current_index < INDEX_DOUBLE_INDIRECT_BLOCKS) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_sectors;

      success &= inode_allocate_indirect_sectors(&inode_disk->block_pointers[current_index], max_iterator, 0);

      num_of_sectors -= max_iterator;
      current_index += 1;
  }

  if (num_double_indirect_sectors > 0) {
    success &= inode_allocate_double_indirect_sectors(inode_disk->block_pointers[current_index], num_of_sectors, 0 , 0);
  }

  return success;
}


/* TODO maybe refactor this because the call in allocate is not very nice with condition checking */
bool
inode_allocate_indirect_sectors(block_sector_t *sectors, size_t max_iterator, size_t index_offset)
{
  bool success = true;
  size_t iterator = index_offset;
  struct indirect_block indirect_block;

  char zero_sector[BLOCK_SECTOR_SIZE];

  success &= free_map_allocate (1, sectors);

  for (iterator = index_offset; iterator < max_iterator; iterator++) {
    success &= free_map_allocate (1, &indirect_block.block_pointers[iterator]);
    block_write(fs_device, indirect_block.block_pointers[iterator], zero_sector);
  }

  block_write(fs_device, *sectors, &indirect_block);

  return success;
}


/* ONLY SUPPORT FOR ONE DOUBLE INDIRECT SECTOR! */
/* TODO might still be broken!!! */
bool
inode_allocate_double_indirect_sectors(block_sector_t *sectors, size_t num_of_sectors, size_t start_index_double_indirect, size_t  start_index_indirect)
{
  ASSERT(num_of_sectors > 0);

  bool success = true;
  // TODO: has to be set via parameter 
  size_t iterator = start_index_double_indirect;
  struct indirect_block double_indirect_block;

  char zero_sector[BLOCK_SECTOR_SIZE];

  success &= free_map_allocate (1, sectors);

  int counter = 0;


  // TODO catch case for to large
  while (number_of_sectors > 0) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_sectors;

    //TODO: check if 0 as start is always correct
    if (counter == 0){
      inode_allocate_indirect_sectors(&double_indirect_block.block_pointers[iterator], max_iterator, start_index_indirect); 
    } else {
      inode_allocate_indirect_sectors(&double_indirect_block.block_pointers[iterator], max_iterator, 0); 
    }
    num_of_sectors -= max_iterator;

    iterator += 1;
    counter += 1;
  }

  block_write(fs_device, *sectors, &double_indirect_block);

  return success;
}

bool inode_grow(struct inode *inode, off_t size, off_t offset){
  // TODO: assert lock is already hold 
  char zero_sector[BLOCK_SECTOR_SIZE];
  off_t length = inode->data_length;
  // compute how many sectors are already used
  size_t num_of_used_sectors = number_of_sectors(length);

  size_t num_double_indirect_sectors = number_of_double_indirect_sectors(length);

  off_t new_size = size+offset;

  size_t num_of_sectors = number_of_sectors(new_size);

  size_t num_of_add_sectors = num_of_sectors - num_of_used_sectors;

  int current_index = 0;

  size_t index_offset = 0;

  bool success = true;

  if (num_of_used_sectors < INDEX_INDIRECT_BLOCKS){
    // start in direct blocks
    current_index = num_of_used_sectors;
    index_offset = 0;

    while (num_of_add_sectors > 0 && current_index < INDEX_INDIRECT_BLOCKS) {
      // TODO: check if it is correct to use inode instead of inode_disk
      success &= free_map_allocate (1, &inode->block_pointers[current_index]);
      block_write(fs_device, inode->block_pointers[current_index], zero_sector);
      num_of_add_sectors -= 1;
      current_index += 1;
      num_of_used_sectors += 1;
      length += BLOCK_SECTOR_SIZE;
    }
  }

  if (num_of_used_sectors >= INDEX_INDIRECT_BLOCKS && num_double_indirect_sectors == 0){
    // we have to calculate the indirect sector and the offset within the sector
    size_t start_sector = number_of_indirect_sectors(length);
    if (start_sector != 0){
      current_index = start_sector;
    } 
    index_offset = num_of_used_sectors - (NUMBER_DIRECT_BLOCKS + start_sector * NUMBER_INDIRECT_POINTERS);
    // start while-loop index_offset only in first indirect sector needed
    int indirect_iterator = 0;
    while (num_of_add_sectors > 0 && current_index < INDEX_DOUBLE_INDIRECT_BLOCKS) {
    size_t max_iterator = 0;
    if (num_of_add_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_add_sectors;

      if (indirect_iterator == 0){
        success &= inode_allocate_indirect_sectors(&inode->block_pointers[current_index], max_iterator, index_offset);
      } else {
        success &= inode_allocate_indirect_sectors(&inode->block_pointers[current_index], max_iterator, 0);
      }
      
      num_of_add_sectors -= max_iterator;
      length += (max_iterator * BLOCK_SECTOR_SIZE);
      current_index += 1;
      indirect_iterator += 1;
    }
  }

  size_t index_double_indirect = compute_index_double_indirect (length);
  size_t index_indirect = compute_index_indirect_from_double (length);

  if (num_of_add_sectors > 0){
    inode_allocate_double_indirect_sectors(inode->block_pointers[current_index], num_of_add_sectors, index_double_indirect , index_indirect);
    length += num_of_add_sectors * BLOCK_SECTOR_SIZE;
  }

  inode->data_length = length;
  return success;
}


void
inode_deallocate (struct inode *inode)
{
  size_t num_of_sectors = number_of_sectors(inode->data_length);
  size_t num_direct_sectors = number_of_direct_sectors(inode->data_length);
  size_t num_double_indirect_sectors = number_of_double_indirect_sectors(inode->data_length);

  int current_index = 0;

  while (num_direct_sectors > 0 && current_index < INDEX_INDIRECT_BLOCKS) {
    free_map_release (inode->block_pointers[current_index], 1);
    num_direct_sectors -= 1;
    num_of_sectors -= 1;
    current_index += 1;
  }

  while (num_of_sectors > 0 && current_index < INDEX_DOUBLE_INDIRECT_BLOCKS) {
    size_t max_iterator = 0;
    if (num_of_sectors > NUMBER_INDIRECT_POINTERS)
      max_iterator = NUMBER_INDIRECT_POINTERS;
    else
      max_iterator = num_of_sectors;

    inode_deallocate_indirect_sectors(&inode->block_pointers[current_index], max_iterator);

    num_of_sectors -= max_iterator;
    current_index += 1;
  }

  if (num_of_sectors > 0) {
    inode_deallocate_double_indirect_sectors(&inode->block_pointers[current_index], num_of_sectors);
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


/* already support for multiple indirect sectors */
size_t
number_of_indirect_sectors (off_t size)
{
  if (size < NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
    return 0;

  size = size - NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE;

  size_t number_indirect_sectors = DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE * NUMBER_INDIRECT_POINTERS);

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
    return inode->block_pointers[index];
  }

  /* check inside indirect blocks */
  if (pos < INDIRECT_BLOCKS_END) {
    return byte_to_sector_indirect(inode, pos);
  }

  /* check inside double indirect blocks */
  if (pos < DOUBLE_INDIRECT_BLOCKS_END) {
    return byte_to_sector_double_indirect(inode, pos);
  } else {
    // TODO -1 the correct illegal sector?
    return -1;
  }
}


/* return the block sector from indirect block */
static block_sector_t
byte_to_sector_indirect (const struct inode *inode, off_t pos)
{
  uint32_t temporary_indirect_block[NUMBER_INDIRECT_POINTERS];
  uint32_t index;

  off_t indirect_pos = pos - (NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE);
  index = INDEX_INDIRECT_BLOCKS + (indirect_pos / (NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE));

  /* read the indirect block into temporary representation */
  block_read(fs_device, inode->block_pointers[index], &temporary_indirect_block);

  /* set indirect_pos to array index */
  indirect_pos = indirect_pos % (NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE);

  return temporary_indirect_block[indirect_pos / BLOCK_SECTOR_SIZE];
}


/* return the block sector from double indirect block */
static block_sector_t
byte_to_sector_double_indirect (const struct inode *inode, off_t pos)
{
  uint32_t temporary_double_indirect_block[NUMBER_INDIRECT_POINTERS];
  uint32_t temporary_indirect_block[NUMBER_INDIRECT_POINTERS];

  /* read the double indirect block into temporary representation */
  block_read(fs_device, inode->block_pointers[INDEX_DOUBLE_INDIRECT_BLOCKS], &temporary_double_indirect_block);

  off_t double_indirect_pos = pos - (NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
    - (NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE);

  uint32_t first_index = double_indirect_pos / (BLOCK_SECTOR_SIZE * NUMBER_INDIRECT_POINTERS);

  /* read the correct indirect block into temporary representation */
  block_read(fs_device, temporary_double_indirect_block[first_index], &temporary_indirect_block);

  /* set indirect_pos to array index */
  off_t indirect_pos = double_indirect_pos % (NUMBER_INDIRECT_POINTERS * BLOCK_SECTOR_SIZE);

  return temporary_indirect_block[indirect_pos / BLOCK_SECTOR_SIZE];
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
      if (length > MAX_FILESIZE)
        disk_inode->length = MAX_FILESIZE;
      else
        disk_inode->length = length;

      disk_inode->magic = INODE_MAGIC;

      if (inode_allocate (disk_inode))
        {
          block_write (fs_device, sector, disk_inode);
          success = true; 
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
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_extend_lock);

  /* read inode from disk into disk_data */
  struct inode_disk disk_data;
  block_read (fs_device, inode->sector, &disk_data);
  inode->data_length = disk_data.length;

  // TODO make this stacis somewhere
  int bytes_per_block_sector = sizeof(block_sector_t);

  /* copy the entire segment of block_points from disk_data into inode */
  memcpy(&inode->block_pointers, &disk_data.block_pointers, NUMBER_INODE_POINTERS * bytes_per_block_sector);

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
        }
      else
        { 
          /* write back to disk */
          /*
          struct inode_disk inode_disk;
          inode_disk.length = inode->data_length;
          inode_disk.magic = INODE_MAGIC;
          inode_disk.directory = inode->directory;
          memcpy(&inode_disk.block_pointers, &inode->block_pointers, NUMBER_INODE_POINTERS * sizeof(block_sector_t));
          block_write(fs_device, inode->sector, &inode_disk);
          */
          
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

      /*if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer. 
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy
          // into caller's buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      */
      
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
    inode_grow (inode, size, offset);
    lock_release(&inode->inode_extend_lock);
    //TODO: release lock
  }
  //
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // TODO inode_grow has to be called here depending on size/offset

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

      /*if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Write full sector directly to disk. 
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          // We need a bounce buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          // we're writing, then we need to read in the sector
          // first.  Otherwise we start with a sector of all zeros. 
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }
      */

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

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
