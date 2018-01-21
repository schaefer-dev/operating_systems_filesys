#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void validate_pointer(const void* pointer);

void syscall_exit(const int exit_type);


/* locks the file system to avoid data races */
struct lock lock_filesystem;

#endif /* userprog/syscall.h */
