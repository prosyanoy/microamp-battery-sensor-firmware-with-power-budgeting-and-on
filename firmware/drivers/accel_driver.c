#include "accel_driver.h"

#include "hal.h"

#define REG_WHO_AM_I 0x0F
#define REG_CTRL1 0x20
#define REG_CTRL2 0x21
#define REG_CTRL3 0x22
#define REG_CTRL4 0x23
#define REG_CTRL5 0x24
#define REG_STATUS 0x27
#define REG_OUT_X_L 0x28
#define REG_FIFO_CTRL 0x2E
#define REG_FIFO_SRC 0x2F
#define REG_INT1_CFG 0x30
#define REG_INT1_SRC 0x31
#define REG_INT1_THS 0x32
#define REG_INT1_DUR 0x33
#define AUTO_INC 0x80

static int wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    return hal_i2c_xfer(ACCEL_I2C_ADDR, b, 2, 0, 0);
}

static int rd(uint8_t reg, uint8_t *buf, size_t n)
{
    const uint8_t r = (uint8_t)(reg | (n > 1 ? AUTO_INC : 0));
    return hal_i2c_xfer(ACCEL_I2C_ADDR, &r, 1, buf, n);
}

static uint8_t odr_code(uint16_t hz)
{
    switch (hz) {
    case 1: return 1;
    case 10: return 2;
    case 25: return 3;
    case 50: return 4;
    case 100: return 5;
    case 200: return 6;
    case 400: return 7;
    default: return 0;
    }
}

static void raw_to_mg(const uint8_t b[6], int16_t mg[3])
{
    for (int a = 0; a < 3; a++) {
        int16_t raw = (int16_t)((uint16_t)b[2 * a] | ((uint16_t)b[2 * a + 1] << 8));
        mg[a] = (int16_t)((raw >> 4) * ACCEL_MG_PER_LSB);   /* left-justified 12-bit */
    }
}

int accel_who_am_i(uint8_t *id) { return rd(REG_WHO_AM_I, id, 1); }

int accel_init(void)
{
    uint8_t id = 0;
    if (wr(REG_CTRL5, 0x80) < 0) return -1;                /* BOOT: reload defaults */
    if (accel_who_am_i(&id) < 0 || id != 0x33) return -2;
    if (wr(REG_CTRL4, 0x38) < 0) return -3;                /* FS=16 g, HR=1 */
    return wr(REG_CTRL1, 0x07);                            /* power-down, XYZ on */
}

int accel_configure(const accel_cfg_t *cfg)
{
    if (cfg->watermark == 0 || cfg->watermark >= ACCEL_FIFO_DEPTH) return -10;
    if (wr(REG_CTRL1, 0x07) < 0) return -1;                /* quiesce while changing mode */
    if (wr(REG_FIFO_CTRL, 0) < 0) return -1;               /* bypass resets the FIFO */
    const uint8_t fifo_en = cfg->fifo_mode != ACCEL_FIFO_BYPASS ? 0x40 : 0x00;
    if (wr(REG_CTRL5, fifo_en) < 0) return -1;
    if (wr(REG_FIFO_CTRL, (uint8_t)((cfg->fifo_mode << 6) | cfg->watermark)) < 0) return -1;
    return wr(REG_CTRL1, (uint8_t)((odr_code(cfg->odr_hz) << 4) | 0x07));
}

int accel_route_irq(uint8_t mask) { return wr(REG_CTRL3, mask); }

int accel_enable_motion(uint8_t threshold)
{
    if (wr(REG_CTRL2, 0x01) < 0) return -1;                /* HP filter on INT1 path */
    if (wr(REG_INT1_THS, threshold) < 0) return -1;
    if (wr(REG_INT1_DUR, 0) < 0) return -1;
    return wr(REG_INT1_CFG, 0x2A);                         /* X/Y/Z high events, OR */
}

int accel_clear_motion(void)
{
    uint8_t v;
    return rd(REG_INT1_SRC, &v, 1);
}

int accel_status_drdy(bool *ready)
{
    uint8_t s = 0;
    int rc = rd(REG_STATUS, &s, 1);
    *ready = (s & 0x08) != 0;
    return rc;
}

int accel_read_sample(int16_t mg[3])
{
    uint8_t b[6];
    int rc = rd(REG_OUT_X_L, b, 6);
    if (rc == 0) raw_to_mg(b, mg);
    return rc;
}

int accel_fifo_level(uint8_t *count, bool *overrun)
{
    uint8_t s = 0;
    int rc = rd(REG_FIFO_SRC, &s, 1);
    *count = (s & 0x20) ? 0 : (uint8_t)(s & 0x1F ? (s & 0x1F) : ((s & 0x40) ? ACCEL_FIFO_DEPTH : 0));
    *overrun = (s & 0x40) != 0;
    return rc;
}

int accel_fifo_read(int16_t (*out)[3], uint8_t max, uint8_t *n)
{
    uint8_t level;
    bool ovr;
    *n = 0;
    int rc = accel_fifo_level(&level, &ovr);
    if (rc < 0) return rc;
    if (level > max) level = max;
    for (uint8_t i = 0; i < level; i++) {
        rc = accel_read_sample(out[i]);
        if (rc < 0) return rc;
        (*n)++;
    }
    return 0;
}
