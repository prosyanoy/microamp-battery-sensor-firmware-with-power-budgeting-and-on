/* Shared RTOS objects (all statically allocated in main.c). */
#ifndef RTOS_H
#define RTOS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app.h"

extern SemaphoreHandle_t g_irq_sem;      /* binary: sensor interrupt -> sampler        */
extern SemaphoreHandle_t g_bus_mutex;    /* guards the I2C bus (sampler, reporter)     */
extern QueueHandle_t g_batch_q;          /* sampler  -> detector  (sample_batch_t)     */
extern QueueHandle_t g_event_q;          /* detector -> reporter  (det_event_t)        */

void sampler_task(void *arg);
void detector_task(void *arg);
void reporter_task(void *arg);

#endif
