#include <stdio.h>
#include <stdlib.h>
#include <kernel/timer.h>
#include <kernel/clock.h>
#include <dos/config.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "timer line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned int checks, fired, requested;
static uint64_t now, programmed, arm_delay;
extern uint64_t test_irq_mask;
uint64_t clock_now(void) { return now; }
uint64_t clock_frequency(void) { return 1000000; }
uint64_t clock_after_ms(uint32_t ms)
{
    uint64_t deadline = now + (uint64_t)ms * 1000;
    now += arm_delay; arm_delay = 0;
    return deadline;
}
void arch_timer_program(uint64_t deadline) { programmed = deadline; }
bool sched_quantum_expired(void) { ++requested; return true; }
static void callback(void *arg) { CHECK(test_irq_mask == 128); fired = fired * 10 + (unsigned int)(uintptr_t)arg; }
static struct timer_event recurring;
static unsigned int repeats;
static void rearm(void *arg)
{
    (void)arg; ++repeats;
    CHECK(!kernel_timer_pending(&recurring));
    if (repeats < 3) CHECK(kernel_timer_arm(&recurring, 1, rearm, NULL) == 0);
}
static void count(void *arg) { ++*(unsigned int *)arg; }
int main(void)
{
    struct timer_event a = {0}, b = {0}, c = {0};
    CHECK(kernel_timer_arm(NULL, 1, callback, NULL) < 0);
    CHECK(kernel_timer_arm(&a, 0, callback, NULL) < 0);
    CHECK(kernel_timer_arm(&a, 1, NULL, NULL) < 0);
    kernel_time_start(); CHECK(programmed == TIMER_INTERVAL_MS * 1000U);
    CHECK(kernel_timer_arm(&a, 3, callback, (void *)1) == 0);
    CHECK(kernel_timer_arm(&b, 2, callback, (void *)2) == 0);
    CHECK(kernel_timer_arm(&c, 2, callback, (void *)3) == 0);
    CHECK(programmed == 2000);
    now = 1999; kernel_timer_expire(now); CHECK(!fired && test_irq_mask == 0);
    now = 2000; kernel_timer_expire(now); CHECK(fired == 23 && !b.armed && !c.armed);
    CHECK(kernel_timer_cancel(&a)); CHECK(!kernel_timer_cancel(&a));
    CHECK(kernel_timer_arm(&a, 5, callback, (void *)4) == 0);
    CHECK(kernel_timer_arm(&a, 1, callback, (void *)5) == 0);
    now = 3000; kernel_timer_expire(now); CHECK(fired == 235);
    CHECK(kernel_timer_arm(&recurring, 1, rearm, NULL) == 0);
    for (unsigned int i = 0; i < 3; ++i) { now += 1000; kernel_timer_expire(now); }
    CHECK(repeats == 3 && !recurring.armed);
    now = 1000000; CHECK(kernel_time_irq(NULL));
    CHECK(requested == 1 && kernel_timer_irq_count() == 1 && programmed > now);
    /* Hardware elapsed time expires sleep even if many quantum IRQs were missed. */
    CHECK(kernel_timer_arm(&a, 1, callback, (void *)6) == 0);
    now += 100000; CHECK(kernel_time_irq(NULL)); CHECK(fired == 2356);
    struct timer_event many[40] = {{0}}; unsigned int count_value = 0;
    for (unsigned int i = 0; i < 40; ++i) CHECK(kernel_timer_arm(&many[i], 1, count, &count_value) == 0);
    now += 1000; kernel_timer_expire(now); CHECK(count_value == 32);
    kernel_timer_expire(now); CHECK(count_value == 40);
    now = ~0ULL - 500;
    CHECK(kernel_timer_arm(&a, 1, callback, (void *)7) == 0);
    now = 498; kernel_timer_expire(now); CHECK(a.armed);
    now = 499; kernel_timer_expire(now); CHECK(!a.armed && fired == 23567);
    test_irq_mask = 128; CHECK(kernel_timer_arm(&a, 1, callback, NULL) == 0);
    CHECK(test_irq_mask == 128 && kernel_timer_cancel(&a) && test_irq_mask == 128);
    test_irq_mask = 0; now = 2000000; arm_delay = 100000;
    unsigned int late = 0;
    CHECK(kernel_timer_arm(&a, 1, count, &late) == 0);
    CHECK(a.armed && !late && programmed > now && test_irq_mask == 0);
    kernel_timer_expire(now); CHECK(late == 1 && !a.armed);
    CHECK(kernel_timer_arm_at(&a, now, count, &late) < 0 && !a.armed);
    printf("timer deadlines/cancel/rearm/budget/wrap: PASS (%u checks)\n", checks);
}
