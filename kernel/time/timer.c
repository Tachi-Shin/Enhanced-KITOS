#include <kernel/timer.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <arch/irq.h>
static struct timer_event *events;
static bool before(uint64_t a, uint64_t b) { return (int64_t)(a - b) < 0; }
static bool cancel_locked(struct timer_event *event)
{
    if (!event || !event->armed) return false;
    struct timer_event **link = &events;
    while (*link && *link != event) link = &(*link)->next;
    if (!*link) return false;
    *link = event->next;
    event->next = NULL;
    event->armed = false;
    return true;
}
bool kernel_timer_cancel(struct timer_event *event)
{
    uint64_t flags = arch_irq_save();
    bool result = cancel_locked(event);
    kernel_time_reprogram();
    arch_irq_restore(flags);
    return result;
}
bool kernel_timer_pending(const struct timer_event *event)
{
    uint64_t flags = arch_irq_save();
    bool armed = event && event->armed;
    arch_irq_restore(flags);
    return armed;
}
int kernel_timer_arm_at(struct timer_event *event, uint64_t deadline,
                        void (*callback)(void *), void *argument)
{
    if (!event || !callback) return -K_EINVAL;
    uint64_t flags = arch_irq_save();
    /* Future deadlines must be within half the counter range. */
    if ((int64_t)(deadline - clock_now()) <= 0) {
        arch_irq_restore(flags);
        return -K_EINVAL;
    }
    cancel_locked(event);
    event->deadline = deadline;
    event->callback = callback;
    event->argument = argument;
    struct timer_event **link = &events;
    while (*link && !before(deadline, (*link)->deadline)) link = &(*link)->next;
    event->next = *link;
    *link = event;
    event->armed = true;
    kernel_time_reprogram();
    arch_irq_restore(flags);
    return 0;
}
int kernel_timer_arm(struct timer_event *event, uint32_t ms,
                     void (*callback)(void *), void *argument)
{
    if (!ms) return -K_EINVAL;
    uint64_t flags = arch_irq_save();
    int rc = kernel_timer_arm_at(event, clock_after_ms(ms), callback, argument);
    arch_irq_restore(flags);
    return rc;
}
void kernel_timer_expire(uint64_t now)
{
    uint64_t flags = arch_irq_save();
    /* A bounded interrupt even when callbacks continually rearm events. */
    for (unsigned int budget = 0; events && budget < 32U; ++budget) {
        if (before(now, events->deadline)) break;
        struct timer_event *event = events;
        void (*callback)(void *) = event->callback;
        void *argument = event->argument;
        cancel_locked(event);
        callback(argument);
    }
    arch_irq_restore(flags);
}
uint64_t kernel_timer_next(uint64_t fallback)
{
    uint64_t flags = arch_irq_save();
    uint64_t next = events && before(events->deadline, fallback) ? events->deadline : fallback;
    arch_irq_restore(flags);
    return next;
}
