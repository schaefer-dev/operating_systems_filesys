#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void validate_pointer(const void* pointer);

/* locks the file system to avoid data races */
struct lock lock_filesystem;

#endif /* userprog/syscall.h */
