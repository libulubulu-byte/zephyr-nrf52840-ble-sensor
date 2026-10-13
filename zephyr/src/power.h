/*
 * Sampling cadence and power state machine.
 *
 *  connected     : System ON, one sample + notify every APP_NOTIFY_INTERVAL_S
 *  disconnected  : System ON Idle, one sample every
 *                  APP_OFFLINE_SAMPLE_INTERVAL_S, stored in the ring buffer,
 *                  CPU asleep in between (tickless kernel, RTC running)
 */

#ifndef POWER_H_
#define POWER_H_

#include <stdbool.h>

/** Kick off the offline sampling cadence. */
void power_init(void);

/** Switch between the connected and the offline cadence. */
void power_set_connected(bool connected);

#endif /* POWER_H_ */
