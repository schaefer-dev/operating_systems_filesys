#include "userprog/syscall.h"
#include <stdio.h>
#include "lib/string.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include <kernel/console.h>
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

void syscall_init (void);
void validate_pointer(const void* pointer);
void* read_argument_at_index(struct intr_frame *f, int arg_offset);
void syscall_exit(const int exit_type);
void syscall_halt(void);
void syscall_exec(const char *cmd_line, struct intr_frame *f);
int syscall_write(int fd, const void *buffer, unsigned size);
bool syscall_create(const char* file, unsigned initial_size);
bool syscall_remove(const char* file);

struct lock lock_filesystem;

// TODO:
// save list of file descriptors in thread
// save list of child processes in thread
// save partent process in thread
// struct child-thread which contains *thread, parent,

// parse arguments in start_process and pass **arguments and num_arguments
// to load and to setup_stack where the real work will be done
// setup_stack setups stack like described in description

// lock file system operations with lock in syscall.c
// make sure to mark executables as "non writable" as explained in description
// nicht nach stin und stout lesen
// file descriptor table in thread as list 


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&lock_filesystem);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *esp = (int*) f->esp;

  //printf("DEBUG: Syscall ESP is |%p|\n", esp);

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
        char *cmd_line = (char*) read_argument_at_index(f, 0);
        syscall_exec(cmd_line, f);
        break;
      }

    case SYS_WAIT:
      break;

    case SYS_CREATE:
      {
        // TODO: check if length of file_name has to be checked //
        char* file_name= (char*) read_argument_at_index(f,0);
        unsigned initial_size = (unsigned) *((unsigned*)read_argument_at_index(f, strlen(file_name)*sizeof(char)+sizeof(char)));
        f->eax = syscall_create(file_name, initial_size);
        break;
      }

    case SYS_REMOVE:
      {
        char* file_name= (char*) read_argument_at_index(f,0);
        f->eax = syscall_remove(file_name);
        break;
      }

    case SYS_OPEN:
      break;

    case SYS_FILESIZE:
      {

        break;
      }

    case SYS_READ:
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
      break;

    case SYS_TELL:
      break;

    case SYS_CLOSE:
      break;

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
      printf("this should not happen! DEFAULT SYSCALL CASE");
      break;
    }
  

  // TODO remove this later
  //printf ("DEBUG: Forced thread_exit after FIRST syscall!\n");
  //thread_exit ();
}

void
validate_pointer(const void* pointer){
  // Validation of pointer
  if (pointer == NULL || !is_user_vaddr(pointer)){
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
  // check for held locks
  printf("%s: exit(%d)\n", thread_current()->name, exit_type);
  thread_exit();
}

int
syscall_write(int fd, const void *buffer, unsigned size){
  struct file_desc *fd_struct;
  int returnvalue = 0;

  //printf("DEBUG: write started with fd |%i|!\n", fd);

  validate_pointer(buffer);

  // use temporary buffer to make sure we don't overflow?
  if (fd == STDOUT_FILENO){
    //printf("DEBUG: putbuff called with size |%i| and pointer |%p| with content |%s|!\n", size, buffer, buffer);
    putbuf(buffer,size);
    returnvalue = size;
  }
  else{
    // TODO read fd_struct and get file handler
    
    // check if file NULL
    if (fd_struct != NULL){
      returnvalue = -1;
    }

    // TODO I/O Operations, make sure its locked here

    returnvalue = size;
  }

  return returnvalue;

}

void
syscall_halt(){
  shutdown_power_off();
}

void
syscall_exec(const char *cmd_line, struct intr_frame *f){
  // TODO make sure to not change program which is running during runtime (see project description)
  // TODO must return pid -1 (=TID_ERROR), if the program cannot load or run for any reason
  // TODO process_execute returns the thread id of the new process
  tid_t tid = process_execute(cmd_line); 
  f->eax = tid;  // return to process (tid)
}

bool
syscall_create(const char* file, unsigned initial_size){
  lock_acquire(&lock_filesystem);
  bool success = filesys_create(file, initial_size);
  lock_release(&lock_filesystem);
  return success;
}

bool
syscall_remove(const char* file){
  lock_acquire(&lock_filesystem);
  bool success = filesys_remove(file);
  lock_release(&lock_filesystem);
  return success;
}
