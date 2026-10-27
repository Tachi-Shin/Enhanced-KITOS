#include <kernel/syscall.h>
#include <uapi/syscall.h>
#include <kernel/task.h>
#include <kernel/vm.h>
#include <kernel/console.h>
#include <kernel/clock.h>
#include <kernel/process.h>
#include <kernel/error.h>

static int64_t public_result(int64_t result)
{
    if (result >= 0) return result;
    switch (-result) {
    case K_ENAMETOOLONG: return -91;
    case K_ENOTEMPTY: return -90;
    case K_ETIMEDOUT: return -116;
    case K_ENOTSUP: return -134;
    case K_EOVERFLOW: return -139;
    case K_ECANCELED: return -140;
    case K_ENOSYS: return -KITOS_ENOSYS;
    default: return result;
    }
}
int64_t syscall_dispatch(uint64_t number, const uint64_t arguments[6])
{
    struct vm_space *space = task_current_space();
    if (!space) return -KITOS_EINVAL;
    if (task_current_process() && (number == KITOS_SYS_WRITE || (number >= KITOS_SYS_READ && number <= KITOS_SYS_KILL)))
        return public_result(process_syscall(number, arguments));
    switch (number) {
    case KITOS_SYS_EXIT:
        task_finish((int)(uint32_t)arguments[0], 0, 0, 0);
    case KITOS_SYS_WRITE: {
        if (arguments[0] != 1 && arguments[0] != 2) return -KITOS_EBADF;
        uint64_t address = arguments[1], length = arguments[2];
        if (length > KITOS_SYSCALL_WRITE_MAX) return -KITOS_EINVAL;
        if (!length) return 0;
        int rc = vm_user_check(space, address, (size_t)length, VM_READ);
        if (rc) return -KITOS_EFAULT;
        char buffer[128];
        for (size_t done = 0; done < length;) {
            size_t chunk = length - done;
            if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
            rc = copy_from_user(space, buffer, address + done, chunk);
            if (rc) return -KITOS_EFAULT;
            for (size_t i = 0; i < chunk; ++i) console_putc(buffer[i]);
            done += chunk;
        }
        return (int64_t)length;
    }
    case KITOS_SYS_YIELD:
        task_yield(); return 0;
    case KITOS_SYS_SLEEP_MS:
        if (arguments[0] > 0xFFFFFFFFULL) return -KITOS_EINVAL;
        return task_sleep_ms((uint32_t)arguments[0]) ? -KITOS_EINVAL : 0;
    case KITOS_SYS_GETPID:
        return task_current_id();
    case KITOS_SYS_CLOCK_MS: {
        uint64_t now = clock_ms();
        return copy_to_user(space, arguments[0], &now, sizeof(now)) ? -KITOS_EFAULT : 0;
    }
    default:
        return -KITOS_ENOSYS;
    }
}
