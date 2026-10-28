#include <platform/platform.h>
#include <drivers/uart/uart.h>
#include <block/block.h>
#include <fs/fat32.h>
#include <fs/fat.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/printk.h>
#include <kernel/irq.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/root.h>
#include <kernel/memory.h>
#include <kernel/boot_memory.h>
#include <kernel/process.h>
#include <arch/exception.h>
#include <arch/irq.h>
#include <platform/irq.h>
#include <drivers/usb/usb.h>
#include <kernel/module.h>
static bool uart_irq(void *context)
{
    struct uart_device *uart = context;
    uart_handle_irq(uart->irq_intid);
    return false;
}
void main(uint64_t dtb_physical)
{
    struct boot_memory boot;
    int memory_status=platform_boot_info(dtb_physical,&boot);
    extern uint64_t _bss_start[], _bss_end[];
    for (uint64_t *p = _bss_start; p < _bss_end; ++p) *p = 0;
    arch_exception_init();
    if (!memory_status) memory_status = memory_init(&boot,dtb_physical);
    if (!memory_status) memory_status = task_init_stacks();
    if (memory_status) goto halt;
    if (module_bootstrap()) goto halt;
    struct uart_device *uart = platform_uart();
    if (!uart) goto halt;
    console_bind(uart_char_device(uart));
    printk("Enhanced KITOS [%s] modular drivers; shell\n", platform_name());
    memory_boot_print();
    irq_controller_init();
    if (irq_register(uart->irq_intid, uart_irq, uart) ||
        irq_register(platform_timer_irq(), kernel_time_irq, NULL)) goto halt;
    irq_controller_enable(uart->irq_intid, 8U);
    if (uart_enable_rx_irq(uart)) goto halt;
    printk("[console] %s RX irq=%u; FIFO polling fallback enabled\n",uart->name,uart->irq_intid);
    struct block_device *storage = platform_storage();
    if (storage && !block_register(storage) && !block_init(storage) &&
        !vfs_register(&fat32_type) && !vfs_mount("fat32", "/", storage, NULL))
        printk("[storage] FAT32 ready via VFS blocks=%llu\n", storage->block_count);
    else printk("[storage] unavailable; shell remains available\n");
    (void)vfs_register(&fat_type);
    int id = task_create("reaper", process_reaper, NULL);
    if (id < 0 || task_protect((uint32_t)id)) goto halt;
    id=task_create("drivers",module_autoload,NULL);
    if(id<0 || task_protect((uint32_t)id))goto halt;
    irq_controller_enable(platform_timer_irq(), 10U);
    kernel_time_start();
    task_start();
halt:
    for (;;) arch_wait_for_irq();
}
