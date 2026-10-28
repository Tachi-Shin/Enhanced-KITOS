#include <kernel/syscall.h>
#include <uapi/syscall.h>
#include <kernel/task.h>
#include <kernel/vm.h>
#include <kernel/console.h>
#include <kernel/clock.h>
#include <kernel/process.h>
#include <kernel/error.h>
#include <drivers/usb/input.h>

static int64_t public_result(int64_t result)
{
    if (result >= 0) return result;
    switch (-result) {
    case K_ENAMETOOLONG: return -KITOS_ENAMETOOLONG;
    case K_ENOTEMPTY: return -KITOS_ENOTEMPTY;
    case K_ETIMEDOUT: return -KITOS_ETIMEDOUT;
    case K_ENOTSUP: return -KITOS_ENOTSUP;
    case K_EOVERFLOW: return -KITOS_EOVERFLOW;
    case K_ECANCELED: return -KITOS_ECANCELED;
    case K_ENOSYS: return -KITOS_ENOSYS;
    case K_EMSGSIZE:return -KITOS_EMSGSIZE;
    default: return result;
    }
}
int64_t syscall_dispatch(uint64_t number, const uint64_t arguments[6])
{
    struct vm_space *space = task_current_space();
    if (!space) return -KITOS_EINVAL;
    if (task_current_process() && (number == KITOS_SYS_WRITE || (number >= KITOS_SYS_READ && number <= KITOS_SYS_KILL) ||
        (number >= KITOS_SYS_READDIR && number <= KITOS_SYS_CONTROL)||
        (number >= KITOS_SYS_PIPE && number <= KITOS_SYS_DISPLAY)))
        return public_result(process_syscall(number, arguments));
    switch (number) {
    case KITOS_SYS_TTY_INFO:{
        struct kitos_terminal_info info;console_info(&info);
        return copy_to_user(space,arguments[0],&info,sizeof(info))?-KITOS_EFAULT:0;
    }
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
    case KITOS_SYS_INPUT_READ: {
        if (arguments[1] > KITOS_INPUT_BATCH_MAX) return -KITOS_EINVAL;
        if (!arguments[1]) return 0;
        size_t size = (size_t)arguments[1] * sizeof(struct kitos_input_event);
        if (vm_user_check(space, arguments[0], size, VM_WRITE)) return -KITOS_EFAULT;
        struct kitos_input_event events[KITOS_INPUT_BATCH_MAX];
        unsigned int count = usb_input_read(events, (unsigned int)arguments[1]);
        if (copy_to_user(space, arguments[0], events, count * sizeof(events[0]))) return -KITOS_EFAULT;
        return count;
    }
    default:
        return -KITOS_ENOSYS;
    }
}
