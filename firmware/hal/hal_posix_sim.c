/* HAL backend for the FreeRTOS POSIX simulator: the I2C bus, the INT pin and
 * low-power sleep are served by the Python register-level sensor simulator
 * running as a child process (see sim/microamp_sim/server.py for the protocol).
 * Simulator time is slaved to the RTOS tick count. */
#include "hal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "energy.h"
#include "hal_posix_sim.h"

static int to_sim = -1, from_sim = -1;
static volatile bool irq_level;
static void (*irq_handler)(void);
static FILE *radio_log;

static void put_u64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t get_u64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }

static void io_all(int fd, uint8_t *buf, size_t n, bool write_mode)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = write_mode ? write(fd, buf + done, n - done) : read(fd, buf + done, n - done);
        if (r < 0 && errno == EINTR) continue;      /* RTOS tick signal */
        if (r <= 0) {
            fprintf(stderr, "microamp: simulator connection lost\n");
            _exit(2);
        }
        done += (size_t)r;
    }
}

int hal_posix_start(const app_config_t *cfg)
{
    int in[2], out[2];
    if (pipe(in) || pipe(out)) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(in[0], 0);
        dup2(out[1], 1);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        char dur[32], seed[32];
        snprintf(dur, sizeof dur, "%u", cfg->duration_ms / 1000u + 2u);
        snprintf(seed, sizeof seed, "%u", cfg->seed);
        setenv("PYTHONPATH", cfg->sim_dir, 1);
        const char *argv[16];
        int n = 0;
        argv[n++] = cfg->python; argv[n++] = "-m"; argv[n++] = "microamp_sim";
        argv[n++] = "--scenario"; argv[n++] = cfg->scenario;
        argv[n++] = "--duration"; argv[n++] = dur;
        argv[n++] = "--seed"; argv[n++] = seed;
        if (cfg->truth_path) { argv[n++] = "--truth"; argv[n++] = cfg->truth_path; }
        argv[n] = NULL;
        execvp(cfg->python, (char *const *)argv);
        perror("microamp: cannot start simulator");
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    to_sim = in[1];
    from_sim = out[0];
    signal(SIGPIPE, SIG_IGN);
    char dir[256];                      /* create the log's directory (one level) if missing */
    snprintf(dir, sizeof dir, "%s", cfg->log_path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = 0; mkdir(dir, 0755); }
    radio_log = fopen(cfg->log_path, "wb");
    return radio_log ? 0 : -2;
}

void hal_posix_stop(void)
{
    if (radio_log) fclose(radio_log);
    radio_log = NULL;
    if (to_sim >= 0) close(to_sim);
    to_sim = -1;
}

uint64_t hal_now_us(void) { return (uint64_t)xTaskGetTickCount() * 1000u; }
bool hal_irq_level(void) { return irq_level; }
void hal_irq_set_handler(void (*h)(void)) { irq_handler = h; }

static void update_irq(bool level, bool force_notify)
{
    bool rising = level && !irq_level;
    irq_level = level;
    if ((rising || (force_notify && level)) && irq_handler) irq_handler();
}

int hal_i2c_xfer(uint8_t addr7, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    uint8_t req[12 + 16];
    if (wlen > 16) return -1;
    req[0] = 'T'; req[1] = addr7; req[2] = (uint8_t)wlen; req[3] = (uint8_t)rlen;
    put_u64(req + 4, hal_now_us());
    memcpy(req + 12, w, wlen);
    io_all(to_sim, req, 12 + wlen, true);

    uint8_t resp[2 + 256];
    io_all(from_sim, resp, 2 + rlen, false);
    if (rlen) memcpy(r, resp + 2, rlen);

    /* Bus time: start + address + data bytes, 9 clocks per byte, 400 kHz. */
    const energy_params_t *p = energy_params();
    uint32_t bytes = 1 + (uint32_t)wlen + (rlen ? 1 + (uint32_t)rlen : 0);
    energy_charge_us(PS_SAMPLE, bytes * 9u * 1000000u / p->i2c_hz + 10u);
    stat_add(&g_stats.i2c_xfers, 1);
    stat_add(&g_stats.i2c_bytes, bytes);

    update_irq(resp[1] != 0, false);
    return resp[0] ? 0 : -1;
}

uint64_t hal_sleep_us(uint64_t max_us)
{
    uint8_t req[17], resp[9];
    uint64_t now = hal_now_us();
    req[0] = 'W';
    put_u64(req + 1, now);
    put_u64(req + 9, max_us);
    io_all(to_sim, req, sizeof req, true);
    io_all(from_sim, resp, sizeof resp, false);
    uint64_t reached = get_u64(resp);
    update_irq(resp[8] != 0, true);
    return reached > now ? reached - now : 0;
}

int hal_radio_send(const uint8_t *buf, size_t len)
{
    const energy_params_t *p = energy_params();
    uint32_t airtime_us = (uint32_t)(len + p->radio_overhead_bytes) * 8u * 3u;   /* 3 adv channels @ 1 Mbit/s */
    energy_charge_us(PS_RADIO, p->radio_startup_us + airtime_us);
    uint8_t hdr[5];
    size_t h = 0;
    size_t v = len;
    while (v >= 0x80) { hdr[h++] = (uint8_t)(v | 0x80); v >>= 7; }
    hdr[h++] = (uint8_t)v;
    fwrite(hdr, 1, h, radio_log);
    fwrite(buf, 1, len, radio_log);
    fflush(radio_log);
    stat_add(&g_stats.radio_packets, 1);
    stat_add(&g_stats.radio_bytes, (uint32_t)len);
    return 0;
}
