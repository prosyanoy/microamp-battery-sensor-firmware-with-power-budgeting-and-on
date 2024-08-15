#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "energy.h"
#include "hal.h"
#include "hal_posix_sim.h"
#include "rtos.h"

app_config_t g_cfg;
app_stats_t g_stats;
volatile uint32_t g_last_activity_ms;
SemaphoreHandle_t g_irq_sem, g_bus_mutex;
QueueHandle_t g_batch_q, g_event_q;

static void ledger_lock(void) { taskENTER_CRITICAL(); }
static void ledger_unlock(void) { taskEXIT_CRITICAL(); }

void stat_add(uint32_t *c, uint32_t v) { __atomic_fetch_add(c, v, __ATOMIC_RELAXED); }

/* ---- static kernel objects: the firmware never touches a heap ---- */
#define STACK_WORDS (64 * 1024 / sizeof(StackType_t))
static StaticSemaphore_t irq_sem_buf, bus_mutex_buf;
static StaticQueue_t batch_q_buf, event_q_buf;
static uint8_t batch_q_store[4 * sizeof(sample_batch_t)];
static uint8_t event_q_store[8 * sizeof(det_event_t)];
static StaticTask_t tcb[3];
static StackType_t stacks[3][STACK_WORDS];
static StaticTask_t idle_tcb;
static StackType_t idle_stack[STACK_WORDS];

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb_out, StackType_t **stack_out, configSTACK_DEPTH_TYPE *size_out)
{
    *tcb_out = &idle_tcb;
    *stack_out = idle_stack;
    *size_out = STACK_WORDS;
}

static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return v && *v ? v : def;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--design polling|fifo|wom] [--scenario day|drive|parked] [--seed N]\n"
            "          [--duration SECONDS] [--log FILE] [--report FILE.json] [--truth FILE.json]\n"
            "          [--status-period SECONDS] [--battery-mah N] [--python PATH] [--sim-dir DIR]\n"
            "Defaults come from MICROAMP_* environment variables (see .env.example).\n", argv0);
}

static int parse(int argc, char **argv)
{
    const char *design = env_or("MICROAMP_DESIGN", "wom");
    g_cfg.scenario = env_or("MICROAMP_SCENARIO", "day");
    g_cfg.seed = (uint32_t)atoi(env_or("MICROAMP_SEED", "7"));
    g_cfg.duration_ms = (uint32_t)(atof(env_or("MICROAMP_DURATION_S", "600")) * 1000);
    g_cfg.log_path = env_or("MICROAMP_LOG", "microamp.log");
    g_cfg.python = env_or("MICROAMP_PYTHON", "python3");
    g_cfg.sim_dir = env_or("MICROAMP_SIM_DIR", MICROAMP_DEFAULT_SIM_DIR);
    g_cfg.battery_mah = (uint32_t)atoi(env_or("MICROAMP_BATTERY_MAH", "225"));
    g_cfg.status_period_ms = (uint32_t)(atof(env_or("MICROAMP_STATUS_PERIOD_S", "60")) * 1000);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-h") || !strcmp(a, "--help") || !v) return -1;
        if (!strcmp(a, "--design")) design = v;
        else if (!strcmp(a, "--scenario")) g_cfg.scenario = v;
        else if (!strcmp(a, "--seed")) g_cfg.seed = (uint32_t)atoi(v);
        else if (!strcmp(a, "--duration")) g_cfg.duration_ms = (uint32_t)(atof(v) * 1000);
        else if (!strcmp(a, "--log")) g_cfg.log_path = v;
        else if (!strcmp(a, "--report")) g_cfg.report_path = v;
        else if (!strcmp(a, "--truth")) g_cfg.truth_path = v;
        else if (!strcmp(a, "--status-period")) g_cfg.status_period_ms = (uint32_t)(atof(v) * 1000);
        else if (!strcmp(a, "--battery-mah")) g_cfg.battery_mah = (uint32_t)atoi(v);
        else if (!strcmp(a, "--python")) g_cfg.python = v;
        else if (!strcmp(a, "--sim-dir")) g_cfg.sim_dir = v;
        else return -1;
        i++;
    }
    g_cfg.design = design_by_name(design);
    if (!g_cfg.design || g_cfg.duration_ms == 0 || g_cfg.status_period_ms == 0) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    if (parse(argc, argv) < 0) {
        usage(argv[0]);
        return 1;
    }
    energy_params_t ep;
    energy_default_params(&ep);
    ep.battery_mah = g_cfg.battery_mah;
    energy_set_lock_hooks(ledger_lock, ledger_unlock);
    energy_init(&ep, g_cfg.design->tickless);
    if (hal_posix_start(&g_cfg) < 0) {
        perror("microamp: start");
        return 2;
    }

    g_irq_sem = xSemaphoreCreateBinaryStatic(&irq_sem_buf);
    g_bus_mutex = xSemaphoreCreateMutexStatic(&bus_mutex_buf);
    g_batch_q = xQueueCreateStatic(4, sizeof(sample_batch_t), batch_q_store, &batch_q_buf);
    g_event_q = xQueueCreateStatic(8, sizeof(det_event_t), event_q_store, &event_q_buf);

    xTaskCreateStatic(sampler_task, "sampler", STACK_WORDS, (void *)g_cfg.design, 3, stacks[0], &tcb[0]);
    xTaskCreateStatic(detector_task, "detector", STACK_WORDS, NULL, 2, stacks[1], &tcb[1]);
    xTaskCreateStatic(reporter_task, "reporter", STACK_WORDS, NULL, 1, stacks[2], &tcb[2]);
    vTaskStartScheduler();
    return 0;
}
