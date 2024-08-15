/* Driver tests against a fake LIS3DH implemented behind the HAL. */
#include <string.h>

#include "accel_driver.h"
#include "hal.h"
#include "unity.h"

static uint8_t regs[0x40];
static uint8_t fifo[64][6];
static int fifo_n;
static int nack_all;

static void push_sample(int16_t x, int16_t y, int16_t z)   /* 12-bit counts */
{
    int16_t v[3] = {x, y, z};
    for (int a = 0; a < 3; a++) {
        uint16_t raw = (uint16_t)(v[a] << 4);
        fifo[fifo_n][2 * a] = raw & 0xFF;
        fifo[fifo_n][2 * a + 1] = raw >> 8;
    }
    fifo_n++;
}

static uint8_t read_reg(uint8_t a)
{
    if (a >= 0x28 && a <= 0x2D) {
        uint8_t v = fifo_n ? fifo[0][a - 0x28] : 0;
        if (a == 0x2D && fifo_n) { memmove(fifo[0], fifo[1], (size_t)(--fifo_n) * 6); }
        return v;
    }
    if (a == 0x2F) return fifo_n == 0 ? 0x20 : (fifo_n >= 32 ? 0x40 : (uint8_t)fifo_n);
    return regs[a];
}

int hal_i2c_xfer(uint8_t addr7, const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{
    if (nack_all || addr7 != ACCEL_I2C_ADDR) return -1;
    uint8_t p = w[0] & 0x7F;
    int inc = w[0] & 0x80;
    for (size_t i = 1; i < wl; i++) { regs[p] = w[i]; p += inc ? 1 : 0; }
    for (size_t i = 0; i < rl; i++) { r[i] = read_reg(p); p += inc ? 1 : 0; }
    return 0;
}
bool hal_irq_level(void) { return false; }
void hal_irq_set_handler(void (*h)(void)) { (void)h; }
uint64_t hal_now_us(void) { return 0; }
uint64_t hal_sleep_us(uint64_t m) { return m; }
int hal_radio_send(const uint8_t *b, size_t n) { (void)b; (void)n; return 0; }

void setUp(void) { memset(regs, 0, sizeof regs); fifo_n = 0; nack_all = 0; regs[0x0F] = 0x33; }
void tearDown(void) {}

static void test_init_ok_and_sets_range(void)
{
    TEST_ASSERT_EQUAL(0, accel_init());
    TEST_ASSERT_EQUAL_HEX8(0x38, regs[0x23]);      /* +-16 g, high resolution */
}

static void test_init_rejects_wrong_chip(void)
{
    regs[0x0F] = 0x44;
    TEST_ASSERT_TRUE(accel_init() < 0);
}

static void test_nack_is_reported(void)
{
    nack_all = 1;
    TEST_ASSERT_TRUE(accel_init() < 0);
}

static void test_configure_fifo_stream_100hz(void)
{
    accel_cfg_t c = {100, ACCEL_FIFO_STREAM, 16};
    TEST_ASSERT_EQUAL(0, accel_configure(&c));
    TEST_ASSERT_EQUAL_HEX8(0x57, regs[0x20]);      /* ODR=100 Hz, XYZ enabled */
    TEST_ASSERT_EQUAL_HEX8(0x40, regs[0x24]);      /* FIFO enable */
    TEST_ASSERT_EQUAL_HEX8(0x90, regs[0x2E]);      /* stream mode, FTH=16 */
}

static void test_configure_validates_watermark(void)
{
    accel_cfg_t c = {100, ACCEL_FIFO_STREAM, 0};
    TEST_ASSERT_TRUE(accel_configure(&c) < 0);
    c.watermark = 32;
    TEST_ASSERT_TRUE(accel_configure(&c) < 0);
}

static void test_sample_conversion_to_mg(void)
{
    push_sample(83, -83, 1333);                    /* ~1 g, -1 g, 16 g full scale */
    int16_t mg[3];
    TEST_ASSERT_EQUAL(0, accel_read_sample(mg));
    TEST_ASSERT_EQUAL_INT16(996, mg[0]);
    TEST_ASSERT_EQUAL_INT16(-996, mg[1]);
    TEST_ASSERT_EQUAL_INT16(15996, mg[2]);
}

static void test_fifo_drain_pops_in_order(void)
{
    for (int i = 0; i < 10; i++) push_sample((int16_t)i, 0, 0);
    int16_t out[32][3];
    uint8_t n;
    TEST_ASSERT_EQUAL(0, accel_fifo_read(out, 32, &n));
    TEST_ASSERT_EQUAL_UINT8(10, n);
    for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL_INT16(i * ACCEL_MG_PER_LSB, out[i][0]);
}

static void test_fifo_read_respects_max_and_empty(void)
{
    int16_t out[4][3];
    uint8_t n = 99;
    TEST_ASSERT_EQUAL(0, accel_fifo_read(out, 4, &n));
    TEST_ASSERT_EQUAL_UINT8(0, n);
    for (int i = 0; i < 6; i++) push_sample(1, 1, 1);
    TEST_ASSERT_EQUAL(0, accel_fifo_read(out, 4, &n));
    TEST_ASSERT_EQUAL_UINT8(4, n);
}

static void test_full_fifo_reports_32_and_overrun(void)
{
    for (int i = 0; i < 32; i++) push_sample(0, 0, 0);
    uint8_t cnt;
    bool ovr;
    TEST_ASSERT_EQUAL(0, accel_fifo_level(&cnt, &ovr));
    TEST_ASSERT_EQUAL_UINT8(32, cnt);
    TEST_ASSERT_TRUE(ovr);
}

static void test_motion_setup(void)
{
    TEST_ASSERT_EQUAL(0, accel_enable_motion(1));
    TEST_ASSERT_EQUAL_HEX8(0x01, regs[0x21]);
    TEST_ASSERT_EQUAL_HEX8(0x01, regs[0x32]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, regs[0x30]);
    TEST_ASSERT_EQUAL(0, accel_route_irq(ACCEL_IRQ_MOTION));
    TEST_ASSERT_EQUAL_HEX8(0x40, regs[0x22]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_ok_and_sets_range);
    RUN_TEST(test_init_rejects_wrong_chip);
    RUN_TEST(test_nack_is_reported);
    RUN_TEST(test_configure_fifo_stream_100hz);
    RUN_TEST(test_configure_validates_watermark);
    RUN_TEST(test_sample_conversion_to_mg);
    RUN_TEST(test_fifo_drain_pops_in_order);
    RUN_TEST(test_fifo_read_respects_max_and_empty);
    RUN_TEST(test_full_fifo_reports_32_and_overrun);
    RUN_TEST(test_motion_setup);
    return UNITY_END();
}
