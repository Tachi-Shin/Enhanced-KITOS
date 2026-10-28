#include <errno.h>
#include <dos/config.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uapi/syscall.h>
#include "arch_syscall.h"
#include "kitos.h"

static const char executable[] = "/PROBE.ELF";
static void failed(int line)
{
    fprintf(stderr, "[limits] FAIL line=%d errno=%d\n", line, errno);
    exit(102);
}
#define CHECK(x) do { if (!(x)) failed(__LINE__); } while (0)

static pid_t launch(const char *mode)
{
    char *args[] = {(char *)executable, (char *)mode, NULL};
    pid_t pid = kitos_spawn(executable, args, NULL);
    CHECK(pid > 0);
    return pid;
}
static void wait_status(pid_t pid, int expected)
{
    int status = -1;
    CHECK(kitos_waitpid(pid, &status, 0) == pid && status == expected);
}
static void busy_unlink(void)
{
    int fd = open("/BUSY.TXT", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    CHECK(fd >= 0);
    errno = 0;
    CHECK(unlink("/BUSY.TXT") == -1 && errno == EBUSY);
    CHECK(close(fd) == 0 && unlink("/BUSY.TXT") == 0);
    puts("[limits] busy unlink PASS");
}
static void task_slots(void)
{
    /* Repeat saturation to exercise rollback and reuse, including zombie slots. */
    for (unsigned int cycle = 0; cycle < 2; ++cycle) {
        pid_t children[MAX_TASKS]; unsigned int count = 0;
        char *args[] = {(char *)executable, "limits-idle", NULL};
        while (count < MAX_TASKS) {
            pid_t pid = kitos_spawn(executable, args, NULL);
            if (pid < 0) { CHECK(errno == EAGAIN); break; }
            children[count++] = pid;
        }
        CHECK(count >= 2 && count < MAX_TASKS);
        for (unsigned int i = 0; i < count; ++i) CHECK(kill(children[i], 9) == 0);
        for (unsigned int i = 0; i < count; ++i) wait_status(children[i], 9);
    }
    puts("[limits] task saturation/reuse PASS");
}
static void file_limits(void)
{
    int files[16]; unsigned int count = 0;
    while (count < 16) {
        int fd = open("/HELLO.TXT", O_RDONLY);
        if (fd < 0) { CHECK(errno == EMFILE); break; }
        files[count++] = fd;
    }
    CHECK(count == 13); /* stdin/stdout/stderr occupy the other process FDs. */
    wait_status(launch("limits-files"), 0);
    for (unsigned int i = 0; i < count; ++i) CHECK(close(files[i]) == 0);
    int fd = open("/HELLO.TXT", O_RDONLY);
    CHECK(fd >= 0 && close(fd) == 0);
    puts("[limits] EMFILE/ENFILE/close/reuse PASS");
}
static void arguments(void)
{
    char *args[18], *env[18];
    for (unsigned int i = 0; i < 17; ++i) {
        args[i] = "with spaces 日本語";
        env[i] = "BOUNDARY=ok";
    }
    args[0] = (char *)executable; args[1] = "limits-argv";
    args[16] = env[16] = NULL;
    pid_t pid = kitos_spawn(executable, args, env); CHECK(pid > 0); wait_status(pid, 0);
    args[16] = "extra"; args[17] = NULL;
    errno = 0; CHECK(kitos_spawn(executable, args, env) == -1 && errno == E2BIG);
    args[16] = NULL; env[16] = "EXTRA=yes"; env[17] = NULL;
    errno = 0; CHECK(kitos_spawn(executable, args, env) == -1 && errno == E2BIG);
    CHECK(kitos_raw_syscall(KITOS_SYS_SPAWN, (long)executable, -1, 0) == -EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_SPAWN, (long)executable, (long)args, -1) == -EFAULT);
    char bytes[4097]; memset(bytes, 'x', sizeof(bytes));
    char *large[] = {(char *)executable, "limits-bytes", bytes, NULL};
    size_t length = 4096 - strlen(large[0]) - strlen(large[1]) - 3;
    bytes[length] = 0;
    pid = kitos_spawn(executable, large, NULL); CHECK(pid > 0); wait_status(pid, 0);
    bytes[length] = 'x'; bytes[length + 1] = 0;
    errno = 0; CHECK(kitos_spawn(executable, large, NULL) == -1 && errno == E2BIG);
    puts("[limits] argv/envp/auxv/count/bytes/uaccess PASS");
}
static void heap(void)
{
    long base = kitos_raw_syscall(KITOS_SYS_BRK, 0, 0, 0);
    CHECK(base > 0 && !(base & 4095));
    errno = 0; CHECK(sbrk(-base) == (void *)-1 && errno == ENOMEM);
    long end = base + 16U * 1024U * 1024U;
    long result = kitos_raw_syscall(KITOS_SYS_BRK, end, 0, 0);
    CHECK(result == end || result == -ENOMEM);
    if (result == -ENOMEM) {
        CHECK(kitos_raw_syscall(KITOS_SYS_BRK, 0, 0, 0) == base);
        /* OOM must release partial allocations so a smaller retry can succeed. */
        end = base + 1024U * 1024U;
        CHECK(kitos_raw_syscall(KITOS_SYS_BRK, end, 0, 0) == end);
    }
    for (uintptr_t p = (uintptr_t)base; p < (uintptr_t)end; p += 4096) {
        CHECK(*(volatile unsigned char *)p == 0);
        *(volatile unsigned char *)p = 0x5A;
    }
    CHECK(kitos_raw_syscall(KITOS_SYS_BRK, base, 0, 0) == base);
    CHECK(kitos_raw_syscall(KITOS_SYS_BRK, base + 4096, 0, 0) == base + 4096);
    CHECK(*(volatile unsigned char *)(uintptr_t)base == 0);
    CHECK(kitos_raw_syscall(KITOS_SYS_BRK, base, 0, 0) == base);
}
int process_limits(int argc, char **argv)
{
    const char *mode = argv[1];
    if (!strcmp(mode, "limits-idle")) for (;;) CHECK(kitos_sleep_ms(1000)==0);
    if (!strcmp(mode, "limits-input")) {
        char c;
        CHECK(read(0, &c, 1) == 1);
        return 103; /* Parent kills the child while read is blocked. */
    }
    if (!strcmp(mode, "limits-files")) {
        int files[16]; unsigned int count = 0;
        while (count < 16) {
            int fd = open("/HELLO.TXT", O_RDONLY);
            if (fd < 0) { CHECK(errno == ENFILE); break; }
            files[count++] = fd;
        }
        CHECK(count == 3);
        for (unsigned int i = 0; i < count; ++i) CHECK(close(files[i]) == 0);
        return 0;
    }
    if (!strcmp(mode, "limits-argv")) {
        CHECK(argc == 16 && argv[argc] == NULL);
        for (unsigned int i = 2; i < 16; ++i) CHECK(!strcmp(argv[i], "with spaces 日本語"));
        for (unsigned int i = 0; i < 16; ++i) CHECK(environ[i] && !strcmp(environ[i], "BOUNDARY=ok"));
        CHECK(environ[16] == NULL);
        const uint64_t *aux = (const uint64_t *)&environ[17];
        CHECK(aux[0] == 6 && aux[1] == 4096 && aux[2] == 9 && aux[3] == 0x400000 && !aux[4] && !aux[5]);
        return 0;
    }
    if (!strcmp(mode, "limits-bytes")) {
        CHECK(argc == 3 && strlen(argv[0]) + strlen(argv[1]) + strlen(argv[2]) + 3 == 4096);
        return 0;
    }
    if (!strcmp(mode, "limits-heap")) { heap(); return 0; }
    if (!strcmp(mode, "limits-busy")) { busy_unlink(); return 0; }
    if (!strcmp(mode, "limits-slots")) { task_slots(); return 0; }
    CHECK(!strcmp(mode, "limits"));
    busy_unlink(); task_slots(); file_limits(); arguments();
    pid_t pid = launch("limits-input"); CHECK(kitos_sleep_ms(30) == 0);
    CHECK(kill(pid, 9) == 0); wait_status(pid, 9);
    puts("[limits] blocked console read/kill PASS");
    wait_status(launch("limits-heap"), 0);
    puts("[limits] brk/zero-fill/shrink/OOM-retry PASS");
    puts("[limits] process boundaries PASS");
    return 0;
}
