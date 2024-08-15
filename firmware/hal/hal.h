/* Hardware abstraction layer. The driver and application only use this API;
 * hal_posix_sim.c implements it against the Python register-level simulator,
 * a real port would implement it with an I2C peripheral, a GPIO interrupt and
 * a low-power timer. */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Blocking I2C write-then-read (repeated start). Returns 0 on ACK, <0 on NACK/error. */
int hal_i2c_xfer(uint8_t addr7, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen);

/* Interrupt line of the sensor. The handler runs in "ISR context". */
void hal_irq_set_handler(void (*handler)(void));
bool hal_irq_level(void);

/* Monotonic time in microseconds (virtual time slaved to the RTOS tick). */
uint64_t hal_now_us(void);

/* Low power sleep: stop the clock for up to max_us, wake early if the sensor
 * raises its interrupt. Returns microseconds actually slept. */
uint64_t hal_sleep_us(uint64_t max_us);

/* Transmit one packet (the simulated radio appends it to the device log). */
int hal_radio_send(const uint8_t *buf, size_t len);

#endif
