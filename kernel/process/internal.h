#ifndef PROCESS_INTERNAL_H
#define PROCESS_INTERNAL_H
#include <kernel/process.h>
#include <fs/vfs.h>
#define PROC_FREE 0
#define PROC_BUILDING 1
#define PROC_RUNNING 2
#define PROC_DEAD 3
#define PROC_ZOMBIE 4
#define FD_INPUT 1
#define FD_OUTPUT 2
#define FD_FILE 3
struct process_fd { unsigned int kind, flags; struct file *file; };
struct process {
    unsigned int state;
    uint32_t pid, parent;
    int status;
    uint64_t heap_base, heap_break, heap_limit;
    struct process_fd fd[PROCESS_FD_MAX];
};
void process_pin(void);
void process_unpin(void);
int process_user_string(uint64_t address, char *out, size_t capacity);
int process_user_path(uint64_t address, char *out, size_t capacity);
int64_t process_fd_call(struct process *, uint64_t number, const uint64_t args[6]);
#endif
