/* Driver for a LIS3DH-compatible 3-axis accelerometer, written only against hal.h. */
#ifndef ACCEL_DRIVER_H
#define ACCEL_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#define ACCEL_I2C_ADDR 0x18
#define ACCEL_FIFO_DEPTH 32
#define ACCEL_MG_PER_LSB 12          /* +-16 g, 12-bit high-resolution mode */

#define ACCEL_IRQ_NONE 0x00
#define ACCEL_IRQ_FIFO_WATERMARK 0x04
#define ACCEL_IRQ_FIFO_OVERRUN 0x02
#define ACCEL_IRQ_DRDY 0x10
#define ACCEL_IRQ_MOTION 0x40

typedef enum { ACCEL_FIFO_BYPASS = 0, ACCEL_FIFO_STREAM = 2 } accel_fifo_mode_t;

typedef struct {
    uint16_t odr_hz;                 /* 0 (power-down), 1, 10, 25, 50, 100, 200, 400 */
    accel_fifo_mode_t fifo_mode;
    uint8_t watermark;               /* 1..31 */
} accel_cfg_t;

int accel_init(void);                         /* checks WHO_AM_I, reboots, sets +-16 g HR */
int accel_configure(const accel_cfg_t *cfg);
int accel_route_irq(uint8_t mask);            /* which sources drive the INT1 pin */
int accel_enable_motion(uint8_t threshold);   /* high-pass motion detect on all axes */
int accel_clear_motion(void);                 /* read INT1_SRC to release the latch */
int accel_who_am_i(uint8_t *id);
int accel_status_drdy(bool *ready);
int accel_read_sample(int16_t mg[3]);         /* one sample (pops the FIFO when enabled) */
int accel_fifo_level(uint8_t *count, bool *overrun);
/* Drain up to `max` samples; returns the number read in *n. */
int accel_fifo_read(int16_t (*out)[3], uint8_t max, uint8_t *n);

#endif
