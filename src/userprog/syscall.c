#include "userprog/syscall.h"
#include <stdio.h>
#include "lib/string.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "kernel/console.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

static unsigned max_file_name = 14;

void syscall_init (void);
void validate_pointer(const void* pointer);
void* read_argument_at_index(struct intr_frame *f, int arg_offset);
void syscall_exit(const int exit_type);
void syscall_halt(void);
tid_t syscall_exec(const char *cmd_line);
int syscall_write(int fd, const void *buffer, unsigned size);
int syscall_read(int fd, void *buffer, unsigned size);
bool syscall_create(const char *file_name, unsigned initial_size);
bool syscall_remove(const char *file_name);
bool check_file_name(const char *file_name);
int syscall_open(const char *file_name);
int syscall_filesize(int fd);
struct file* get_file(int fd);
void clear_files();
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
void syscall_close(int fd);
struct list_elem* get_list_elem(int fd);

struct lock lock_filesystem;


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&lock_filesystem);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if(f == NULL)
    syscall_exit(-1);
  void *esp = (int*) f->esp;

  validate_pointer(esp);

  // syscall type int is stored at adress esp
  int32_t syscall_type = *((int*)esp);

  switch (syscall_type)
    {
    case SYS_HALT:
      {
        syscall_halt();
        break;
      }

    case SYS_EXIT:
      {
        int exit_type = *((int*) read_argument_at_index(f, 0));
        syscall_exit(exit_type);
        break;
      }

    case SYS_EXEC:
      {
        char *cmd_line = *((char**) read_argument_at_index(f, 0));
        validate_pointer(cmd_line);
        f->eax = syscall_exec(cmd_line);
        break;
      }

    case SYS_WAIT:
      {
        pid_t pid = *((pid_t*) read_argument_at_index(f, 0));
        f->eax = process_wait(pid);
        break;
      }

    case SYS_CREATE:
      {
        char *file_name= *((char**) read_argument_at_index(f,0));
        validate_pointer(file_name);
        if (!check_file_name(file_name)){
          f->eax = false;
        } else {
          unsigned initial_size = *((unsigned*) read_argument_at_index(f,sizeof(char*)));
          f->eax = syscall_create(file_name, initial_size);
        }
        break;
      }

    case SYS_REMOVE:
      {
        char *file_name= *((char**) read_argument_at_index(f,0));
        validate_pointer(file_name);
        if (!check_file_name(file_name)){
          f->eax = false;
        } else {
          f->eax = syscall_remove(file_name);
        }
        break;
      }

    case SYS_OPEN:
      {
        char *file_name= *((char**) read_argument_at_index(f,0));
        validate_pointer(file_name);
        if (!check_file_name(file_name)){
          f->eax = false;
        } else {
          f->eax = syscall_open(file_name);
        }
        break;
      }

    case SYS_FILESIZE:
      {
        int fd = *((int*)read_argument_at_index(f,0)); 
        f->eax = syscall_filesize(fd);
        break;
      }

    case SYS_READ:
      {
        lock_acquire(&lock_filesystem);
        int fd = *((int*)read_argument_at_index(f,0)); 
        void *buffer = *((void**)read_argument_at_index(f,sizeof(int))); 
        unsigned size = *((unsigned*)read_argument_at_index(f,2*sizeof(int))); 
        int returnvalue = syscall_read(fd, buffer, size);
        f->eax = returnvalue;
        lock_release(&lock_filesystem);
      }
      break;

    case SYS_WRITE:
      {
        lock_acquire(&lock_filesystem);
        int fd = *((int*)read_argument_at_index(f,0)); 
        void *buffer = *((void**)read_argument_at_index(f,sizeof(int))); 
        unsigned size = *((unsigned*)read_argument_at_index(f,2*sizeof(int))); 
        int returnvalue = syscall_write(fd, buffer, size);
        f->eax = returnvalue;
        lock_release(&lock_filesystem);
        break;
      }

    case SYS_SEEK:
      {
        int fd = *((int*)read_argument_at_index(f,0)); 
        unsigned position = *((unsigned*)read_argument_at_index(f,sizeof(int)));
        syscall_seek(fd, position);
        break;
      }

    case SYS_TELL:
      {
        int fd = *((int*)read_argument_at_index(f,0)); 
        f->eax = syscall_tell(fd);
        break;
      }

    case SYS_CLOSE:
      {
        int fd = *((int*)read_argument_at_index(f,0)); 
        syscall_close(fd);
        break;
      }

    case SYS_MMAP:
      // TODO project 3
      break;

    case SYS_MUNMAP:
      // TODO project 3
      break;

    case SYS_CHDIR:
      // TODO project 4
      break;

    case SYS_MKDIR:
      // TODO project 4
      break;

    case SYS_READDIR:
      // TODO project 4
      break;

    case SYS_ISDIR:
      // TODO project 4
      break;

    case SYS_INUMBER:
      // TODO project 4
      break;

    default:
      {
        syscall_exit(-1);
        break;
      }
    }
}

void
validate_pointer(const void* pointer){
  // Validation of pointer
  uint32_t *pagedir = thread_current()->pagedir;
  if (pointer == NULL || !is_user_vaddr(pointer) || pagedir_get_page(pagedir, pointer)==NULL || pointer< 0x08048000){
    // Exit if pointer is not valid
    syscall_exit(-1);
  }
}
  

// Read argument of frame f at location shift.
void *
read_argument_at_index(struct intr_frame *f, int arg_offset){

  void *esp = (void*) f->esp;
  validate_pointer(esp);

  void *argument = esp + sizeof(int) + arg_offset;
  validate_pointer(argument);

  return argument;
}


void
syscall_exit(const int exit_type){
  // TODO check for held locks
  struct thread* terminating_thread = thread_current();
  struct child_process* terminating_child = terminating_thread->child_process;

  if (terminating_child != NULL){
    lock_acquire(&terminating_child->child_process_lock);
    if (terminating_child->parent != NULL){
      /* parent is still running -> has to store information */
      terminating_child->terminated = true;
      terminating_child->exit_status = exit_type;
      cond_signal(&terminating_child->terminated_cond, &terminating_child->child_process_lock);
      lock_release(&terminating_child->child_process_lock);
    } else {
      /* parent is already terminated -> free ressources */
      lock_release(&terminating_child->child_process_lock);
      free(terminating_child);
      terminating_child = NULL;
    }
  }

  /* close all files in this thread and free ressources */
  clear_files();

  printf("%s: exit(%d)\n", thread_current()->name, exit_type);

  thread_exit();
}


int
syscall_write(int fd, const void *buffer, unsigned size){
  int returnvalue = 0;

  /* check if the entire buffer is valid */
  uintptr_t buffer_end = ((uintptr_t)buffer) + size - 1;
  validate_pointer(buffer);
  validate_pointer(buffer_end);

  // use temporary buffer to make sure we don't overflow?
  if (fd == STDOUT_FILENO){
    unsigned sizeleft = size;
    putbuf(buffer,size);
    returnvalue = size;
  }
  else if (fd == STDIN_FILENO){
    returnvalue = -1;
  }
  else{
    struct file *file_ = get_file(fd);
    
    // check if file NULL
    if (file_ == NULL){
      returnvalue = 0;
    }else{
      returnvalue = file_write(file_, buffer, size);
    }
  }
  return returnvalue;
}


int
syscall_read(int fd, void *buffer, unsigned size){
  int returnvalue = 0;

  /* check if the entire buffer is valid */
  uintptr_t buffer_end = ((uintptr_t)buffer) + size - 1;
  validate_pointer(buffer);
  validate_pointer(buffer_end);

  if (fd == STDOUT_FILENO){
    returnvalue = -1;
  }
  else if (fd == STDIN_FILENO){
    unsigned size_left = size;
    uint8_t *buffer_copy = (uint8_t *)buffer;
    uint8_t input_char = 0;
      
    while (size_left > 1){
      validate_pointer(buffer_copy);
      input_char = input_getc();
      if (input_char == NULL)
        syscall_exit(-1);
      if (input_char == 0)
        break;
      else{
        size_left -= 1;
        *buffer_copy = input_char;
        buffer_copy += 1;
      }
    }

    returnvalue = size - size_left;
  }
  else{
    struct file *file_ = get_file(fd);
    
    // check if file NULL
    if (file_ == NULL){
      returnvalue = 0;
    }else{
      returnvalue = file_read(file_, buffer, size);
    }
  }

  return returnvalue;
}

void
syscall_halt(){
  shutdown_power_off();
}

tid_t
syscall_exec(const char *cmd_line){

  if (cmd_line == NULL){
    return -1;
  }
  if (strlen(cmd_line) == 0){
    return -1;
  }

  // TODO validate every single pointer until end!
  /* Validate end of passed string */
  int arg_length = strlen(cmd_line);
  char *cmd_line_end = cmd_line + arg_length + 1;
  validate_pointer(cmd_line_end);

  // TODO maybe lock filesystem here because load uses IO
  pid_t pid = process_execute(cmd_line);
  
  struct child_process *current_child = get_child(pid);
  if (current_child == NULL){
    return -1;
  }

  lock_acquire(&current_child->child_process_lock);

  while(current_child->successfully_loaded == NOT_LOADED){
    cond_wait(&current_child->loaded, &current_child->child_process_lock);
  }

  if (current_child->successfully_loaded == LOAD_FAILURE){
    lock_release(&current_child->child_process_lock);
    return -1;
  }

  lock_release(&current_child->child_process_lock);
  return pid;  // return to process pid
}

bool
syscall_create(const char *file_name, unsigned initial_size){
  lock_acquire(&lock_filesystem);
  bool success = filesys_create(file_name, initial_size);
  lock_release(&lock_filesystem);
  return success;
}

bool
syscall_remove(const char *file_name){
  lock_acquire(&lock_filesystem);
  bool success = filesys_remove(file_name);
  lock_release(&lock_filesystem);
  return success;
}

bool check_file_name (const char *file_name){
	if (file_name == NULL){
		syscall_exit(-1);
	} else if (strlen(file_name)>max_file_name){
		return false;
	}
	return true;
}

int syscall_open(const char *file_name){
  lock_acquire(&lock_filesystem);
  struct file *new_file = filesys_open(file_name);
  if (new_file == NULL){
    lock_release(&lock_filesystem);
    return -1;
  }
  struct thread *t = thread_current();
  int current_fd = t->current_fd;
  t->current_fd += 1;
  struct file_entry *current_entry = malloc(sizeof(struct file_entry));
  current_entry->fd = current_fd;
  current_entry->file = new_file;
  list_push_back (&t->file_list, &current_entry->elem);
  lock_release(&lock_filesystem);
  return current_fd;
}

int syscall_filesize(int fd){
  struct file* file = get_file(fd);
  if (file == NULL){
    return -1;
  }
  lock_acquire(&lock_filesystem);
  unsigned size = file_length(file);
  lock_release(&lock_filesystem);
  return size;
}


/* searchs the file in current thread */
struct file*
get_file(int fd){
  struct thread *t = thread_current();
  struct list *open_files = &(t->file_list);

  if (list_empty(open_files))
      return NULL;

  struct list_elem *iterator = list_begin (open_files);

  while (iterator != list_end (open_files)){
      struct file_entry *f = list_entry (iterator, struct file_entry, elem);
      if (f->fd == fd)
        return f->file;
      iterator = list_next(iterator);
  }
  return NULL;
}

/* clears all open files in current thread */
void
clear_files(){
  struct thread *t = thread_current();
  struct list *open_files = &(t->file_list);

  if (list_empty(open_files))
      return;

  struct list_elem *iterator = list_begin (open_files);

  while (iterator != list_end (open_files)){
      struct file_entry *f = list_entry (iterator, struct file_entry, elem);
      if (f->file != NULL){
        file_close(f->file);
      }
      struct list_elem *removeElem = iterator; 
      iterator = list_next(iterator);
      list_remove (removeElem);
      free(f);
  }
}


/* searchs the file in current thread and returns list_elem */
struct list_elem*
get_list_elem(int fd){
  struct thread *t = thread_current();
  struct list *open_files= &(t->file_list);
  
  if (list_empty(open_files))
      return NULL;

  struct list_elem *iterator = list_begin (open_files);


  while (iterator != list_end(open_files)){
      struct file_entry *f = list_entry (iterator, struct file_entry, elem);
      if (f->fd == fd)
        return iterator;
      iterator = list_next(iterator);
  }
  return NULL;
}


void syscall_seek(int fd, unsigned position){
  struct file *file = get_file(fd);
  if (file == NULL){
    syscall_exit(-1);
  }
  lock_acquire(&lock_filesystem);
  file_seek(file, position);
  lock_release(&lock_filesystem);
}

unsigned syscall_tell(int fd){
  struct file *file = get_file(fd);
  if (file == NULL){
    syscall_exit(-1);
  }
  lock_acquire(&lock_filesystem);
  unsigned pos = file_tell(file);
  lock_release(&lock_filesystem);
  return pos;
}

void syscall_close(int fd){
  lock_acquire(&lock_filesystem);
  struct list_elem *element = get_list_elem(fd);

  if (element == NULL){
		lock_release(&lock_filesystem);
    return;
  }
  struct file_entry *f = list_entry (element, struct file_entry, elem);

  if (f->file == NULL){
		lock_release(&lock_filesystem);
    return;
  }

  file_close(f->file);
  list_remove (element);
  free(f);
  lock_release(&lock_filesystem);
}

