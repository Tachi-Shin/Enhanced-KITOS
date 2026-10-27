#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H
#include <dos/type.h>
/* Caller-owned, zero-initialized, must live until cancelled or fired.
 * Callbacks run with IRQ masked; must not block, print, or switch tasks.
 * Cancel/rearm inside callbacks is supported. Single-core IRQ serialization.
 */
struct timer_event {
    uint64_t deadline;
    void (*callback)(void *);
    void *argument;
    struct timer_event *next;
    bool armed;
};
int kernel_timer_arm(struct timer_event *event, uint32_t milliseconds,
                     void (*callback)(void *), void *argument);
int kernel_timer_arm_at(struct timer_event *event, uint64_t deadline,
                        void (*callback)(void *), void *argument);
bool kernel_timer_cancel(struct timer_event *event);
bool kernel_timer_pending(const struct timer_event *event);
void kernel_timer_expire(uint64_t now);
uint64_t kernel_timer_next(uint64_t fallback);
void kernel_time_start(void);
bool kernel_time_irq(void *unused);
uint64_t kernel_timer_irq_count(void);
/* Used by event insertion/cancellation to update the clockevent device. */
void kernel_time_reprogram(void);
#endif
