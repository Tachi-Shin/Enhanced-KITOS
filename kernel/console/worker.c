#include <kernel/console.h>
#include <kernel/task.h>
#include <kernel/error.h>
#include <arch/irq.h>
static int (*pending)(void);
static volatile bool busy,done,started;
static int result;
void console_worker(void *unused)
{
    (void)unused;started=true;
    for(;;){
        if(pending){int (*job)(void)=pending;pending=NULL;result=job();done=true;}
        (void)console_input_ready();console_poll();task_sleep_ms(1);
    }
}
int console_work(int (*job)(void))
{
    if(!job)return -K_EINVAL;
    uint64_t flags=arch_irq_save();
    if(!started||busy){arch_irq_restore(flags);return -K_EBUSY;}
    busy=true;done=false;pending=job;arch_irq_restore(flags);
    while(!done)task_sleep_ms(1);
    int rc=result;busy=false;return rc;
}
