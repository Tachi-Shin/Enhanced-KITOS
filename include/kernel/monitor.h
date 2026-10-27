#ifndef KERNEL_MONITOR_H
#define KERNEL_MONITOR_H
void monitor_task(void *unused);
int kernel_regression(void);
int memory_regression(void);
int user_regression(void);
int memory_pressure_regression(void);
#endif
