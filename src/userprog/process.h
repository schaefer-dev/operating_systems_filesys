#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

typedef int pid_t;

#define NOT_LOADED 0
#define LOAD_SUCCESS 1
#define LOAD_FAILURE 2

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct child_process get_child(pid_t pid);

#endif /* userprog/process.h */
