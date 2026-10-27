#include <kernel/clock.h>
#include <kernel/timer.h>
#include <kernel/sched.h>
#include <dos/config.h>
#include <arch/timer.h>
#include <arch/irq.h>
static uint64_t quantum, next_quantum, interrupts;
static bool started;
void kernel_time_reprogram(void)
{
    if (!started) return;
    uint64_t next = kernel_timer_next(next_quantum), now = clock_now();
    if ((int64_t)(next - now) <= 0) next = now + 1U;
    arch_timer_program(next);
}
void kernel_time_start(void)
{
    uint64_t flags = arch_irq_save();
    quantum = (clock_frequency() * TIMER_INTERVAL_MS + 999U) / 1000U;
    if (!quantum) quantum = 1;
    next_quantum = clock_now() + quantum;
    started = true;
    kernel_time_reprogram();
    arch_irq_restore(flags);
}
bool kernel_time_irq(void *unused)
{
    (void)unused;
    uint64_t now = clock_now();
    bool reschedule = false;
    ++interrupts;
    kernel_timer_expire(now);
    if ((int64_t)(now - next_quantum) >= 0) {
        next_quantum = now + quantum;
        reschedule = sched_quantum_expired();
    }
    kernel_time_reprogram();
    return reschedule;
}
uint64_t kernel_timer_irq_count(void) { return interrupts; }
