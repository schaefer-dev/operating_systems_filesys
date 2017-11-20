#include "userprog/syscall.h"
#include <stdio.h>
#include "lib/string.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

// TODO:
// save list of file descriptors in thread
// save list of child processes in thread
// save partent process in thread
// parse arguments in start_process and pass **arguments and num_arguments
// to load and to setup_stack where the real work will be done
// setup_stack setups stack like described in description

// lock file system operations with lock in syscall.c
// make sure to mark executables as "non writable" as explained in description
// nicht nach stin und stout lesen
// file descriptor table in thread as list
// struct child-thread which contains *thread, parent, 


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int32_t *esp = (int32_t*) f->esp;

  validate_pointer(esp);

  // syscall type int is stored at adress esp
  int32_t syscall_type = *esp;

  switch (syscall_type)
    {
    case SYS_HALT:
      syscall_halt();
      break;

    case SYS_EXIT:
      {
        int exit_type = read_argument_at_index(f, 0);
        syscall_exit(exit_type);
        break;
      }

    case SYS_EXEC:
      char *cmd_line = read_argument_at_index(f, 0);
      syscall_exec(cmd_line);
      break;

    case SYS_WAIT:
      break;

    case SYS_CREATE:
      break;

    case SYS_REMOVE:
      break;

    case SYS_OPEN:
      break;

    case SYS_FILESIZE:
      break;

    case SYS_READ:
      break;

    case SYS_WRITE:
      break;

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
  printf ("system call!\n");
  thread_exit ();
}

void
validate_pointer(uint32_t* pointer){
  // Validation of pointer
  if (pointer == NULL || !is_user_vaddr(pointer)){
    // Exit if pointer is not valid
    syscall_exit(-1);
  }
}
  

// Read argument of frame f at location shift.
unit32_t
read_argument_at_index(struct intr_frame *f, int arg_index){

  uint32_t *esp = (uint32_t*) f->esp;
  validate_pointer(esp);

  uint32_t *argument = esp + 1 + index;
  validate_pointer(esp);

  return *argument;
}

void
syscall_exit(const int exit_type){
  char terminating_thread_name[16];
  strlcpy(terminating_thread_name, thread_name(), 16);
  printf("%s: exit(%d)\n", terminating_thread_name, exit_type);

}

void
syscall_halt(){
  shutdown_power_off();
}

void
syscall_exec(const char *cmd_line){
  // TODO make sure to not change program which is running during runtime (see project description)
  // TODO must return pid -1 (=TID_ERROR), if the program cannot load or run for any reason
  // TODO process_execute returns the thread id of the new process
  pid_t pid = process_execute(cmd_line); 
  f->eax = pid;  // return to process (pid)
}
