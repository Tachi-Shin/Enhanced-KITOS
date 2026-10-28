#include <kernel/root.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/printk.h>
#include <kernel/error.h>
void shell_init(void *unused)
{
    (void)unused;
    const char *args[]={"/bin/sh"};
    const char *env[]={"PATH=/bin:/sbin:/usr/bin:/usr/sbin","HOME=/root","TERM=xterm","USER=root"};
    for(;;){
        int pid=process_spawn("/bin/sh",1,args,4,env);
        if(pid<0){
            const char *reason=pid==-K_ENOENT?"file not found":pid==-K_ENODEV?"root filesystem unavailable":
                pid==-K_ENOEXEC?"invalid AArch64 ELF":pid==-K_ENOMEM?"out of memory":
                pid==-K_EAGAIN?"no free process/task slot":"load failed";
            printk("[init] /bin/sh: %s (error %d)\n",reason,pid);
            if(pid==-K_ENOENT)printk("[init] Install /bin and /sbin on the mounted root FAT32 volume (DATA if partition 2 is FAT32).\n");
            shell_rescue(NULL);return;
        }
        int status;int rc=process_wait((uint32_t)pid,&status,false);
        printk("[init] shell exited status=%d wait=%d; restarting\n",status,rc);
        task_sleep_ms(100);
    }
}
