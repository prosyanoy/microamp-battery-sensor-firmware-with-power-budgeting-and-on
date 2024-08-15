/* portSUPPRESS_TICKS_AND_SLEEP implementation for the simulator: instead of
 * waiting in real time, ask the sensor simulator to fast-forward virtual time
 * to the next sensor interrupt or the next RTOS timeout, whichever is first. */
#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "energy.h"
#include "hal.h"

void app_suppress_ticks_and_sleep(unsigned long expected_idle_ticks)
{
    if (expected_idle_ticks > 3600ul * 1000ul) expected_idle_ticks = 3600ul * 1000ul;
    uint64_t slept_us = hal_sleep_us((uint64_t)expected_idle_ticks * 1000u);
    unsigned long ticks = (unsigned long)((slept_us + 999u) / 1000u);
    if (ticks > expected_idle_ticks) ticks = expected_idle_ticks;
    vTaskStepTick(ticks);
    energy_note_wake();
}
