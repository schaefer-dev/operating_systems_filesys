#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

typedef int pid_t;

/* NOT_LOADED value for processes which are still in 
   the process of loading */
#define NOT_LOADED 0
#define LOAD_SUCCESS 1
#define LOAD_FAILURE 2

/*  every process which is created by a parent process is considered a
    child process and contains a struct child_process which stores all
    important values for child processes */
struct child_process
  {
    int exit_status;
    pid_t pid;
    pid_t parent;
    bool terminated;
    bool waited_for;
    int successfully_loaded;
    struct lock child_process_lock;
    struct condition loaded;
    struct condition terminated_cond;
    struct list_elem elem;
  };


bool faulty_esp(uintptr_t esp, uintptr_t min_addr);
pid_t process_execute (const char *file_name);
int process_wait (pid_t pid);
void process_exit (void);
void process_activate (void);
struct child_process* get_child(pid_t pid);

#endif /* userprog/process.h */
