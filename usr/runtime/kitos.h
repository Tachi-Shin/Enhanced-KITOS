#ifndef USER_KITOS_H
#define USER_KITOS_H
#include <sys/types.h>
#include <stdint.h>
int kitos_yield(void);
int kitos_sleep_ms(uint32_t ms);
uint64_t kitos_clock_ms(void);
pid_t kitos_spawn(const char *path, char *const argv[], char *const envp[]);
pid_t kitos_waitpid(pid_t pid, int *status, int options);
#endif
