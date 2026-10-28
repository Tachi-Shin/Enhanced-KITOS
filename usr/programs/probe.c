#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kitos.h"
#include "arch_syscall.h"
#include <uapi/syscall.h>
extern int fp_probe(unsigned long seed);
extern int process_limits(int argc, char **argv);
static volatile unsigned char bss[17000];
static volatile int initialized = 12345;
static void failed(int line)
{ fprintf(stderr, "[newlib] FAIL line=%d errno=%d\n", line, errno); exit(101); }
#define CHECK(x) do { if (!(x)) failed(__LINE__); } while (0)
static void goodbye(void) { puts("[newlib] atexit PASS"); }
static void input_test(void)
{
    struct kitos_input_event events[KITOS_INPUT_BATCH_MAX];
    CHECK(kitos_input_read(NULL, 0) == 0);
    errno = 0;
    CHECK(kitos_input_read(events, KITOS_INPUT_BATCH_MAX + 1) == -1 && errno == EINVAL);
    while (kitos_input_read(events, KITOS_INPUT_BATCH_MAX) > 0) {}
    uint64_t start = kitos_clock_ms();
    puts("[user-input] ready"); fflush(stdout);
    while (kitos_clock_ms() - start < 3000) {
        CHECK(kitos_raw_syscall(KITOS_SYS_INPUT_READ, -1, 1, 0) == -EFAULT);
        int count = kitos_input_read(events, KITOS_INPUT_BATCH_MAX); CHECK(count >= 0);
        for (int i = 0; i < count; ++i) {
            struct kitos_input_event *e = &events[i];
            CHECK(!e->reserved && e->time_ms >= start);
            if (e->type == KITOS_INPUT_POINTER && e->code == 1 && e->x == 13 && e->y == -9) {
                puts("[user-input] mouse/uaccess/batch PASS"); return;
            }
        }
        CHECK(kitos_sleep_ms(1) == 0);
    }
    failed(__LINE__);
}
static void file_test(void)
{
    FILE *f = fopen("/USER.TXT", "w+"); CHECK(f);
    CHECK(fprintf(f, "Newlib file IO %d\n", 42) > 0);
    CHECK(fflush(f) == 0);
    struct stat st; CHECK(fstat(fileno(f), &st) == 0 && st.st_size == 18 && S_ISREG(st.st_mode));
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    char text[64]; CHECK(fgets(text, sizeof(text), f) && !strcmp(text, "Newlib file IO 42\n"));
    CHECK(fclose(f) == 0);
    CHECK(stat("/USER.TXT", &st) == 0 && st.st_size == 18);
    errno = 0; CHECK(!fopen("/NOTHERE.TXT", "r") && errno == ENOENT);
    errno = 0; CHECK(write(99, "x", 1) == -1 && errno == EBADF);
    int fd = open("USER.TXT", O_RDONLY); CHECK(fd >= 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 0);
    CHECK(kitos_raw_syscall(KITOS_SYS_READ, fd, -1, 4) == -EFAULT);
    CHECK(lseek(fd, 0, SEEK_CUR) == 0 && close(fd) == 0);
    CHECK(kitos_raw_syscall(KITOS_SYS_SPAWN, -1, 0, 0) == -EFAULT);
    CHECK(isatty(1));
    puts("[newlib] stdio/seek/stat/errno PASS");
}
static void memory_test(void)
{
    CHECK(initialized == 12345);
    for (size_t i = 0; i < sizeof(bss); ++i) CHECK(bss[i] == 0);
    unsigned char *a = calloc(1, 65536); CHECK(a);
    for (size_t i = 0; i < 65536; ++i) CHECK(a[i] == 0);
    memset(a, 0x5A, 65536);
    a = realloc(a, 1024 * 1024); CHECK(a);
    for (size_t i = 0; i < 65536; ++i) CHECK(a[i] == 0x5A);
    memset(a, 0x77, 1024 * 1024); free(a);
    errno = 0;
    void *huge = malloc(32U * 1024U * 1024U);
    CHECK(!huge && errno == ENOMEM);
    puts("[newlib] BSS/calloc/realloc/free/ENOMEM PASS");
}
static pid_t child(const char *mode)
{
    char *args[] = {"/PROBE.ELF", (char *)mode, NULL};
    char *env[] = {"KITOS_CHILD=yes", NULL};
    pid_t pid = kitos_spawn(args[0], args, env); CHECK(pid > 0); return pid;
}
static void family_test(void)
{
    int status;
    pid_t pid = child("child");
    CHECK(kitos_raw_syscall(KITOS_SYS_WAITPID, pid, 0x400000, 0) == -EFAULT);
    CHECK(kitos_waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 37);
    errno = 0; CHECK(kitos_waitpid(pid, &status, 0) == -1 && errno == ECHILD);
    CHECK(unlink("/LEAK.TXT") == 0); /* Exit must close the inherited open handle. */
    pid = child("hold"); CHECK(kitos_waitpid(pid, &status, 1) == 0); CHECK(kitos_sleep_ms(30) == 0);
    CHECK(kill(pid, 9) == 0);
    CHECK(kitos_waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == 9);
    CHECK(unlink("/LEAK.TXT") == 0);
    pid = child("fault");
    CHECK(kitos_waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == 11);
    CHECK(unlink("/LEAK.TXT") == 0);
    pid = child("orphan");
    CHECK(kitos_waitpid(pid, &status, 0) == pid && status == 0);
    CHECK(kitos_sleep_ms(30) == 0);
    CHECK(unlink("/LEAK.TXT") == 0);
    puts("[newlib] spawn/envp/wait/kill/fault/orphan/FD-cleanup PASS");
}
static void usb_unplug_test(const char *path)
{
    int fd=open(path,O_RDONLY);CHECK(fd>=0);
    puts("[user-usb] ready");fflush(stdout);
    CHECK(kitos_sleep_ms(800)==0);
    char byte;errno=0;CHECK(read(fd,&byte,1)==-1&&errno==ENODEV);
    CHECK(close(fd)==0);puts("[user-usb] unplug/ENODEV/close PASS");
}
static void usb_large_test(const char *path)
{
    CHECK(sizeof(off_t)>=8);int fd=open(path,O_RDWR);CHECK(fd>=0);
    const off_t offset=(off_t)0x100000011LL;char bytes[14];
    CHECK(lseek(fd,offset,SEEK_SET)==offset);CHECK(read(fd,bytes,sizeof(bytes))==sizeof(bytes));
    CHECK(!memcmp(bytes,"UPDATED-4GIB!!",sizeof(bytes)));
    CHECK(lseek(fd,offset,SEEK_SET)==offset);CHECK(write(fd,"EL0-above-4GiB",14)==14);CHECK(close(fd)==0);
    fd=open(path,O_RDONLY);CHECK(fd>=0);CHECK(lseek(fd,offset,SEEK_SET)==offset);
    CHECK(read(fd,bytes,14)==14&&!memcmp(bytes,"EL0-above-4GiB",14));CHECK(close(fd)==0);
    puts("[user-usb] exFAT seek/read/write above 4GiB PASS");
}
int main(int argc, char **argv)
{
    CHECK(argc >= 1 && argv[argc] == NULL);
    if(argc==3&&!strcmp(argv[1],"usb-large")){usb_large_test(argv[2]);return 0;}
    if(argc==3&&!strcmp(argv[1],"usb-unplug")){usb_unplug_test(argv[2]);return 0;}
    if (argc > 1 && !strcmp(argv[1], "input")) { input_test(); return 0; }
    if (argc > 1 && !strncmp(argv[1], "limits", 6)) return process_limits(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "orphan")) { child("child"); return 0; }
    if (argc > 1 && (!strcmp(argv[1], "child") || !strcmp(argv[1], "hold") || !strcmp(argv[1], "fault"))) {
        CHECK(getenv("KITOS_CHILD") && !strcmp(getenv("KITOS_CHILD"), "yes"));
        FILE *leak = fopen("/LEAK.TXT", "w"); CHECK(leak);
        CHECK(fputs("close on termination", leak) >= 0 && fflush(leak) == 0);
        CHECK(malloc(65536));
        if (!strcmp(argv[1], "child")) return 37;
        if (!strcmp(argv[1], "fault")) {
            __asm__ volatile("mov x9, xzr; str xzr, [x9]" ::: "x9", "memory");
        }
        for (;;) __asm__ volatile("nop");
    }
    unsigned int seed = argc > 2 ? (unsigned int)atoi(argv[2]) : 1;
    if (argc > 1 && !strcmp(argv[1], "fp")) {
        CHECK(fp_probe(seed)); printf("[newlib] FP/SIMD seed=%u PASS\n", seed); return 0;
    }
    CHECK(argc == 2 && !strcmp(argv[1], "hello"));
    printf("[newlib] ELF argc=%d argv=%s pid=%d float=%.3f\n", argc, argv[1], getpid(), 3.125);
    CHECK(atexit(goodbye) == 0);
    memory_test(); file_test(); family_test();
    CHECK(fp_probe(3));
    puts("[newlib] ELF/Newlib PASS");
    return 0;
}
