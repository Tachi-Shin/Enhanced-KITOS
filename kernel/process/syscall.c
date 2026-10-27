#include "internal.h"
#include <kernel/task.h>
#include <kernel/error.h>
#include <uapi/syscall.h>
#include <lib/string.h>

static int64_t heap_break(struct process *p, uint64_t requested)
{
    if (!requested) return (int64_t)p->heap_break;
    if (requested < p->heap_base || requested > p->heap_limit) return -K_ENOMEM;
    uint64_t old = (p->heap_break + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    struct vm_space *space = task_current_space();
    uint64_t page;
    for (page = old; page < end; page += PAGE_SIZE) {
        int rc = vm_map_new(space, page, VM_READ | VM_WRITE, NULL, 0);
        if (rc) {
            while (page > old) { page -= PAGE_SIZE; (void)vm_unmap(space, page); }
            return rc;
        }
    }
    for (page = end; page < old; page += PAGE_SIZE) (void)vm_unmap(space, page);
    p->heap_break = requested;
    return (int64_t)requested;
}
struct launch_arguments {
    const char *args[PROCESS_ARG_MAX], *env[PROCESS_ARG_MAX];
    char bytes[PROCESS_ARG_BYTES]; size_t used;
};
static int user_vector(uint64_t vector, const char **out, unsigned int *count, struct launch_arguments *launch)
{
    *count = 0;
    if (!vector) return 0;
    for (unsigned int i = 0; i <= PROCESS_ARG_MAX; ++i) {
        uint64_t pointer;
        if (vector > ~0ULL - (uint64_t)i * 8 ||
            copy_from_user(task_current_space(), &pointer, vector + (uint64_t)i * 8, 8)) return -K_EFAULT;
        if (!pointer) return 0;
        if (i == PROCESS_ARG_MAX || launch->used == PROCESS_ARG_BYTES) return -K_E2BIG;
        char *text = launch->bytes + launch->used;
        int rc = process_user_string(pointer, text, PROCESS_ARG_BYTES - launch->used);
        if (rc) return rc == -K_ENAMETOOLONG ? -K_E2BIG : rc;
        out[(*count)++] = text; launch->used += strlen(text) + 1;
    }
    return -K_E2BIG;
}
int64_t process_syscall(uint64_t number, const uint64_t a[6])
{
    struct process *p = task_current_process();
    if (!p) return -K_EINVAL;
    if (number == KITOS_SYS_WAITPID) {
        if ((a[0] > 0x7FFFFFFF && a[0] != ~0ULL) || a[2] > 1) return -K_EINVAL;
        if (a[1] && vm_user_check(task_current_space(), a[1], sizeof(int), VM_WRITE)) return -K_EFAULT;
        int status;
        int rc = process_wait(a[0] == ~0ULL ? 0 : (uint32_t)a[0], &status, a[2] != 0);
        if (rc > 0 && a[1] && copy_to_user(task_current_space(), a[1], &status, sizeof(status))) return -K_EFAULT;
        return rc;
    }
    if (number == KITOS_SYS_SPAWN) {
        char path[VFS_PATH_MAX]; struct launch_arguments launch = {0};
        unsigned int argc, envc;
        int rc = process_user_path(a[0], path, sizeof(path));
        if (!rc) rc = user_vector(a[1], launch.args, &argc, &launch);
        if (!rc) rc = user_vector(a[2], launch.env, &envc, &launch);
        if (rc) return rc;
        return process_spawn(path, argc, launch.args, envc, launch.env);
    }
    if (number == KITOS_SYS_KILL) {
        if (a[0] > 0x7FFFFFFF || (a[1] != 0 && a[1] != 9)) return -K_EINVAL;
        /* Children only. General signal delivery is not part of this ABI. */
        extern int process_kill_child(uint32_t pid, bool terminate);
        return process_kill_child((uint32_t)a[0], a[1] == 9);
    }
    process_pin();
    int64_t result = number == KITOS_SYS_BRK ? heap_break(p, a[0]) : process_fd_call(p, number, a);
    process_unpin();
    return result;
}
