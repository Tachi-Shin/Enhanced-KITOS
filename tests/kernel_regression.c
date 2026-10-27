#include <kernel/monitor.h>
#include <kernel/task.h>
#include <kernel/clock.h>
#include <kernel/timer.h>
#include <kernel/printk.h>
#include <kernel/sync.h>
static volatile unsigned int progress[2];
static volatile bool finished[2];
static void worker(void *arg)
{
    unsigned int n = (unsigned int)(uintptr_t)arg;
    uint64_t until = clock_ms() + 2000U;
    /* Neither worker yields: both can advance only with timer preemption. */
    while ((!progress[1U - n] || progress[n] < 100000U) && clock_ms() < until)
        ++progress[n];
    finished[n] = true;
}
static volatile unsigned int wake_count;
static void waiting_worker(void *unused)
{
    (void)unused;
    for (;;) { ++wake_count; task_sleep_ms(20); }
}
static struct kernel_gate resource;
static volatile bool resource_held;
static void resource_worker(void *unused)
{
    (void)unused;
    if (!kernel_gate_try(&resource)) return;
    resource_held = true;
    task_sleep_ms(80);
    kernel_gate_release(&resource);
    resource_held = false;
}
static bool control_regression(void)
{
    wake_count = 0;
    int id = task_create("regress-wait", waiting_worker, NULL);
    if (id < 0 || task_sleep_ms(30)) return false;
    if (task_control((uint32_t)id, TASK_STOP)) return false;
    unsigned int saved = wake_count;
    task_sleep_ms(40);
    if (wake_count != saved || task_control((uint32_t)id, TASK_RESUME)) return false;
    task_sleep_ms(40);
    if (wake_count == saved || task_sleep((uint32_t)id, 100)) return false;
    if (task_control((uint32_t)id, TASK_KILL)) return false;
    saved = wake_count;
    task_sleep_ms(120);
    if (wake_count != saved) return false;
    id = task_create("regress-pin", resource_worker, NULL);
    task_sleep_ms(20);
    if (id < 0 || !resource_held || task_control((uint32_t)id, TASK_KILL) == 0 ||
        task_control((uint32_t)id, TASK_STOP) == 0) return false;
    task_sleep_ms(100);
    return !resource_held;
}
int kernel_regression(void)
{
    progress[0] = progress[1] = 0;
    finished[0] = finished[1] = false;
    int a = task_create("regress-a", worker, (void *)0UL);
    int b = task_create("regress-b", worker, (void *)1UL);
    if (a < 0 || b < 0) {
        if (a > 0) task_control((uint32_t)a, TASK_KILL);
        if (b > 0) task_control((uint32_t)b, TASK_KILL);
        return -1;
    }
    uint64_t start = clock_ms(), irqs = kernel_timer_irq_count();
    if (task_sleep_ms(60U) != 0) return -1;
    uint64_t elapsed = clock_ms() - start;
    bool ok = elapsed >= 60U && elapsed < 1000U && finished[0] && finished[1] &&
              progress[0] && progress[1] && kernel_timer_irq_count() > irqs;
    if (!finished[0]) task_control((uint32_t)a, TASK_KILL);
    if (!finished[1]) task_control((uint32_t)b, TASK_KILL);
    printk("[test] preempt/sleep %s elapsed=%llu irq=%llu\n", ok ? "PASS" : "FAIL",
           elapsed, kernel_timer_irq_count() - irqs);
    bool control_ok = control_regression();
    printk("[test] stop/resume/cancel/kill/reuse/pin %s\n", control_ok ? "PASS" : "FAIL");
    return ok && control_ok ? 0 : -1;
}
