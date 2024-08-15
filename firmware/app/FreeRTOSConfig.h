#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <limits.h>

#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE ((unsigned short)(PTHREAD_STACK_MIN))
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 0
#define configCHECK_FOR_STACK_OVERFLOW 0

/* No heap: every kernel object is statically allocated. */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configAPPLICATION_ALLOCATED_HEAP 1

/* Tickless idle with a user-supplied sleep routine (see app/tickless.c). */
void app_suppress_ticks_and_sleep(unsigned long expected_idle_ticks);
#define configUSE_TICKLESS_IDLE 2
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2
#define portSUPPRESS_TICKS_AND_SLEEP(x) app_suppress_ticks_and_sleep(x)

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1

#define configASSERT(x) do { if (!(x)) { __builtin_trap(); } } while (0)

#endif
