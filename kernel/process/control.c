#include "internal.h"
#include <uapi/system.h>
#include <kernel/task.h>
#include <kernel/clock.h>
#include <kernel/console.h>
#include <kernel/timer.h>
#include <kernel/printk.h>
#include <kernel/memory.h>
#include <kernel/diagnostics.h>
#include <kernel/module.h>
#include <kernel/storage.h>
#include <drivers/uart/uart.h>
#include <drivers/gpio/gpio.h>
#include <drivers/usb/input.h>
#include <drivers/usb/usb.h>
#include <kernel/error.h>
#include <block/block.h>
#include <arch/irq.h>
static unsigned int diagnostic_operation;
static bool diagnostic_busy;
static int diagnostic_job(void)
{
    switch(diagnostic_operation){
    case KITOS_CTL_SELFTEST:return kernel_regression();
    case KITOS_CTL_MEMTEST:return memory_regression();
    case KITOS_CTL_USERTEST:return user_regression();
    default:return memory_pressure_regression();
    }
}
static int run_diagnostic(unsigned int operation)
{
    uint64_t flags=arch_irq_save();
    if(diagnostic_busy){arch_irq_restore(flags);return -K_EBUSY;}
    diagnostic_busy=true;diagnostic_operation=operation;arch_irq_restore(flags);
    int rc=console_work(diagnostic_job);diagnostic_busy=false;return rc;
}
int64_t process_control(const uint64_t a[6])
{
    uint64_t op=a[0],x=a[1],y=a[2];int rc=0;char path[VFS_PATH_MAX];
    switch(op){
    case KITOS_CTL_DISPLAY:console_print_display();break;
    case KITOS_CTL_TIME:printk("ms=%llu irq=%llu\n",clock_ms(),kernel_timer_irq_count());break;
    case KITOS_CTL_PS:{
        struct task_info tasks[MAX_TASKS];unsigned int n=task_snapshot(tasks,MAX_TASKS);
        printk("PID   MODE STATE COMMAND\n");
        for(unsigned int i=0;i<n;++i)printk("%u EL%u %u %s\n",tasks[i].id,tasks[i].user?0U:1U,tasks[i].state,tasks[i].name);
        break;
    }
    case KITOS_CTL_MEM:memory_print_stats();break;
    case KITOS_CTL_UART:{struct uart_rx_stats s;uart_get_rx_stats(uart_get_device(0),&s);printk("irq=%llu rx=%llu drop=%llu overrun=%llu\n",s.interrupts,s.bytes,s.dropped,s.overruns);break;}
    case KITOS_CTL_USB:usb_print();break;
    case KITOS_CTL_INPUT:usb_input_print();break;
    case KITOS_CTL_DISKS:storage_print();break;
    case KITOS_CTL_LSMOD:module_print();break;
    case KITOS_CTL_INSMOD:case KITOS_CTL_RMMOD:case KITOS_CTL_UMOUNT:
        rc=op==KITOS_CTL_RMMOD?process_user_string(x,path,sizeof(path)):process_user_path(x,path,sizeof(path));
        if(!rc)rc=op==KITOS_CTL_INSMOD?module_load(path):op==KITOS_CTL_RMMOD?module_unload(path):vfs_unmount(path);
        break;
    case KITOS_CTL_MOUNT:
        if(x>=BLOCK_DEVICE_COUNT)return -K_EINVAL;
        rc=process_user_path(y,path,sizeof(path));if(!rc)rc=storage_mount((unsigned int)x,path);break;
    case KITOS_CTL_SELFTEST:case KITOS_CTL_MEMTEST:case KITOS_CTL_USERTEST:case KITOS_CTL_OOMTEST:
        return run_diagnostic((unsigned int)op);
    case KITOS_CTL_PROCS:process_print();break;
    case KITOS_CTL_GPIO:
        if(x>0xffffffffU||y>7)return -K_EINVAL;
        if(!y)return gpio_read((unsigned int)x);
        if(y==1||y==2)return gpio_pinMode((unsigned int)x,y==1?INPUT:OUTPUT);
        if(y==3||y==4)return gpio_write((unsigned int)x,y==3?LOW:HIGH);
        return gpio_set_pull((unsigned int)x,y==5?GPIO_PULL_UP:y==6?GPIO_PULL_DOWN:GPIO_PULL_NONE);
    case KITOS_CTL_STOP:case KITOS_CTL_RESUME:case KITOS_CTL_KILL:
        if(x>0xffffffffU)return -K_EINVAL;
        return task_control((uint32_t)x,op==KITOS_CTL_STOP?TASK_STOP:op==KITOS_CTL_RESUME?TASK_RESUME:TASK_KILL);
    case KITOS_CTL_TASK_SLEEP:
        if(x>0xffffffffU||y>0xffffffffU)return -K_EINVAL;
        return task_sleep((uint32_t)x,(uint32_t)y);
    default:return -K_EINVAL;
    }
    return rc;
}
