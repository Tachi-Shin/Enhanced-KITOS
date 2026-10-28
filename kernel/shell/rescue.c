#include <kernel/diagnostics.h>
#include <kernel/root.h>
#include <kernel/console.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <kernel/memory.h>
#include <kernel/process.h>
#include <fs/vfs.h>
#include <drivers/gpio/gpio.h>
#include <drivers/uart/uart.h>
#include <lib/string.h>
#include <drivers/usb/usb.h>
#include <kernel/storage.h>
#include <drivers/usb/input.h>
#include <kernel/module.h>
static char cwd[VFS_PATH_MAX] = "/";
static int path(const char *arg, char *out)
{
    if (!arg) arg = cwd;
    if(!*arg)return -K_ENOENT;
    size_t n = strlen(arg), prefix = arg[0] == '/' ? 0 : strlen(cwd);
    bool separator=prefix&&cwd[prefix-1]!='/';
    if (n + prefix + separator >= VFS_PATH_MAX) return -K_ENAMETOOLONG;
    if (prefix) memcpy(out,cwd,prefix);
    if(separator)out[prefix++]='/';
    strcpy(out + prefix, arg);
    return 0;
}
static int number(const char *s, uint32_t *value)
{
    if (!s || !*s) return -K_EINVAL;
    uint32_t n = 0;
    while (*s) {
        if (*s < '0' || *s > '9' || n > (0xFFFFFFFFU - (*s - '0')) / 10U) return -K_EINVAL;
        n = n * 10U + (uint32_t)(*s++ - '0');
    }
    *value = n; return 0;
}
static int listing(void *unused, const struct vfs_dirent *entry)
{
    (void)unused;
    printk("%c %llu %s\n", entry->kind == VFS_DIRECTORY ? 'd' : '-', entry->size, entry->name);
    return 0;
}
static int file_command(const char *command, const char *name, const char *text)
{
    if ((!strcmp(command, "write") || !strcmp(command, "append") ||
         !strcmp(command, "truncate") || !strcmp(command, "mv")) && !text) return -K_EINVAL;
    uint32_t truncate_size = 0;
    if (!strcmp(command, "truncate") && number(text, &truncate_size)) return -K_EINVAL;
    char full[VFS_PATH_MAX];
    int rc = path(name, full);
    if (rc) return rc;
    if (!strcmp(command, "ls")) return vfs_list(full, listing, NULL);
    if (!name) return -K_EINVAL;
    if (!strcmp(command, "mkdir")) return vfs_mkdir(full);
    if (!strcmp(command, "rm")) return vfs_unlink(full);
    if (!strcmp(command, "rmdir")) return vfs_rmdir(full);
    if (!strcmp(command, "mv")) {
        char target[VFS_PATH_MAX];
        if (!text || path(text, target)) return -K_EINVAL;
        return vfs_rename(full, target);
    }
    if (!strcmp(command, "cd")) {
        do{rc=vfs_resolve_directory(full,cwd);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);
        return rc;
    }
    struct file *file;
    bool read = !strcmp(command, "cat");
    unsigned int flags = read ? VFS_READ : VFS_WRITE;
    if (!read && strcmp(command, "truncate")) flags |= VFS_CREATE;
    if (!strcmp(command, "write")) flags |= VFS_TRUNC;
    if (!strcmp(command, "append")) flags |= VFS_APPEND;
    rc = vfs_open(full, flags, &file);
    if (rc) return rc;
    if (read) {
        char buffer[256]; uint32_t done;
        do {
            rc = vfs_read(file, buffer, sizeof(buffer), &done);
            for (uint32_t i = 0; !rc && i < done; ++i) console_putc(buffer[i]);
        } while (!rc && done);
        console_putc('\n');
    } else if (!strcmp(command, "truncate")) {
        rc = vfs_ftruncate(file, truncate_size);
    } else if (strcmp(command, "touch")) {
        uint32_t done;
        rc = text ? vfs_write(file, text, (uint32_t)strlen(text), &done) : -K_EINVAL;
    }
    int closed = vfs_close(file);
    return rc ? rc : closed;
}
static void execute(unsigned int argc, char **argv)
{
    if (!argc) return;
    const char *cmd = argv[0];
    int rc = 0;
    if (!strcmp(cmd, "help")) {
        printk("help | time | ps | selftest | mem | memtest | usertest | uart | display | usb | input | pwd | echo TEXT\n"
               "run PATH [ARGS...] | spawn PATH [ARGS...] | wait PID | procs\n"
               "insmod PATH | rmmod NAME | lsmod | disks | mount DISK PATH | umount PATH\n"
               "ls [PATH] | cd PATH | cat PATH | touch PATH | mkdir PATH\n"
               "write PATH TEXT | append PATH TEXT | truncate PATH BYTES\n"
               "rm PATH | rmdir PATH | mv OLD NEW (quote names with spaces)\n"
               "stop ID | resume ID | kill ID | sleep ID MS\n"
               "gpio PIN in|out|0|1|up|down|none | gpio PIN\n");
    } else if (!strcmp(cmd, "selftest")) rc = kernel_regression();
    else if (!strcmp(cmd,"display")) console_print_display();
    else if (!strcmp(cmd, "usb")) usb_print();
    else if (!strcmp(cmd, "input")) usb_input_print();
    else if (!strcmp(cmd,"disks")) storage_print();
    else if (!strcmp(cmd,"mount")&&argc==3) {uint32_t disk;char full[VFS_PATH_MAX];rc=number(argv[1],&disk);if(!rc)rc=path(argv[2],full);if(!rc)rc=storage_mount(disk,full);}
    else if (!strcmp(cmd,"umount")&&argc==2) {char full[VFS_PATH_MAX];rc=path(argv[1],full);if(!rc)rc=vfs_unmount(full);}
    else if (!strcmp(cmd,"lsmod")) module_print();
    else if (!strcmp(cmd,"insmod") && argc==2) {char full[VFS_PATH_MAX];rc=path(argv[1],full);if(!rc)rc=module_load(full);}
    else if (!strcmp(cmd,"rmmod") && argc==2) rc=module_unload(argv[1]);
    else if (!strcmp(cmd, "memtest")) rc = memory_regression();
    else if (!strcmp(cmd, "usertest")) rc = user_regression();
    else if (!strcmp(cmd, "oomtest")) rc = memory_pressure_regression();
    else if (!strcmp(cmd, "procs")) process_print();
    else if ((!strcmp(cmd, "run") || !strcmp(cmd, "spawn")) && argc >= 2) {
        char full[VFS_PATH_MAX]; rc = path(argv[1], full);
        if (!rc) {
            rc = process_spawn(full, argc - 1, (const char *const *)&argv[1], 0, NULL);
            if (rc > 0) {
                uint32_t pid = (uint32_t)rc;
                printk("[process] pid=%u\n", pid);
                if (!strcmp(cmd, "run")) {
                    int status; rc = process_wait(pid, &status, false);
                    if (rc > 0) { printk("[process] pid=%u status=%d\n", pid, status); rc = 0; }
                } else rc = 0;
            }
        }
    } else if (!strcmp(cmd, "wait") && argc == 2) {
        uint32_t pid; rc = number(argv[1], &pid);
        if (!rc) { int status; rc = process_wait(pid, &status, false);
            if (rc > 0) { printk("[process] pid=%u status=%d\n", pid, status); rc = 0; } }
    }
    else if (!strcmp(cmd, "mem")) memory_print_stats();
    else if (!strcmp(cmd, "time")) printk("ms=%llu irq=%llu\n", clock_ms(), kernel_timer_irq_count());
    else if (!strcmp(cmd, "pwd")) printk("%s\n", cwd);
    else if (!strcmp(cmd, "echo")) printk("%s\n", argc > 1 ? argv[1] : "");
    else if (!strcmp(cmd, "uart")) {
        struct uart_rx_stats stats;
        uart_get_rx_stats(uart_get_device(0), &stats);
        printk("irq=%llu rx=%llu drop=%llu overrun=%llu\n", stats.interrupts, stats.bytes, stats.dropped, stats.overruns);
    } else if (!strcmp(cmd, "ps")) {
        struct task_info tasks[MAX_TASKS];
        unsigned int count = task_snapshot(tasks, MAX_TASKS);
        for (unsigned int i = 0; i < count; ++i)
            printk("%u %s EL%u state=%u exit=%d ESR=%llx stack=%p..%p\n", tasks[i].id, tasks[i].name,
                   tasks[i].user ? 0U : 1U, tasks[i].state, tasks[i].exit_status, tasks[i].fault_esr,
                   tasks[i].stack_bottom, tasks[i].stack_top);
    } else if (!strcmp(cmd, "gpio")) {
        uint32_t pin;
        rc = argc > 1 ? number(argv[1], &pin) : -K_EINVAL;
        if (!rc && argc == 2) { rc = gpio_read(pin); if (rc >= 0) { printk("%d\n", rc); rc = 0; } }
        else if (!rc && argc == 3) {
            if (!strcmp(argv[2], "in")) rc = gpio_pinMode(pin, INPUT);
            else if (!strcmp(argv[2], "out")) rc = gpio_pinMode(pin, OUTPUT);
            else if (!strcmp(argv[2], "0")) rc = gpio_write(pin, LOW);
            else if (!strcmp(argv[2], "1")) rc = gpio_write(pin, HIGH);
            else if (!strcmp(argv[2], "up")) rc = gpio_set_pull(pin, GPIO_PULL_UP);
            else if (!strcmp(argv[2], "down")) rc = gpio_set_pull(pin, GPIO_PULL_DOWN);
            else if (!strcmp(argv[2], "none")) rc = gpio_set_pull(pin, GPIO_PULL_NONE);
            else rc = -K_EINVAL;
        }
    } else if (!strcmp(cmd, "stop") || !strcmp(cmd, "resume") || !strcmp(cmd, "kill") || !strcmp(cmd, "sleep")) {
        uint32_t id, ms;
        rc = argc > 1 ? number(argv[1], &id) : -K_EINVAL;
        if (!rc && !strcmp(cmd, "sleep")) {
            rc = argc == 3 ? number(argv[2], &ms) : -K_EINVAL;
            if (!rc) rc = task_sleep(id, ms);
        } else if (!rc) rc = task_control(id, !strcmp(cmd, "stop") ? TASK_STOP : !strcmp(cmd, "resume") ? TASK_RESUME : TASK_KILL);
    } else {
        const char *commands[] = {"ls", "cd", "cat", "touch", "mkdir", "write", "append", "truncate", "rm", "rmdir", "mv"};
        rc = -K_EINVAL;
        for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
            if (!strcmp(cmd, commands[i])) {
                rc = file_command(cmd, argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL); break;
            }
    }
    if (rc) printk("error %d\n", rc);
}
/* Recovery shell tokenizer: single/double quotes, no expansion or scripting. */
static int tokenize(char *line, char **args, unsigned int capacity)
{
    unsigned int n = 0; char *read = line, *write = line;
    while (*read) {
        while (*read == ' ' || *read == '\t') ++read;
        if (!*read) break;
        if (n == capacity) return -1;
        args[n++] = write;
        char quote = 0;
        while (*read && (quote || (*read != ' ' && *read != '\t'))) {
            if (*read == '\'' || *read == '"') {
                if (!quote) { quote = *read++; continue; }
                if (quote == *read) { quote = 0; ++read; continue; }
            }
            *write++ = *read++;
        }
        if (quote) return -1;
        if (*read) ++read;
        *write++ = 0;
    }
    return (int)n;
}
void shell_rescue(void *unused)
{
    (void)unused;
    char line[1024]; unsigned int n = 0; bool discard = false, was_cr = false;
    printk("[shell] recovery mode; /bin/sh unavailable\nroot@ekitos:/# ");
    for (;;) {
        char c; int rc = console_try_getc(&c);
        if (!rc) { task_sleep_ms(1); continue; }
        if (rc < 0) { n = 0; discard = true; continue; }
        if (c == '\n' && was_cr) { was_cr = false; continue; }
        was_cr = c == '\r';
        if (c == '\r' || c == '\n') {
            line[n] = 0; console_putc('\n');
            char *args[PROCESS_ARG_MAX + 1]; int argc = tokenize(line, args, PROCESS_ARG_MAX + 1);
            if (discard || argc < 0) printk("error: input lost, too long, or invalid quoting\n");
            else execute((unsigned int)argc, args);
            n = 0; discard = false; printk("root@ekitos:%s# ", cwd);
        } else if (c == 127 || c == '\b') { if (n) { --n; console_write("\b \b"); } }
        else if ((unsigned char)c >= 32) {
            if (n + 1 >= sizeof(line)) discard = true;
            else { line[n++] = c; console_putc(c); }
        }
    }
}
