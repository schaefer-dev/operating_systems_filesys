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


/* returns the path contained in the string 'name' */
char*
dir_get_path (const char* name)
{
  ASSERT(name != NULL);

  //bool is_absolute_path = false;
  int name_length = strlen(name);

  if (name_length == 0)
    return NULL;

  char *temp = malloc(sizeof(char) * (name_length + 1));

  // TODO make sure that this output is freed in all cases!
  char *output = malloc(sizeof(char) * name_length);
  unsigned output_offset = 0;

  /* to make sure that last token is not null */
  strlcpy(temp, name, name_length + 1);
  temp[name_length] = ' ';
  temp[name_length+1] = '\0';

  if (temp[0] == '/'){
    //is_absolute_path = true;
    char *absolute_path = "/";
    memcpy (output + output_offset, absolute_path, sizeof(char));
    output_offset += 1;
  }

  char *token = "";
  char *last_token = "";
  char *pos;
  for (token = strtok_r(temp, "/", &pos); token != NULL; token = strtok_r(NULL, "/", &pos)) {
    int token_length = strlen(token);
    if (token_length > 0){
      memcpy (output + output_offset, token, sizeof(char) * token_length);
      output_offset += token_length;
      output[output_offset] = '/';
      output_offset += 1;
    }
    last_token = token;
  }

  if (last_token != NULL)
      output_offset -= strlen(last_token);

  output[output_offset-1] = '\0';

  free(temp);

  if (strlen(output) == 0){
    free(output);
    return NULL;
  }

  return output;
}



/* returns the filename contained in the string 'name' and NULL if name is
empty or doesn't contain a filename */
char*
dir_get_file_name (const char* name)
{
  ASSERT(name != NULL);
  int name_length = strlen(name);

  if (name_length == 0)
    return NULL;

  char *temp = malloc(sizeof(char) * (name_length + 1));

  char *output = malloc(sizeof(char) * name_length);
  unsigned output_offset = 0;

  /* to make sure that last token is not null */
  strlcpy(temp, name, name_length + 1);
  temp[name_length] = ' ';
  temp[name_length+1] = '\0';


  char *token = "";
  char *last_token = "";
  char *pos;
  for (token = strtok_r(temp, "/", &pos); token != NULL; token = strtok_r(NULL, "/", &pos)) {
    last_token = token;
  }

  if (last_token != NULL){
    int last_token_length = strlen(last_token);
    memcpy (output, last_token, sizeof(char) * last_token_length);
    output_offset += last_token_length;

    output[output_offset-1] = '\0';
  }

  free(temp);

  if (strlen(output) == 0){
    free(output);
    return NULL;
  }

  return output;
}

struct dir*
dir_open_path(const char* path)
{
  //ASSERT(path != NULL);
  if (path == NULL){
    //TODO: open current working directory
    if (thread_current()->current_working_dir == NULL){
      //printf("DEBUG: open path cwd is NULL \n");
      return dir_open_root();		
    } else {
      //printf("DEBUG: open path cwd NOT NULL \n");
        return dir_reopen(thread_current()->current_working_dir);
    }
  }

  int name_length = strlen(path);

  if (name_length==0){
    return NULL;
  }

  struct dir *current_dir = NULL;

  char *temp = malloc(sizeof(char) * (name_length + 1));

  /* to make sure that last token is not null */
  strlcpy(temp, path, name_length + 1);

  if (temp[0] == '/'){
    /* absolute path */
    current_dir = dir_open_root();
  } else {
    /* relative path */
    struct thread *current_thread = thread_current();
    if (current_thread->current_working_dir != NULL){
      current_dir = dir_reopen(current_thread->current_working_dir);
    } else {
      current_dir = dir_open_root();
    }
  }

  /* change directory step by step based on path */
  char *token = "";
  char *pos;
  for (token = strtok_r(temp, "/", &pos); token != NULL; token = strtok_r(NULL, "/", &pos)) {
    int token_length = strlen(token);
    //printf("DEBUG: token_iter in dir opening: '%s'\n", token);
    if (token_length > 0){
      struct inode *inode = NULL;
      if (!dir_lookup (current_dir, token, &inode)){
        goto invalid;
      }
      if (inode == NULL){
        goto invalid;
      }
      struct dir *next_dir = dir_open(inode);
      if (next_dir == NULL){
        goto invalid;
      }
      dir_close(current_dir);
      current_dir = next_dir;
    }
  }


  free(temp);

  /* double check if inode has been removed already */
  if (inode_is_removed(current_dir->inode)) {
    dir_close(current_dir);
    return NULL;
  }

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
bool
dir_create_root (block_sector_t sector, size_t entry_cnt)
{
  bool success = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);

  struct inode *root_inode = inode_open (sector);
  struct dir *root = dir_open (root_inode);
  if (root == NULL)
    return NULL;
  struct dir_entry current;
  current.in_use = true;
  current.inode_sector = root_inode->sector;
  char *current_name = "."; 
  strlcpy (current.name, current_name, sizeof current.name);
  /* wirte own directory to first position */
  if(!inode_write_at(root->inode, &current, sizeof current, 0)){
    return false;
  }
  struct dir_entry parent;
  parent.in_use = true;
  parent.inode_sector = root_inode->sector;
  char *parent_name = "..";
  strlcpy (parent.name, parent_name, sizeof parent.name);
  /* wirte parent to second position in directory; first one is own directory */
  if(!inode_write_at(root->inode, &parent, sizeof parent, sizeof parent)){
    return false;
  }

  return success;
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
      inode_close (inode);
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
  /*
  if (root == NULL)
    return NULL;
  struct dir_entry current;
  current.in_use = true;
  current.inode_sector = root_inode->sector;
  char *current_name = "."; 
  strlcpy (current.name, current_name, sizeof current.name);
  /* wirte own directory to first position */
  /*
  if(!inode_write_at(root->inode, &current, sizeof current, 0)){
    return NULL;
  }
  struct dir_entry parent;
  parent.in_use = true;
  parent.inode_sector = root_inode->sector;
  char *parent_name = "..";
  strlcpy (parent.name, parent_name, sizeof parent.name);
  /* wirte parent to second position in directory; first one is own directory */
  /*
  if(!inode_write_at(root->inode, &parent, sizeof parent, sizeof parent)){
    return NULL;
  }
  */

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

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* if directory we add the directory to the file(in this case a directory) */
  /* TODO: could be bad at this position race if inode not already created */
  if(directory){
    struct dir *child_dir = dir_open(inode_open(inode_sector));
    if (child_dir == NULL)
      return false;

    /* add directory itself as directory entry to directory */
    struct dir_entry current;
    current.in_use = true;
    current.inode_sector = inode_sector;
    char *current_name = ".";
    strlcpy (current.name, current_name, sizeof current.name);
    /* wirte parent to second position in directory; first one is own directory */
    if(!inode_write_at(child_dir->inode, &current, sizeof current, 0)){
      dir_close(child_dir);
      return false;
    }
    /* add parent to directory */
    struct dir_entry parent;
    parent.in_use = true;
    struct inode *parent_inode = dir->inode;
    parent.inode_sector = parent_inode->sector;
    char *parent_name = "..";
    strlcpy (parent.name, parent_name, sizeof parent.name);
    /* wirte parent to second position in directory; first one is own directory */
    if(!inode_write_at(child_dir->inode, &parent, sizeof parent, sizeof parent)){
      dir_close(child_dir);
      return false;
    }
    dir_close(child_dir);
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

  ASSERT(ofs >= (2*(sizeof e)));

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if (inode->directory){
    struct dir *delete_dir = dir_open(inode);
    if(!dir_is_empty(delete_dir)){
      /* directory is not empty and cannot be removed */
      dir_close(delete_dir);
      goto done;
    } else {
      /* inode has to be removed and entry has to be set to not in use */
      dir_close(delete_dir);
    }
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
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
      if (e.in_use && ((dir->pos) >= (2*(sizeof e))))
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
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use && ((dir->pos) >= (2*(sizeof e))))
        {
          return false;
        } 
    }
  return true;
}
