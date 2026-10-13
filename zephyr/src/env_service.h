/*
 * Custom GATT service for the environmental sensor node.
 *
 *   Service      e0f10001-7b5a-4b1c-9a3d-6f2c8e4d5a10
 *   Temperature  e0f10002-...  int16  0.01 degC   read + notify
 *   Humidity     e0f10003-...  uint16 0.01 %RH    read + notify
 *   Battery      e0f10004-...  uint16 millivolt   read
 *   History      e0f10005-...  8-byte records     notify
 *   History ctrl e0f10006-...  uint8 command      write
 *
 * The history characteristic is an extension beyond the three characteristics
 * in the specification: it is what makes the "replay the ring buffer after a
 * reconnect" requirement observable from a phone. Subscribe to it (or write
 * 0x01 to the control point) to receive the backlog.
 */

#ifndef ENV_SERVICE_H_
#define ENV_SERVICE_H_

#include <stdbool.h>

#include "env_sensor.h"

/*
 * Control-point commands. This characteristic started life as a history
 * control point; rebooting into the bootloader belongs to it too, because
 * "do something to the device" is a single concern and adding a second
 * characteristic for one byte would be worse.
 */
#define ENV_CTRL_STOP_REPLAY 0x00
#define ENV_CTRL_START_REPLAY 0x01
#define ENV_CTRL_CLEAR_HISTORY 0x02
#define ENV_CTRL_ENTER_BOOTLOADER 0x03

/**
 * Register the connection callbacks and start advertising.
 *
 * @return 0 on success, negative errno otherwise.
 */
int env_service_init(void);

/** True while a central is connected. */
bool env_service_connected(void);

/**
 * Update the temperature/humidity characteristics and, if the central has
 * subscribed, notify it.
 */
void env_service_publish(const struct env_sample *sample);

#endif /* ENV_SERVICE_H_ */
