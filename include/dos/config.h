#ifndef DOS_CONFIG_H
#define DOS_CONFIG_H

/* タイマー割込み周期：10ミリ秒 */
#define TIMER_INTERVAL_MS 10U

/* シェルを含めた通常タスク数。内部idleタスクは含まない。 */
#define MAX_TASKS        32U
#define TASK_STACK_SIZE (64U * 1024U)

#define TASK_UNUSED     0
#define TASK_READY      1
#define TASK_RUNNING    2
#define TASK_BLOCKED    3
#define TASK_EXITED     4
#define TASK_STOPPED    5
#define TASK_SLEEPING   6

#endif
