// kernel/sched/task.c
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vm.h>
#include <kernel/error.h>
#include <kernel/process.h>
#include <arch/memory.h>
#include <arch/user.h>
#include <arch/context.h>

struct task {
    uint32_t id;
    char task_name[TASK_NAME_SIZE];

    uint32_t state;

    struct process *process;
    bool user;
    struct vm_space space;
    uint64_t user_entry, user_stack, user_argument;
    int exit_status;
    uint64_t fault_esr, fault_address, fault_pc;
    bool kill_pending;
    bool protected;
    unsigned int resources;

    void (*entry)(void *);
    void *argument;

    /*
     * スタック領域は[stack_bottom, stack_top)。
     * stack_topは最後のバイトの直後を指す。
     */
    void *stack_bottom;
    void *stack_top;

    /* このタスクのsleepで使用するタイマー */
    struct timer_event sleep_timer;

    struct arch_context context;
};


#include <arch/irq.h>

static struct task task_table[MAX_TASKS];

static uint8_t task_stacks[MAX_TASKS][TASK_STACK_SIZE + PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));

static struct task *current_task;
static struct arch_context boot_context;

/*
 * 実行可能な通常タスクがない場合の待機用タスク。
 * 通常タスクの枠やIDは消費しない。
 */
static struct task idle_task;

static uint8_t idle_stack[TASK_STACK_SIZE + PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));

/* タスク枠を再利用しても、IDは新しく割り当てる */
static uint32_t next_id = 1U;

static void copy_text(
    char *dst,
    const char *src,
    unsigned int size
)
{
    unsigned int i = 0U;

    while (i + 1U < size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

/* IRQ禁止状態で呼ぶ */
static struct task *find_task(uint32_t id)
{
    for (unsigned int i = 0U; i < MAX_TASKS; i++) {
        if (
            task_table[i].state != TASK_UNUSED &&
            task_table[i].id == id
        ) {
            return &task_table[i];
        }
    }

    return NULL;
}

/* IRQ禁止状態で呼ぶ */
static struct task *select_next_task(void)
{
    unsigned int index = MAX_TASKS - 1U;

    if (
        current_task != NULL &&
        current_task != &idle_task
    ) {
        index = (unsigned int)(current_task - task_table);
    }

    for (unsigned int i = 0U; i < MAX_TASKS; i++) {
        index = (index + 1U) % MAX_TASKS;

        if (task_table[index].state == TASK_READY) {
            return &task_table[index];
        }
    }

    /*
     * 他にREADYタスクがなくても、
     * 現在のタスクが実行可能なら継続する。
     */
    if (
        current_task != NULL &&
        current_task->state == TASK_RUNNING
    ) {
        return current_task;
    }

    /* 全タスクが待機・停止・終了している */
    return &idle_task;
}

/* IRQ禁止状態で呼ぶ */
static void switch_to_task(struct task *next)
{
    struct task *previous = current_task;

    if (next == NULL || next == previous) {
        return;
    }

    /*
     * SLEEPING、STOPPED、EXITEDなどの状態は保持する。
     */
    if (previous->state == TASK_RUNNING) {
        previous->state = TASK_READY;
    }

    next->state = TASK_RUNNING;
    current_task = next;
    vm_activate(next->user ? &next->space : NULL);

    arch_context_switch(
        &previous->context,
        &next->context
    );
    /* This context is now on another live kernel stack. Exited user address
     * spaces can be released without freeing the stack doing the release. */
    task_reap();
}

static void task_bootstrap(void)
{
    struct task *task = current_task;

    task_reap();
    /* 新しいタスクのスタックに切り替わってからIRQを許可 */
    arch_irq_restore(0U);

    task->entry(task->argument);

    /* タスク関数から戻った場合は終了処理へ */
    task_exit();
}

static void idle_entry(void *argument)
{
    (void)argument;

    for (;;) {
        arch_wait_for_irq();
    }
}

/*
 * タイマー割込み中に呼ばれる。
 * 実行可能状態に戻すだけで、ここでは切り替えない。
 */
static void wake_sleeping_task(void *argument)
{
    struct task *task = argument;

    if (task->state == TASK_SLEEPING) {
        task->state = TASK_READY;
    }

    /*
     * STOPPEDなら、期限が到来しても停止状態を維持する。
     * resume時にタイマーの登録状態を確認する。
     */
}

int task_sleep(uint32_t id, uint32_t milliseconds)
{
    uint64_t flags = arch_irq_save();

    struct task *task = find_task(id);
    int result = -1;

    /*
     * シェル自身や保護タスクは休止させない。
     * 停止中・終了済み・別の理由で待機中のタスクも対象外。
     */
    if (milliseconds == 0U ||
        task == NULL ||
        task == current_task ||
        task->protected || task->resources ||
        (task->state != TASK_READY &&
         task->state != TASK_SLEEPING)) {
        goto done;
    }

    if (kernel_timer_arm(
            &task->sleep_timer,
            milliseconds,
            wake_sleeping_task,
            task
        ) == 0) {
        task->state = TASK_SLEEPING;
        result = 0;
    }

done:
    arch_irq_restore(flags);
    return result;
}

int task_create(
    const char *name,
    void (*entry)(void *),
    void *argument
)
{
    if (name == NULL || entry == NULL) {
        return -1;
    }

    uint64_t flags = arch_irq_save();
    int result = -1;

    if (next_id > 0x7FFFFFFFU) {
        goto done;
    }

    for (unsigned int i = 0U; i < MAX_TASKS; i++) {
        struct task *task = &task_table[i];

        /*
         * 現在実行中のタスクのスタックは再利用しない。
         * 未使用か終了済みの枠を探す。
         */
        if (
            task == current_task ||
            (
                task->state != TASK_UNUSED &&
                task->state != TASK_EXITED
            )
        ) {
            continue;
        }

        vm_space_destroy(&task->space);
        task->user = false;
        task->exit_status = 0;
        task->fault_esr = task->fault_address = task->fault_pc = 0;
        task->id = next_id++;

        copy_text(
            task->task_name,
            name,
            TASK_NAME_SIZE
        );

        task->entry = entry;
        task->argument = argument;

        task->protected = false;
        task->kill_pending = false;
        task->resources = 0;

        task->stack_bottom = &task_stacks[i][PAGE_SIZE];
        task->stack_top = &task_stacks[i][PAGE_SIZE + TASK_STACK_SIZE];

        /*
         * 再利用する枠に古いタイマーを残さない。
         */
        (void)kernel_timer_cancel(&task->sleep_timer);
        task->sleep_timer = (struct timer_event){0};

        arch_context_init(
            &task->context,
            task->stack_top,
            task_bootstrap
        );

        /* 初期化完了後に実行可能にする */
        task->state = TASK_READY;

        result = (int)task->id;
        break;
    }

done:
    arch_irq_restore(flags);
    return result;
}

void task_yield(void)
{
    uint64_t flags = arch_irq_save();

    if (current_task != NULL) {
        switch_to_task(select_next_task());
    }

    arch_irq_restore(flags);
}

int task_sleep_ms(uint32_t milliseconds)
{
    uint64_t flags = arch_irq_save();

    /*
     * IRQ禁止中や割込みハンドラ内からのsleepを拒否する。
     */
    if (
        (flags & (1ULL << 7)) != 0U ||
        current_task == NULL ||
        current_task == &idle_task ||
        current_task->state != TASK_RUNNING
    ) {
        arch_irq_restore(flags);
        return -1;
    }

    if (milliseconds != 0U) {
        if (
            kernel_timer_arm(
                &current_task->sleep_timer,
                milliseconds,
                wake_sleeping_task,
                current_task
            ) != 0
        ) {
            arch_irq_restore(flags);
            return -1;
        }

        current_task->state = TASK_SLEEPING;
    }

    /*
     * milliseconds == 0の場合は、
     * 状態をRUNNINGのまま切替処理へ渡してyieldする。
     *
     * 正の待機時間ならSLEEPING状態なので、
     * 次回以降の実行対象から外れる。
     */
    switch_to_task(select_next_task());

    /*
     * タイマーによる復帰後、再びこのタスクが選ばれると
     * ここから処理が再開する。
     */
    arch_irq_restore(flags);

    return 0;
}

void task_exit(void)
{
    (void)arch_irq_save();

    if (current_task != NULL) {
        (void)kernel_timer_cancel(&current_task->sleep_timer);

        if (current_task->process) {
            process_task_exited(current_task->process, current_task->exit_status, current_task->fault_esr);
            current_task->process = NULL;
        }
        current_task->state = TASK_EXITED;

        switch_to_task(select_next_task());
    }

    arch_irq_restore(0U);

    for (;;) {
        arch_wait_for_irq();
    }
}

void task_start(void)
{
    uint64_t flags = arch_irq_save();

    if (current_task == NULL) {
        idle_task.entry = idle_entry;
        idle_task.protected = true;

        idle_task.stack_bottom = idle_stack + PAGE_SIZE;
        idle_task.stack_top = idle_stack + sizeof(idle_stack);

        arch_context_init(
            &idle_task.context,
            idle_task.stack_top,
            task_bootstrap
        );

        current_task = select_next_task();
        current_task->state = TASK_RUNNING;
        vm_activate(current_task->user ? &current_task->space : NULL);

        arch_context_switch(
            &boot_context,
            &current_task->context
        );
    }

    arch_irq_restore(flags);
}

int task_protect(uint32_t id)
{
    uint64_t flags = arch_irq_save();

    struct task *task = find_task(id);
    int result = -1;

    if (
        task != NULL &&
        task->state != TASK_EXITED
    ) {
        task->protected = true;
        result = 0;
    }

    arch_irq_restore(flags);

    return result;
}

int task_control(
    uint32_t id,
    enum task_operation operation
)
{
    uint64_t flags = arch_irq_save();

    struct task *task = find_task(id);
    int result = -1;

    if (task == NULL ||
        task == current_task ||
        task->protected) {
        goto done;
    }
    if (task->resources) {
        if (operation == TASK_KILL && task->process) {
            task->kill_pending = true; result = 0;
        }
        goto done;
    }

    if (operation == TASK_STOP &&
        (task->state == TASK_READY ||
         task->state == TASK_SLEEPING)) {
        /* 自動復帰を解除して、無期限停止へ変更する。 */
        (void)kernel_timer_cancel(&task->sleep_timer);

        task->state = TASK_STOPPED;
        result = 0;

    } else if (operation == TASK_RESUME &&
               (task->state == TASK_STOPPED ||
                task->state == TASK_SLEEPING)) {
        /* sleepの期限を待たず、実行可能にする。 */
        (void)kernel_timer_cancel(&task->sleep_timer);

        task->state = TASK_READY;
        result = 0;

    } else if (operation == TASK_KILL &&
               (task->state == TASK_READY ||
                task->state == TASK_STOPPED ||
                task->state == TASK_SLEEPING)) {
        /* 終了後に古いタイマーで復活しないよう解除する。 */
        (void)kernel_timer_cancel(&task->sleep_timer);

        task->state = TASK_EXITED;
        task->exit_status = -K_ECANCELED;
        if (task->process) {
            process_task_exited(task->process, task->exit_status, 0);
            task->process = NULL;
        }
        result = 0;
    }

done:
    arch_irq_restore(flags);
    return result;
}

unsigned int task_snapshot(
    struct task_info *out,
    unsigned int capacity
)
{
    if (out == NULL) {
        return 0U;
    }

    uint64_t flags = arch_irq_save();
    task_reap();
    unsigned int n = 0U;

    for (
        unsigned int i = 0U;
        i < MAX_TASKS && n < capacity;
        i++
    ) {
        struct task *task = &task_table[i];

        if (task->state == TASK_UNUSED) {
            continue;
        }

        out[n].id = task->id;
        out[n].state = task->state;
        out[n].user = task->user;
        out[n].exit_status = task->exit_status;
        out[n].fault_esr = task->fault_esr;
        out[n].fault_address = task->fault_address;
        out[n].fault_pc = task->fault_pc;

        out[n].stack_bottom = task->stack_bottom;
        out[n].stack_top = task->stack_top;

        copy_text(
            out[n].name,
            task->task_name,
            TASK_NAME_SIZE
        );

        n++;
    }

    arch_irq_restore(flags);

    return n;
}

/* Called with IRQ masked by the gate implementation. */
void task_resource_get(void) { if (current_task) ++current_task->resources; }
void task_resource_put(void) { if (current_task && current_task->resources) --current_task->resources; }

int task_init_stacks(void)
{
    for (unsigned int i = 0; i < MAX_TASKS; ++i) {
        int rc = arch_kernel_guard(task_stacks[i]);
        if (rc) return rc;
    }
    return arch_kernel_guard(idle_stack);
}

uint32_t task_current_id(void)
{ return current_task ? current_task->id : 0; }
struct vm_space *task_current_space(void)
{ return current_task && current_task->user ? &current_task->space : NULL; }

static void user_bootstrap(void *unused)
{
    (void)unused;
    arch_enter_user(current_task->user_entry, current_task->user_stack, current_task->user_argument);
}

int task_create_user(const char *name, struct vm_space *space, uint64_t entry,
                     uint64_t stack, uint64_t argument)
{
    if (!space || !space->root || (entry & 3) || (stack & 15) || stack < 16 ||
        vm_user_check(space, entry, 4, VM_EXEC) ||
        vm_user_check(space, stack - 16, 16, VM_WRITE)) return -K_EINVAL;
    uint64_t flags = arch_irq_save();
    int id = task_create(name, user_bootstrap, NULL);
    if (id > 0) {
        struct task *task = find_task((uint32_t)id);
        task->user = true;
        task->space = *space;
        space->root = 0;
        task->user_entry = entry;
        task->user_stack = stack;
        task->user_argument = argument;
    }
    arch_irq_restore(flags);
    return id;
}

void task_finish(int status, uint64_t syndrome, uint64_t address, uint64_t pc)
{
    (void)arch_irq_save();
    if (current_task) {
        current_task->exit_status = status;
        current_task->fault_esr = syndrome;
        current_task->fault_address = address;
        current_task->fault_pc = pc;
    }
    task_exit();
    __builtin_unreachable();
}

void task_reap(void)
{
    uint64_t flags = arch_irq_save();
    for (unsigned int i = 0; i < MAX_TASKS; ++i)
        if (&task_table[i] != current_task && task_table[i].state == TASK_EXITED)
            vm_space_destroy(&task_table[i].space);
    arch_irq_restore(flags);
}

struct process *task_current_process(void)
{ return current_task ? current_task->process : NULL; }
int task_bind_process(uint32_t id, struct process *process)
{
    uint64_t flags = arch_irq_save();
    struct task *task = find_task(id);
    int rc = -K_EINVAL;
    if (task && task->user && task->state == TASK_READY && !task->process) {
        task->process = process; rc = 0;
    }
    arch_irq_restore(flags);
    return rc;
}

void task_check_cancel(void)
{
    uint64_t flags = arch_irq_save();
    if (current_task && current_task->kill_pending && !current_task->resources)
        task_finish(-K_ECANCELED, 0, 0, 0);
    arch_irq_restore(flags);
}
