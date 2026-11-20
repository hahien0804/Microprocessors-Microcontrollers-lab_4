#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

#define SCH_TICK_MS 10u
#define SCH_MAX_TASKS 16u
#define SCH_INVALID_ID UINT32_MAX

void SCH_Init(void);
void SCH_Update(void);
void SCH_Dispatch_Tasks(void);
bool SCH_Task_Ready(void);

/* Delay and period are in ticks. Zero period means one-shot.
   Add, Delete and Dispatch run in the foreground. Update runs in the ISR. */
uint32_t SCH_Add_Task(void (*function)(void), uint32_t delay, uint32_t period);
uint8_t SCH_Delete_Task(uint32_t task_id);
uint32_t get_time(void);

#endif
