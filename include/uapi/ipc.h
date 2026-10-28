#ifndef UAPI_IPC_H
#define UAPI_IPC_H
#define KITOS_IPC_NONBLOCK 0x100U
#define KITOS_IPC_CLOEXEC 0x200U
#define KITOS_IPC_CREATE 1U
#define KITOS_IPC_EXCL 2U
#define KITOS_IPC_READONLY 4U
#define KITOS_EVENT_SEMAPHORE 1U
#define KITOS_POLLIN 1U
#define KITOS_POLLOUT 2U
#define KITOS_POLLERR 4U
#define KITOS_POLLHUP 8U
#define KITOS_POLLNVAL 16U
#define KITOS_PIPE_BUF 4096U
#define KITOS_MESSAGE_MAX 4096U
#define KITOS_SHM_MAX (64U*1024*1024)
#define KITOS_F_GETFL 1U
#define KITOS_F_SETFL 2U
#define KITOS_F_GETFD 3U
#define KITOS_F_SETFD 4U
struct kitos_pollfd {int fd;unsigned int events,revents;};
struct kitos_fd_map {int source,target;};
/* spawn_ex inherits only standard FDs plus explicit mappings. Other parent
 * descriptors remain closed; mappings act as dup and clear CLOEXEC. */
struct kitos_spawn_spec {
    unsigned long long path,argv,envp,maps;
    unsigned int map_count,flags;
};
_Static_assert(sizeof(struct kitos_spawn_spec)==40,"spawn extended wire ABI");
#endif
