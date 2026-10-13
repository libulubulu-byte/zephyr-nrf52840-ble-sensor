/*
 * Status LED (led0 in the devicetree: blue, P0.15, active high).
 *
 * Only ever lit while a central is connected, so it cannot pollute the
 * standby current measurement.
 */

#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include <stdbool.h>

/** Configure the LED, blink an identification pattern, then turn it off. */
void status_led_init(void);

/** Turn the LED on or off (no-op if the alias is missing/disabled). */
void status_led_set(bool on);

#endif /* STATUS_LED_H_ */
