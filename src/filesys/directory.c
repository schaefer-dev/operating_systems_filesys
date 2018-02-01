#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };


/*  IMPORTANT: Paths are not allowed to end with / in string!!! */
/*  TODO make sure to create and free path/file_name whenever this function is used! 
 *  they should be allocated with strlen(string) + 1 */
void
parse_string_to_path_file(const char *string, char *path, char *file_name)
{
    if (string == NULL || strlen(string) == 0)
      return;

    int string_length = strlen(string);

    // TODO think about +1 here
    char *temp = malloc(sizeof(char) * (string_length + 1));
    ASSERT(temp != NULL && string != NULL);
    memcpy(temp, string, sizeof(char) * (string_length + 1));

    /*  reference to continiously write into path string */
    ASSERT(path != NULL);
    char *path_writer = path;

    if (temp[0] == '/'){
      *path_writer = '/';
      path_writer += 1;
    }

    char *token = NULL;
    char *previous_token = NULL;
    char *pos;

    for (token = strtok_r(temp, "/", &pos); token != NULL; token = strtok_r(NULL, "/", &pos)) {
      if (previous_token != NULL){
        int token_length = strlen(previous_token);
        if (token_length > 0){
          memcpy (path_writer, previous_token, sizeof(char) * token_length);
          path_writer += token_length;
          *path_writer = '/';
          path_writer += 1;
        }
      }
      previous_token = token;
    }

    ASSERT(file_name != NULL);

    *path_writer = '\0';
    int previous_token_length = 0;
    if (previous_token != NULL){
      previous_token_length = strlen(previous_token);
      memcpy(file_name, previous_token, sizeof(char) * previous_token_length + 1);
    }

    free(temp);
}



struct dir*
dir_open_path(const char* path)
{ 
  //ASSERT(path != NULL);
  if (path == NULL || strlen(path) == 0) {
    if (thread_current()->current_working_dir == NULL){
      /* CWD of current_thread has not been set yet */
      return dir_open_root();		
    } else {
      /* return CWD if it was not marked as removed */
      struct dir *current_dir =  dir_reopen(thread_current()->current_working_dir);
      if (current_dir == NULL || dir_get_inode(current_dir) == NULL || inode_is_removed(dir_get_inode(current_dir)))
        return NULL;
      return current_dir;
    }
  }

  int name_length = strlen(path);

  struct dir *current_dir = NULL;

  char *temp = malloc(sizeof(char) * (name_length + 1));

  /* to make sure that last token is not null */
  strlcpy(temp, path, name_length + 1);

  if (*temp == '/'){
    /* absolute path */
    current_dir = dir_open_root();
    // TODO this should not be neccessary, trying to force open to work on '/'
    if (name_length == 1){
      //printf("DEBUG: parsing of just '/' returns\n");
      goto success;
    }
  } else {
    /* relative path */
    struct thread *current_thread = thread_current();
    if (current_thread->current_working_dir != NULL){
      current_dir = dir_reopen(current_thread->current_working_dir);
    } else {
      current_dir = dir_open_root();
    }
  }

  /* if current directory is removed, cannot open path */
  if (current_dir == NULL || dir_get_inode(current_dir) == NULL || inode_is_removed(dir_get_inode(current_dir)))
  {
    //printf("DEBUG: current_dir was initially already marked as removed\n");
    return NULL;
  }

  ASSERT(current_dir != NULL);

  /* change directory step by step based on path */
  char *token = "";
  char *pos;
  for (token = strtok_r(temp, "/", &pos); token != NULL; token = strtok_r(NULL, "/", &pos)) {
    int token_length = strlen(token);
    //printf("DEBUG: token_iter in dir opening: '%s'\n", token);
    if (token_length > 0){
      /* new special cases for parent .. */
      if (token_length == 2){
        if (token[0] == '.'  && token[1] == '.'){
          //printf("DEBUG: recognized '..'\n");
          struct dir *next_dir = dir_open_parent_dir(current_dir);
          dir_close(current_dir);
          current_dir = next_dir;
          continue;
        }
      }

      /* new special cases for self . */
      if (token_length == 1){
        if (token[0] == '.'){
          continue;
        }
      }

      struct inode *inode = NULL;
      if (!dir_lookup (current_dir, token, &inode)){
        goto invalid;
      }
      if (inode == NULL){
        goto invalid;
      }

      /* traversing illegal / removed directory not allowed */
      if (inode_is_removed(inode)){
        goto invalid;
      }

      dir_close(current_dir);
      current_dir = dir_open(inode);
    }
  }

  free(temp);

  if (current_dir == NULL || current_dir->inode == NULL || inode_is_removed(current_dir->inode))
  {
    dir_close(current_dir);
    //printf("DEBUG: result-current_dir was marked as removed\n");
    return NULL;
  }

 success:
  //printf("DEBUG: dir_open_path returned successful\n");
  return current_dir;

 invalid:
  dir_close(current_dir);
  free(temp);
  //printf("DEBUG: dir_open_path invalid\n");
  return NULL;
}


/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
struct dir*
dir_create_root (block_sector_t sector, size_t entry_cnt)
{
  if (!inode_create (sector, entry_cnt * sizeof (struct dir_entry), true))
    return NULL;

  struct inode *root_inode = inode_open (sector);
  inode_set_parent_to_inode(root_inode, root_inode);

  if (root_inode == NULL)
    return NULL;

  struct dir *root = dir_open (root_inode);

  if (root == NULL)
    return NULL;

  return root;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  struct inode *root_inode = inode_open (ROOT_DIR_SECTOR);
  struct dir *root = dir_open (root_inode);

  return root;
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* TODO replace all calls of lookup with dir_lookup!!! */
/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* handle '.' seperatly! */
  if (strlen(name) == 1){
    if (name[0] == '.'){
      *inode = dir->inode;
      return true;
    }
  }

  /* handle '..' seperatly! */
  if (strlen(name) == 2){
    if (name[0] == '.' && name[1] == '.'){
      *inode = inode_open(inode_parent(dir->inode));
      return true;
    }
  }

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */

//TODO: do the same for "."
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool directory)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  struct inode *directory_inode = dir_get_inode(dir);
  ASSERT (directory_inode != NULL);

  lock_acquire(&directory_inode->inode_directory_lock);

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* if directory we add the directory to the file(in this case a directory) */
  /* TODO: could be bad at this position race if inode not already created */
  if(directory){
    struct inode *child_inode = inode_open(inode_sector);
    inode_set_parent_to_inode(child_inode, dir_get_inode(dir));

    inode_close(child_inode);
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  off_t bytes_written = inode_write_at (dir->inode, &e, sizeof e, ofs);
  // TODO check that written bytes are okay!

  // TODO if parts of the struct are written here this might break the dir!!
  if((bytes_written) != (1*(sizeof e))){
    lock_release(&directory_inode->inode_directory_lock);
    return false;
  }

  if (bytes_written == sizeof e)
    success = true;

 done:
  /* Check that NAME is now in use. */
  ASSERT (lookup (dir, name, NULL, NULL));
  lock_release(&directory_inode->inode_directory_lock);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  bool success = false;
  struct dir_entry e;
  struct inode *inode = NULL;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  struct inode *directory_inode = dir_get_inode(dir);
  ASSERT (directory_inode != NULL);

  lock_acquire(&directory_inode->inode_directory_lock);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
  {
    //printf("DEBUG: lookup failed in remove\n");
    goto done;
  }

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
  {
    //printf("DEBUG: inode NULL in remove\n");
    goto done;
  }

  if (inode->directory == true){
    lock_acquire(&inode->inode_directory_lock);
    struct dir *delete_dir = dir_open(inode);
    if(!dir_is_empty(delete_dir)){
      /* directory is not empty and cannot be removed */
      lock_release(&inode->inode_directory_lock);
      dir_close(delete_dir);
      //printf("DEBUG: removing dir is not empty\n");
      goto done;
    } else {
      /* inode has to be removed and entry has to be set to not in use */
      lock_release(&inode->inode_directory_lock);
      dir_close(delete_dir);
    }
  }


  /* Erase directory entry by writing new entry with in_use=false */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
  {
    //printf("DEBUG: inode overwrite of dir entry failed\n");
    goto done;
  }


  /* Remove inode. */
  //printf("DEBUG: removed '%s'\n", name); 
  inode_remove (inode);
  success = true;

 done:
  //printf("DEBUG: remove of '%s' failed\n", name); 
  lock_release(&directory_inode->inode_directory_lock);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/* Checks if directory is empty */
bool
dir_is_empty (struct dir *dir)
{
  struct inode *inode = dir->inode;
  struct dir_entry e;
  off_t iterator_next_dir_entry = 0 * sizeof(e);

  while (true) 
    {
      /* stop if no more valid entry was read! */
      if (inode_read_at (inode, &e, sizeof e, iterator_next_dir_entry) != sizeof e)
        break;

      if (e.in_use)
        {
          return false;
        } 
      iterator_next_dir_entry += sizeof(e);
    }
  return true;
}

/* returns parent dir for passed directory */
struct dir*
dir_open_parent_dir(struct dir *dir)
{
  ASSERT(dir != NULL);
  struct dir *parent_dir = NULL;

  struct inode *inode = dir_get_inode(dir);

  // TODO reopen instead?
  block_sector_t parent_inode_block = inode_parent(inode);
  ASSERT(parent_inode_block != 0);
  struct inode *parent_inode = inode_open(parent_inode_block);
  ASSERT(parent_inode != NULL);


  parent_dir = dir_open(parent_inode);

  ASSERT(parent_dir != NULL);

  return parent_dir;
}
