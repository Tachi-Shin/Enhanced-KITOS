#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H
#include <kernel/vm.h>
#define PROCESS_ARG_MAX 16U
#define PROCESS_ARG_BYTES 4096U
#define PROCESS_FD_MAX 16U
#define PROCESS_STACK_TOP 0x7FFF000000ULL
#define PROCESS_STACK_SIZE (64U * 1024U)
#define PROCESS_HEAP_MAX (16U * 1024U * 1024U)
struct process;
int process_spawn(const char *path, unsigned int argc, const char *const argv[],
                   unsigned int envc, const char *const envp[]);
int process_wait(uint32_t pid, int *status, bool nonblocking);
void process_task_exited(struct process *, int status, uint64_t syndrome);
void process_reaper(void *unused);
int64_t process_syscall(uint64_t number, const uint64_t arguments[6]);
void process_print(void);
#endif
