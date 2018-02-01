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
#include "threads/thread.h"

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

  if (format){
    do_format ();
  }

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();

  filesys_cache_writeback();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool directory) 
{
  if (name == NULL)
    return false;
  block_sector_t inode_sector = 0;
  int name_length = strlen(name);


  /* parses file name and path from name */
  char path [(name_length + 1)];
  char file_name [(name_length + 1)];

  int clear_iterator = 0;
  for (clear_iterator = 0; clear_iterator <= name_length; 
    clear_iterator += 1){
    path[clear_iterator] = '\0';
    file_name[clear_iterator] = '\0';
  }
  parse_string_to_path_file(name, path, file_name);

  /* check if file name is valid */
  if (file_name == NULL || strlen(file_name) == 0)
    return false;
  
  struct dir *dir = NULL;
  if (strlen(file_name) > NAME_MAX)
    return false;
  
  /* open directory */
  dir = dir_open_path(path);

  /* check if directory is valid and create new inode with flag directory
  which indicates if inode is used as directory */
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, directory)
                  && dir_add (dir, file_name, inode_sector, directory));

  /* if dir_add or inode_create failed release sector again */
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
  /* check if name is valid */
  if (name == NULL)
    return NULL;

  int name_length = strlen(name);

  if (name_length == 0)
     return NULL;

  /* extract path and file name */
  char path [(name_length + 1)];
  char file_name [(name_length + 1)];
  int clear_iterator = 0;
  for (clear_iterator = 0; clear_iterator <= name_length;
   clear_iterator += 1){
    path[clear_iterator] = '\0';
    file_name[clear_iterator] = '\0';

  }
  parse_string_to_path_file(name, path, file_name);

  struct dir *dir = NULL;
  
  /* open directory */
  dir = dir_open_path(path);
  struct inode *inode = NULL;

  /* directory cannot be read, error */
  if (dir == NULL)
    return NULL;
  

  if (strlen(file_name) == 0){
    /* case of no file_name -> return directory */
    inode = dir_get_inode(dir);
  } else {
    /* case of file_name -> return search result for filename instead */
    dir_lookup (dir, file_name, &inode);
    dir_close (dir);
  }

  /* double check if inode has been removed already */
  if (inode == NULL || inode_is_removed(inode)) {
   return NULL;
  }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false;
  if (name == NULL)
    return false;

  int name_length = strlen(name);

  /* extract path and file name from name */
  char path [(name_length + 1)];
  char file_name [(name_length + 1)];
  int clear_iterator = 0;
  for (clear_iterator = 0; clear_iterator <= name_length;
   clear_iterator += 1){
    path[clear_iterator] = '\0';
    file_name[clear_iterator] = '\0';

  }
  parse_string_to_path_file(name, path, file_name);

  /* check if file name is valid */
  if (file_name == NULL)
    return false;

  struct dir *dir = NULL;
  /* open directory */
  dir = dir_open_path(path);

  /* remove inode from directory */
  if (dir != NULL)
    success = dir_remove(dir, file_name);

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

  if(new_cwd == NULL)
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
  struct dir *root_dir = dir_create_root (ROOT_DIR_SECTOR, 16);
  if (root_dir == NULL)
    PANIC ("root directory creation failed");
  inode_writeback(dir_get_inode(root_dir));
  free_map_close ();
  printf ("done.\n");
}
