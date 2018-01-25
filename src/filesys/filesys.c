#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "filesys/off_t.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();

  free_map_init ();

  filesys_cache_init();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  filesys_cache_writeback();

  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_directory) 
{
  block_sector_t inode_sector = 0;
  char* path = dir_get_path(name);
  char* filename = dir_get_file_name(name);
  if (filename == NULL)
    return false;
  struct dir *dir = NULL;
  if (path == NULL){
    //TODO: open current working directory
    dir = dir_reopen(thread_current()->current_working_dir);
    if (dir == NULL)
      return false;
  }
  dir = dir_open_path(path);
  /* TODO: create directory at this point?? */
  /*TODO: add self as first entry in directory in case of directory */
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_directory)
                  && dir_add (dir, filename, inode_sector, is_directory));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char* path = dir_get_path(name);
  char* filename = dir_get_file_name(name);
  if (filename == NULL)
    return NULL;
  struct dir *dir = NULL;
  if (path == NULL){
    //TODO: open current working directory
    dir = dir_reopen(thread_current()->current_working_dir);
    if (dir == NULL)
      return NULL;
  }
  dir = dir_open_path(path);
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char* path = dir_get_path(name);
  char* filename = dir_get_file_name(name);
  if (filename == NULL)
    return false;
  struct dir *dir = NULL;
  if (path == NULL){
    //TODO: open current working directory
    dir = dir_reopen(thread_current()->current_working_dir);
    if (dir == NULL)
      return false;
  }
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* changes the current working directory of the running thread
  returns true on success and false if the new working directory
  does not exists */
bool filesys_chdir (const char *name)
{
  if (name == NULL)
    return false;
  struct dir *new_cwd = dir_open_path(name);

  if(dir == NULL)
    return false;

  struct thread *current_thread = thread_current();
  struct dir *old_cwd = current_thread->current_working_dir;
  if (old_cwd != NULL)
    dir_close(old_cwd);

  current_thread->current_working_dir = new_cwd;

  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
