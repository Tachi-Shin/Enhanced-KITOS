#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <dos/type.h>
#include <dos/config.h>

#define TASK_NAME_SIZE   16U
struct vm_space;
struct process;

struct task_info {
    uint32_t id;
    uint32_t state;

    char name[TASK_NAME_SIZE];

    void *stack_bottom;
    void *stack_top;
    bool user;
    int exit_status;
    uint64_t fault_esr, fault_address, fault_pc;
};

enum task_operation {
    TASK_STOP,
    TASK_RESUME,
    TASK_KILL
};

int task_create(
    const char *name,
    void (*entry)(void *),
    void *argument
);

/* Takes ownership of space on success (clears the caller's root). */
int task_create_user(const char *name, struct vm_space *space, uint64_t entry,
                     uint64_t stack, uint64_t argument);
int task_init_stacks(void);
uint32_t task_current_id(void);
struct vm_space *task_current_space(void);
void task_finish(int status, uint64_t syndrome, uint64_t address, uint64_t pc) __attribute__((noreturn));
void task_reap(void);
void task_check_cancel(void);
struct process *task_current_process(void);
int task_bind_process(uint32_t id, struct process *process);

void task_start(void);
void task_yield(void);

/*
 * IRQが有効な通常のタスクから呼ぶ。
 * milliseconds == 0の場合はCPUを譲る。
 *
 * 成功時0、実行コンテキストが不正な場合は-1。
 */
int task_sleep_ms(uint32_t milliseconds);

/* 指定した別タスクを休止させる。millisecondsは1以上。 */
int task_sleep(uint32_t id, uint32_t milliseconds);

void task_exit(void) __attribute__((noreturn));

int task_protect(uint32_t id);

int task_control(
    uint32_t id,
    enum task_operation operation
);

unsigned int task_snapshot(
    struct task_info *out,
    unsigned int capacity
);

/* Resource pins prevent asynchronous stop/kill while holding kernel objects. */
void task_resource_get(void);
void task_resource_put(void);
#endif
