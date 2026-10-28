#include <kernel/diagnostics.h>
#include <kernel/vm.h>
#include <kernel/task.h>
#include <kernel/clock.h>
#include <kernel/timer.h>
#include <kernel/printk.h>
#include <kernel/error.h>
#include <arch/memory.h>
#include <lib/string.h>
#include "user/layout.h"

extern const uint8_t user_probe_start[], user_probe_end[];
static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printk("[test] memory line=%u FAIL\n", (unsigned int)__LINE__); rc = -1; goto done; } } while (0)

int memory_regression(void)
{
    checks = 0;
    int rc = 0;
    struct vm_space a = {0}, b = {0};
    struct page_stats before, after;
    task_reap(); page_get_stats(&before);
    CHECK(arch_memory_check() == 0);
    CHECK(vm_space_create(&a) == 0 && vm_space_create(&b) == 0);
    CHECK(vm_map_new(&a, 0, VM_READ | VM_WRITE, NULL, 0) == -K_EINVAL);
    CHECK(vm_map_new(&a, 0x10000, VM_READ | VM_WRITE | VM_EXEC, NULL, 0) == -K_EINVAL);
    CHECK(vm_map_new(&a, 0x10001, VM_READ, NULL, 0) == -K_EINVAL);
    CHECK(vm_map_new(&a, 0x10000, VM_READ | VM_WRITE, NULL, 0) == 0);
    CHECK(vm_map_new(&a, 0x11000, VM_READ | VM_WRITE, NULL, 0) == 0);
    CHECK(vm_map_new(&b, 0x10000, VM_READ | VM_WRITE, NULL, 0) == 0);
    struct vm_mapping ma, mb;
    CHECK(vm_query(&a, 0x10000, &ma) == 0 && vm_query(&b, 0x10000, &mb) == 0);
    CHECK(ma.physical != mb.physical);
    for (unsigned int i = 0; i < PAGE_SIZE; ++i) CHECK(((uint8_t *)arch_phys_to_virt(ma.physical))[i] == 0);
    uint8_t bytes[32], copied[32];
    for (unsigned int i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i + 1);
    CHECK(copy_to_user(&a, 0x10FF0, bytes, sizeof(bytes)) == 0);
    CHECK(copy_from_user(&a, copied, 0x10FF0, sizeof(copied)) == 0);
    CHECK(memcmp(bytes, copied, sizeof(bytes)) == 0);
    CHECK(((uint8_t *)arch_phys_to_virt(mb.physical))[PAGE_SIZE - 1] == 0);
    memset(copied, 0x77, sizeof(copied));
    CHECK(copy_from_user(&a, copied, 0x11FF0, sizeof(copied)) == -K_EFAULT);
    for (unsigned int i = 0; i < sizeof(copied); ++i) CHECK(copied[i] == 0x77);
    CHECK(copy_to_user(&a, 0x11FF0, bytes, sizeof(bytes)) == -K_EFAULT);
    CHECK(vm_query(&a, 0x11FF0, &ma) == 0);
    CHECK(*(uint8_t *)arch_phys_to_virt(ma.physical) == 0);
    CHECK(copy_from_user(&a, copied, ~0ULL - 3, 8) == -K_EFAULT);
    CHECK(copy_to_user(&a, KERNEL_VA_BASE, bytes, 4) == -K_EFAULT);
    CHECK(vm_map_new(&a, 0x20000, VM_READ | VM_EXEC, user_probe_start, 64) == 0);
    CHECK(copy_to_user(&a, 0x20000, bytes, sizeof(bytes)) == -K_EFAULT);
    CHECK(vm_user_check(&a, 0x20000, 4, VM_EXEC) == 0);
    CHECK(vm_user_check(&a, 0x10000, 4, VM_EXEC) == -K_EFAULT);
    CHECK(vm_map_new(&a, 0x10000, VM_READ, NULL, 0) == -K_EEXIST);
    CHECK(vm_map_new(&a, ARCH_USER_END - PAGE_SIZE, VM_READ | VM_WRITE, NULL, 0) == 0);
    CHECK(copy_to_user(&a, ARCH_USER_END - 1, bytes, 1) == 0);
    CHECK(copy_to_user(&a, ARCH_USER_END - 1, bytes, 2) == -K_EFAULT);
    CHECK(vm_unmap(&a, 0x10000) == 0);
    CHECK(vm_query(&a, 0x10000, &ma) == -K_EFAULT);
    CHECK(vm_unmap(&a, 0x10000) == -K_ENOENT);
    CHECK(vm_map_new(&a, 0x10000, VM_READ | VM_WRITE, NULL, 0) == 0);
    CHECK(vm_query(&a, 0x10000, &ma) == 0);
    for (unsigned int i = 0; i < PAGE_SIZE; ++i) CHECK(((uint8_t *)arch_phys_to_virt(ma.physical))[i] == 0);
    CHECK(vm_unmap(&a, ARCH_USER_END - PAGE_SIZE) == 0);
done:
    vm_space_destroy(&a); vm_space_destroy(&b);
    page_get_stats(&after);
    if (after.free != before.free) rc = -1;
    printk("[test] MMU/page/VM/uaccess/reclaim %s checks=%u free=%llu\n", rc ? "FAIL" : "PASS", checks, after.free);
    return rc;
}

/* Run with the small-memory DTB fixture. Bound the pressure test to 16 MiB;
 * never try to consume all RAM on an ordinary hardware or QEMU boot. */
int memory_pressure_regression(void)
{
    struct page_stats before, stats;
    struct vm_space space = {0};
    task_reap(); page_get_stats(&before);
    if (before.free > 4096 || before.free < 16) return -K_EINVAL;
    int rc = vm_space_create(&space);
    if (rc) return rc;
    uint64_t held = 0, page;
    while ((page = page_alloc()) != 0) {
        *(uint64_t *)arch_phys_to_virt(page) = held; held = page;
    }
    for (unsigned int available = 0; available <= 4; ++available) {
        page_get_stats(&stats);
        if (stats.free != available) { rc = -1; break; }
        int mapped = vm_map_new(&space, 0x100000000ULL, VM_READ | VM_WRITE, NULL, 0);
        if (available < 4) {
            if (mapped != -K_ENOMEM) { rc = -1; break; }
        } else if (mapped || vm_unmap(&space, 0x100000000ULL)) { rc = -1; break; }
        page_get_stats(&stats);
        if (stats.free != available) { rc = -1; break; }
        if (available != 4) {
            page = held; held = *(uint64_t *)arch_phys_to_virt(page);
            if (page_free(page)) { rc = -1; break; }
        }
    }
    while (held) {
        page = held; held = *(uint64_t *)arch_phys_to_virt(page);
        if (page_free(page)) rc = -1;
    }
    vm_space_destroy(&space);
    page_get_stats(&stats);
    if (stats.free != before.free) rc = -1;
    printk("[test] page/VM OOM rollback %s free=%llu baseline=%llu\n",
           rc ? "FAIL" : "PASS", stats.free, before.free);
    return rc;
}

static uint64_t fault_address(unsigned int mode)
{
    extern uint8_t __text_start[];
    if (mode == 1 || mode == 10) return (uintptr_t)__text_start;
    if (mode == 2) return USER_TEST_CODE;
    if (mode == 3) return USER_TEST_DATA;
    if (mode == 4) return USER_TEST_STACK - 8;
    if (mode == 6) return 0x80000; /* Removed identity alias. */
    if (mode == 9) return KERNEL_VA_BASE + 0xFE200000ULL;
    return 0;
}
static int create_probe(unsigned int mode, uint32_t token, volatile uint64_t **state)
{
    struct vm_space space = {0};
    size_t code_size = (uintptr_t)user_probe_end - (uintptr_t)user_probe_start;
    uint64_t data[PAGE_SIZE / 8] = {0};
    data[4] = fault_address(mode);
    memcpy((uint8_t *)data + PAGE_SIZE - 4, "EL0 ", 4);
    int rc = vm_space_create(&space);
    if (rc) return rc;
    rc = vm_map_new(&space, USER_TEST_CODE, VM_READ | VM_EXEC, user_probe_start, code_size);
    if (!rc) rc = vm_map_new(&space, USER_TEST_DATA, VM_READ | VM_WRITE, data, sizeof(data));
    if (!rc) rc = vm_map_new(&space, USER_TEST_DATA + PAGE_SIZE, VM_READ | VM_WRITE, "copy\n", 5);
    for (uint64_t page = USER_TEST_STACK; !rc && page < USER_TEST_STACK_TOP; page += PAGE_SIZE)
        rc = vm_map_new(&space, page, VM_READ | VM_WRITE, NULL, 0);
    if (!rc && state) {
        struct vm_mapping mapping;
        rc = vm_query(&space, USER_TEST_DATA, &mapping);
        if (!rc) *state = arch_phys_to_virt(mapping.physical);
    }
    if (!rc) rc = task_create_user("EL0-probe", &space, USER_TEST_CODE, USER_TEST_STACK_TOP,
                                   ((uint64_t)token << 32) | mode);
    vm_space_destroy(&space); /* Empty after successful ownership transfer. */
    return rc;
}
static int wait_exit(uint32_t id, struct task_info *result)
{
    uint64_t until = clock_ms() + 4000;
    do {
        struct task_info tasks[MAX_TASKS];
        unsigned int count = task_snapshot(tasks, MAX_TASKS);
        for (unsigned int i = 0; i < count; ++i)
            if (tasks[i].id == id && tasks[i].state == TASK_EXITED) { *result = tasks[i]; return 0; }
        task_sleep_ms(1);
    } while (clock_ms() < until);
    task_control(id, TASK_KILL);
    task_reap();
    return -K_ETIMEDOUT;
}
static int isolation_test(void)
{
    volatile uint64_t *a = NULL, *b = NULL;
    int ida = create_probe(7, 11, &a), idb = create_probe(7, 22, &b);
    bool ok = ida > 0 && idb > 0;
    uint64_t irqs = kernel_timer_irq_count();
    if (ok) {
        task_sleep_ms(60);
        ok = a != b && a[0] == 11 && b[0] == 22 && a[1] && b[1] &&
             kernel_timer_irq_count() > irqs + 1;
        ok = ok && task_control((uint32_t)ida, TASK_STOP) == 0;
        uint64_t saved = a[1], other = b[1];
        task_sleep_ms(30);
        ok = ok && a[1] == saved && b[1] > other;
        ok = ok && task_control((uint32_t)ida, TASK_RESUME) == 0;
        task_sleep_ms(30);
        ok = ok && a[1] > saved;
        ok = ok && task_sleep((uint32_t)ida, 100) == 0;
    }
    if (ida > 0) ok = task_control((uint32_t)ida, TASK_KILL) == 0 && ok;
    if (idb > 0) ok = task_control((uint32_t)idb, TASK_KILL) == 0 && ok;
    task_reap();
    task_sleep_ms(110); /* Killed sleeping task must not be revived by its timer. */
    printk("[test] EL0 separate-TTBR0/preempt/stop/resume/kill %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}
int user_regression(void)
{
    struct page_stats before, after;
    task_reap(); page_get_stats(&before);
    int rc = isolation_test();
    unsigned int modes[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10};
    for (unsigned int i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        unsigned int mode = modes[i];
        int id = create_probe(mode, 0, NULL);
        struct task_info result = {0};
        bool ok = id > 0 && !wait_exit((uint32_t)id, &result);
        unsigned int ec = (unsigned int)(result.fault_esr >> 26) & 63;
        if (!mode) ok = ok && result.exit_status == 0 && !result.fault_esr;
        else {
            ok = ok && result.exit_status == -K_EFAULT;
            if (mode == 3 || mode == 10) ok = ok && result.fault_pc == fault_address(mode);
            else ok = ok && result.fault_pc >= USER_TEST_CODE && result.fault_pc < USER_TEST_CODE + PAGE_SIZE;
            if (mode == 5) ok = ok && (ec == 0 || ec == 0x18);
            else ok = ok && ec == ((mode == 3 || mode == 10) ? 0x20U : 0x24U) &&
                      result.fault_address == fault_address(mode);
            if (mode == 1 || mode == 2 || mode == 3 || mode == 9 || mode == 10)
                ok = ok && (result.fault_esr & 63) == 15; /* L3 permission fault */
            if (mode == 4 || mode == 6 || mode == 8)
                ok = ok && (result.fault_esr & 63) >= 4 && (result.fault_esr & 63) <= 7;
        }
        printk("[test] EL0 mode=%u %s status=%d EC=%x FAR=%llx\n", mode, ok ? "PASS" : "FAIL",
               result.exit_status, ec, result.fault_address);
        if (!ok) rc = -1;
    }
    task_reap(); page_get_stats(&after);
    if (before.free != after.free) rc = -1;
    printk("[test] EL0 syscall/fault-isolation/reclaim %s free=%llu baseline=%llu\n",
           rc ? "FAIL" : "PASS", after.free, before.free);
    return rc;
}
