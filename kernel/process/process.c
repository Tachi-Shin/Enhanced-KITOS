#include "internal.h"
#include <kernel/elf.h>
#include <kernel/task.h>
#include <kernel/error.h>
#include <kernel/printk.h>
#include <arch/irq.h>
#include <arch/user.h>
#include <lib/string.h>

static struct process processes[MAX_TASKS];
void process_pin(void)
{ uint64_t f = arch_irq_save(); task_resource_get(); arch_irq_restore(f); }
void process_unpin(void)
{ uint64_t f = arch_irq_save(); task_resource_put(); arch_irq_restore(f); task_check_cancel(); }
#define RETRY_BUSY(expression) do { rc = (expression); if (rc == -K_EBUSY) task_sleep_ms(1); } while (rc == -K_EBUSY)
static int image_read(void *context, uint64_t offset, void *buffer, uint32_t length)
{
    uint64_t position; uint32_t done; int rc;
    RETRY_BUSY(vfs_seek(context, (int64_t)offset, 0, &position));
    if (rc) return rc;
    RETRY_BUSY(vfs_read(context, buffer, length, &done));
    return rc ? rc : done == length ? 0 : -K_ENOEXEC;
}
static int initial_stack(struct vm_space *space, const struct elf_plan *plan,
    unsigned int argc, const char *const argv[], unsigned int envc, const char *const envp[], uint64_t *stack)
{
    uint64_t args[PROCESS_ARG_MAX], env[PROCESS_ARG_MAX], sp = PROCESS_STACK_TOP;
    size_t bytes = 0;
    if (!argc || argc > PROCESS_ARG_MAX || envc > PROCESS_ARG_MAX || !argv || (envc && !envp)) return -K_EINVAL;
    for (unsigned int group = 0; group < 2; ++group) {
        unsigned int n = group ? envc : argc;
        const char *const *strings = group ? envp : argv;
        for (unsigned int i = 0; i < n; ++i) {
            if (!strings[i]) return -K_EINVAL;
            size_t length = 0;
            while (length < PROCESS_ARG_BYTES && strings[i][length]) ++length;
            if (length == PROCESS_ARG_BYTES || length + 1 > PROCESS_ARG_BYTES - bytes) return -K_E2BIG;
            bytes += length + 1;
        }
    }
    for (uint64_t va = PROCESS_STACK_TOP - PROCESS_STACK_SIZE; va < PROCESS_STACK_TOP; va += PAGE_SIZE) {
        int rc = vm_map_new(space, va, VM_READ | VM_WRITE, NULL, 0);
        if (rc) return rc;
    }
    for (unsigned int group = 0; group < 2; ++group) {
        unsigned int n = group ? envc : argc;
        const char *const *strings = group ? envp : argv;
        uint64_t *pointers = group ? env : args;
        for (unsigned int i = 0; i < n; ++i) {
            size_t length = strlen(strings[i]) + 1;
            sp -= length; pointers[i] = sp;
            int rc = copy_to_user(space, sp, strings[i], length);
            if (rc) return rc;
        }
    }
    uint64_t words[2 * PROCESS_ARG_MAX + 9]; unsigned int n = 0;
    words[n++] = argc;
    for (unsigned int i = 0; i < argc; ++i) words[n++] = args[i];
    words[n++] = 0;
    for (unsigned int i = 0; i < envc; ++i) words[n++] = env[i];
    words[n++] = 0;
    words[n++] = 6; words[n++] = PAGE_SIZE; /* AT_PAGESZ */
    words[n++] = 9; words[n++] = plan->entry; /* AT_ENTRY */
    words[n++] = 0; words[n++] = 0;
    sp = (sp - n * sizeof(uint64_t)) & ~15ULL;
    int rc = copy_to_user(space, sp, words, n * sizeof(uint64_t));
    if (!rc) *stack = sp;
    return rc;
}
int process_spawn(const char *path, unsigned int argc, const char *const argv[],
                   unsigned int envc, const char *const envp[])
{
    if (!path || *path != '/') return -K_EINVAL;
    process_pin();
    uint64_t flags = arch_irq_save();
    struct process *p = NULL;
    for (unsigned int i = 0; i < MAX_TASKS; ++i) if (!processes[i].state) {
        p = &processes[i]; *p = (struct process){.state = PROC_BUILDING, .parent = task_current_id()}; break;
    }
    arch_irq_restore(flags);
    if (!p) { process_unpin(); return -K_EAGAIN; }
    struct file *file = NULL;
    struct vm_space space = {0};
    struct vfs_stat stat;
    int rc;
    RETRY_BUSY(vfs_open(path, VFS_READ, &file));
    if (rc) goto done;
    RETRY_BUSY(vfs_fstat(file, &stat));
    if (rc) goto done;
    struct image_reader reader = {file, stat.size, image_read};
    struct elf_plan plan;
    rc = elf_validate(&reader, ARCH_ELF_MACHINE, &plan);
    if (!rc) rc = vm_space_create(&space);
    if (!rc) rc = elf_load(&reader, &plan, &space);
    uint64_t sp = 0;
    if (!rc) rc = initial_stack(&space, &plan, argc, argv, envc, envp, &sp);
    if (rc) goto done;
    p->heap_base = plan.end + PAGE_SIZE;
    p->heap_break = p->heap_base;
    p->heap_limit = p->heap_base + PROCESS_HEAP_MAX;
    p->fd[0].kind = FD_INPUT; p->fd[1].kind = p->fd[2].kind = FD_OUTPUT;
    /* Release loader handle before publishing the child. */
    RETRY_BUSY(vfs_close(file)); file = NULL;
    if (rc) goto done;
    flags = arch_irq_save();
    rc = task_create_user(path, &space, plan.entry, sp, argc);
    if (rc > 0) {
        p->pid = (uint32_t)rc; p->state = PROC_RUNNING;
        if (task_bind_process(p->pid, p)) {
            task_control(p->pid, TASK_KILL); rc = -K_EINVAL; p->state = PROC_FREE;
        }
    }
    arch_irq_restore(flags);
done:
    if (file) { int saved = rc; RETRY_BUSY(vfs_close(file)); rc = saved; }
    vm_space_destroy(&space);
    if (rc < 0) p->state = PROC_FREE;
    process_unpin();
    return rc;
}

void process_task_exited(struct process *p, int status, uint64_t syndrome)
{
    /* IRQ-masked notification only. VFS cleanup runs in the protected reaper. */
    p->status = syndrome ? 11 : status == -K_ECANCELED ? 9 : (status & 255) << 8;
    p->state = PROC_DEAD;
    for (unsigned int i = 0; i < MAX_TASKS; ++i)
        if (processes[i].state && processes[i].parent == p->pid) processes[i].parent = 0;
}
static void collect(void)
{
    for (unsigned int i = 0; i < MAX_TASKS; ++i) {
        struct process *p = &processes[i];
        if (p->state == PROC_DEAD) {
            bool pending = false;
            for (unsigned int f = 0; f < PROCESS_FD_MAX; ++f) if (p->fd[f].kind == FD_FILE) {
                int rc = vfs_close(p->fd[f].file);
                if (rc == -K_EBUSY) pending = true;
                else p->fd[f] = (struct process_fd){0};
            }
            if (!pending) p->state = PROC_ZOMBIE;
        }
        if (p->state == PROC_ZOMBIE && !p->parent) p->state = PROC_FREE;
    }
}
void process_reaper(void *unused)
{
    (void)unused;
    for (;;) { collect(); task_sleep_ms(1); }
}
int process_wait(uint32_t pid, int *status, bool nonblocking)
{
    if (!status) return -K_EINVAL;
    for (;;) {
        uint64_t flags = arch_irq_save();
        bool found = false;
        for (unsigned int i = 0; i < MAX_TASKS; ++i) {
            struct process *p = &processes[i];
            if (p->state && p->parent == task_current_id() && (!pid || p->pid == pid)) {
                found = true;
                if (p->state == PROC_ZOMBIE) {
                    int result = (int)p->pid; *status = p->status; p->state = PROC_FREE;
                    arch_irq_restore(flags); return result;
                }
            }
        }
        arch_irq_restore(flags);
        if (!found) return -K_ECHILD;
        if (nonblocking) return 0;
        if (task_sleep_ms(1)) return -K_EINVAL;
    }
}
void process_print(void)
{
    uint64_t flags = arch_irq_save();
    for (unsigned int i = 0; i < MAX_TASKS; ++i) if (processes[i].state)
        printk("pid=%u parent=%u state=%u status=%d brk=%llx\n", processes[i].pid,
               processes[i].parent, processes[i].state, processes[i].status, processes[i].heap_break);
    arch_irq_restore(flags);
}
int process_kill_child(uint32_t pid, bool terminate)
{
    uint64_t flags = arch_irq_save();
    int rc = -K_ESRCH;
    for (unsigned int i = 0; i < MAX_TASKS; ++i) {
        struct process *p = &processes[i];
        if (p->state == PROC_RUNNING && p->pid == pid && p->parent == task_current_id())
            rc = !terminate ? 0 : task_control(pid, TASK_KILL) ? -K_EBUSY : 0;
    }
    arch_irq_restore(flags);
    return rc;
}
