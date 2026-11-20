/* Host demonstrations of handout Programs 1.1–1.4.
 * Build: gcc -std=c11 -Wall -Wextra -Werror -ICore/Inc Examples/handout_examples.c Core/Src/scheduler.c -o handout_examples
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "scheduler.h"

static unsigned calls;
static uint32_t elapsed_ms;
static unsigned a_calls, b_calls, c_calls;

static void X_Init(void)
{
    calls = 0;
    elapsed_ms = 0;
}

static void X(void)
{
    ++calls;
    elapsed_ms += 10;
}

static void Delay_190ms(void)
{
    elapsed_ms += 190;
}

static void Function_A(void) { ++a_calls; }
static void Function_B(void) { ++b_calls; }
static void Function_C(void) { ++c_calls; }

static void Square_Number(int value, int *result)
{
    *result = value * value;
}

int main(void)
{
    /* 1.1: Finite sample of a super loop. */
    X_Init();
    for (unsigned i = 0; i < 3; ++i) {
        X();
    }
    assert(calls == 3 && elapsed_ms == 30);

    /* 1.2: A 10 ms task with a blocking 190 ms delay. */
    X_Init();
    for (unsigned i = 0; i < 3; ++i) {
        X();
        Delay_190ms();
    }
    assert(calls == 3 && elapsed_ms == 600);

    /* 1.3: Three periodic function pointers and staggered initial delays.
       The host advances ticks manually, so this example needs no hardware. */
    SCH_Init();
    assert(SCH_Add_Task(Function_A, 0, 5) != SCH_INVALID_ID);
    assert(SCH_Add_Task(Function_B, 1, 10) != SCH_INVALID_ID);
    assert(SCH_Add_Task(Function_C, 3, 15) != SCH_INVALID_ID);
    for (unsigned tick = 0; tick < 30; ++tick) {
        while (SCH_Task_Ready()) {
            SCH_Dispatch_Tasks();
        }
        SCH_Update();
    }
    assert(a_calls == 6 && b_calls == 3 && c_calls == 2);

    /* 1.4: Call a function through a pointer and write through an argument. */
    int result;
    void (*operation)(int, int *) = Square_Number;
    operation(7, &result);
    assert(result == 49);
    puts("Programs 1.1–1.4 passed");
}
