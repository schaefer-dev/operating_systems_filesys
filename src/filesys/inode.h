#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

/* 125 unused pointers.
   as many direct pointers as possible while still supporting 8MB File. 
   DIRECT   INDIRECT    DOUBLE-INDIRECT
   (123)  + (1 * 128) + (128 * 128 * 1) = 16.635 sectors

   512 bytes per sector -> 8.517.120 bytes */

/* MAX FILESIZE =
        ( NUMBER_DIRECT_BLOCKS
          + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS
          + NUMBER_DOUBLE_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * NUMBER_INDIRECT_POINTERS
        ) * BLOCK_SECTOR_SIZE   */


/* number of direct/indirect/double-indirect blocks */
#define NUMBER_DIRECT_BLOCKS 1
#define NUMBER_INDIRECT_BLOCKS 5
#define NUMBER_DOUBLE_INDIRECT_BLOCKS 1

#define NUMBER_UNUSED_BYTES (128 - 8 - NUMBER_DIRECT_BLOCKS - NUMBER_INDIRECT_BLOCKS - NUMBER_DOUBLE_INDIRECT_BLOCKS)

/* overall number of pointers in inode */
#define NUMBER_INODE_POINTERS (NUMBER_DIRECT_BLOCKS + NUMBER_INDIRECT_BLOCKS + NUMBER_DOUBLE_INDIRECT_BLOCKS)

/* number of pointers contained in a indirect block */
#define NUMBER_INDIRECT_POINTERS 128

#define DIRECT_BLOCKS_END (NUMBER_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)



#define MAX_FILESIZE ((NUMBER_DIRECT_BLOCKS + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS + NUMBER_DOUBLE_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * NUMBER_INDIRECT_POINTERS) * BLOCK_SECTOR_SIZE)
#define INDIRECT_BLOCKS_END ((NUMBER_DIRECT_BLOCKS + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS) * BLOCK_SECTOR_SIZE)
#define DOUBLE_INDIRECT_BLOCKS_END ((NUMBER_DIRECT_BLOCKS + NUMBER_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS + NUMBER_DOUBLE_INDIRECT_BLOCKS * NUMBER_INDIRECT_POINTERS * NUMBER_INDIRECT_POINTERS) * BLOCK_SECTOR_SIZE)



/* struct which describes indirect_block which contains NUMBER_INDIRECT_POINTERS
   amount of pointers */
struct indirect_block
  {
    block_sector_t block_pointers[NUMBER_INDIRECT_POINTERS];
  };


struct bitmap;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
 {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool directory;
    /* TODO initilized with 0, this might be an issue later */
    block_sector_t parent;
    unsigned index_level;               /* level 0 -> direct, 1->indirect, 2->double-indirect */
    off_t current_index;
    off_t indirect_index;
    off_t double_indirect_index;
    uint32_t unused[NUMBER_UNUSED_BYTES];                /* not used */
    /* pointers to blocks with file content: */
    block_sector_t direct_pointers[NUMBER_DIRECT_BLOCKS];               
    block_sector_t indirect_pointers[NUMBER_INDIRECT_BLOCKS];               
    block_sector_t double_indirect_pointers[NUMBER_DOUBLE_INDIRECT_BLOCKS];               
  };


/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t data_length;                  /* length of the file in bytes */
    bool directory;
    /* TODO initilized with 0, this might be an issue later */
    block_sector_t parent;

    unsigned index_level;               /* level 0 -> direct, 1->indirect, 2->double-indirect */
    off_t current_index;
    off_t indirect_index;
    off_t double_indirect_index;

    struct lock inode_extend_lock;
    /* pointers to blocks with file content: */
    block_sector_t direct_pointers[NUMBER_DIRECT_BLOCKS];               
    block_sector_t indirect_pointers[NUMBER_INDIRECT_BLOCKS];               
    block_sector_t double_indirect_pointers[NUMBER_DOUBLE_INDIRECT_BLOCKS];               
  };


void inode_init (void);

bool inode_allocate (struct inode_disk *inode_disk);
void inode_deallocate (struct inode *inode);

bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (struct inode *);
block_sector_t inode_parent (struct inode *);
bool inode_is_directory (struct inode *);
bool inode_is_removed (struct inode *);

#endif /* filesys/inode.h */
