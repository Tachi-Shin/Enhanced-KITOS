#ifndef USER_KITOS_H
#define USER_KITOS_H
#include <uapi/terminal.h>
#include <sys/types.h>
#include <stdint.h>
#include <uapi/input.h>
#include <uapi/system.h>
#include <uapi/process.h>
#include <uapi/ipc.h>
int kitos_yield(void);
int kitos_sleep_ms(uint32_t ms);
uint64_t kitos_clock_ms(void);
pid_t kitos_spawn(const char *path, char *const argv[], char *const envp[]);
pid_t kitos_waitpid(pid_t pid, int *status, int options);
int kitos_input_read(struct kitos_input_event *events, unsigned int capacity);
int kitos_readdir(const char *, unsigned long long index, struct kitos_dirent *);
long kitos_control(unsigned int op, unsigned long a, unsigned long b);
int kitos_tty_info(struct kitos_terminal_info *);
pid_t kitos_spawn_with_fds(const char *,char *const [],char *const [],const struct kitos_fd_map *,unsigned int);
int kitos_channel(int fds[2],unsigned int flags);
int kitos_pipe(int fds[2],unsigned int flags);
int kitos_poll(struct kitos_pollfd *,unsigned int,int timeout_ms);
int kitos_listen(const char *,unsigned int backlog,unsigned int flags);
int kitos_connect(const char *,unsigned int flags);
int kitos_accept(int,unsigned int flags);
int kitos_event_create(uint64_t initial,unsigned int flags);
int kitos_shm_open(const char *,size_t,unsigned int flags);
void *kitos_shm_map(int,unsigned int flags);
int kitos_shm_unmap(void *);
int kitos_shm_unlink(const char *);
int kitos_display_acquire(void);
int kitos_display_present(int shm,unsigned int width,unsigned int height,unsigned int pitch);
int kitos_display_release(void);
#endif
