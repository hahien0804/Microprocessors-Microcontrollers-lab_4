#include "scheduler.h"

#include <stddef.h>
#include <stdbool.h>

#define NONE SCH_MAX_TASKS
#define GENERATION_MASK (UINT32_MAX >> 4u)

typedef struct {
    void (*function)(void);
    uint32_t deadline;
    uint32_t period;
    uint32_t generation;
    unsigned next;
} Task;

_Static_assert(SCH_MAX_TASKS == 16u, "Task ID reserves four slot bits");

static Task tasks[SCH_MAX_TASKS];
static unsigned head;
static volatile uint32_t ticks;

/* Deadlines are comparable within half the range of the tick counter.
   The foreground must service the queue within that interval. */
static bool before(uint32_t a, uint32_t b)
{
    return a - b > INT32_MAX;
}

static void enqueue(unsigned slot)
{
    Task *task = &tasks[slot];
    unsigned *link = &head;
    uint32_t now = ticks;
    int32_t offset = (int32_t)(task->deadline - now);

    while (*link != NONE) {
        unsigned current = *link;
        int32_t current_offset = (int32_t)(tasks[current].deadline - now);
        if (offset < current_offset ||
            (offset == current_offset && slot < current)) {
            break;
        }
        link = &tasks[current].next;
    }
    task->next = *link;
    *link = slot;
}

void SCH_Init(void)
{
    head = NONE;
    ticks = 0;
    for (unsigned slot = 0; slot < SCH_MAX_TASKS; ++slot) {
        tasks[slot].function = NULL;
    }
}

void SCH_Update(void)
{
    /* Remaining time is deadline - ticks. Advancing the shared clock
       updates every timeout logically in O(1), without touching the queue. */
    ++ticks;
}

uint32_t get_time(void)
{
    return ticks * SCH_TICK_MS;
}

uint32_t SCH_Add_Task(void (*function)(void), uint32_t delay, uint32_t period)
{
    if (function == NULL || delay > INT32_MAX || period > INT32_MAX) {
        return SCH_INVALID_ID;
    }

    for (unsigned slot = 0; slot < SCH_MAX_TASKS; ++slot) {
        Task *task = &tasks[slot];
        if (task->function == NULL) {
            /* Skip the all-ones generation reserved by SCH_INVALID_ID. */
            task->generation = (task->generation + 1u) % GENERATION_MASK;
            task->function = function;
            task->deadline = ticks + delay;
            task->period = period;
            enqueue(slot);
            return (task->generation << 4u) | slot;
        }
    }
    return SCH_INVALID_ID;
}

uint8_t SCH_Delete_Task(uint32_t task_id)
{
    unsigned slot = task_id & 15u;
    Task *task = &tasks[slot];

    if (task->function == NULL || task->generation != (task_id >> 4u)) {
        return 0;
    }

    unsigned *link = &head;
    while (*link != slot) {
        link = &tasks[*link].next;
    }
    *link = task->next;
    task->function = NULL;
    return 1;
}

bool SCH_Task_Ready(void)
{
    return head != NONE && !before(ticks, tasks[head].deadline);
}

void SCH_Dispatch_Tasks(void)
{
    if (!SCH_Task_Ready()) {
        return;
    }

    unsigned slot = head;
    Task *task = &tasks[slot];
    void (*function)(void) = task->function;
    head = task->next;

    if (task->period == 0u) {
        task->function = NULL;
    } else {
        /* Keep the original phase and requeue before calling the task.
           This also lets a running periodic callback delete itself. */
        task->deadline += task->period;
        enqueue(slot);
    }
    function();
}
