/*
 * Environment sampling front-end: SHT31 temperature/humidity plus the
 * on-board battery divider.
 */

#ifndef ENV_SENSOR_H_
#define ENV_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

/** One set of readings. Units match the GATT characteristics exactly. */
struct env_sample {
	int16_t temp_c100;   /**< 0.01 degC, valid when temp_valid */
	uint16_t hum_pct100; /**< 0.01 %RH, valid when hum_valid */
	uint16_t batt_mv;    /**< millivolts, valid when batt_valid */
	bool temp_valid;
	bool hum_valid;
	bool batt_valid;
};

/**
 * Probe the sensors and prepare the ADC channel.
 *
 * @return 0 on success, negative errno if the SHT31 is missing.
 */
int env_sensor_init(void);

/**
 * Take a fresh reading.
 *
 * Never fails as a whole: channels that could not be read come back with
 * their *_valid flag cleared and their value zeroed.
 *
 * @return 0 if at least one channel produced a value, negative errno otherwise.
 */
int env_sensor_read(struct env_sample *out);

#endif /* ENV_SENSOR_H_ */
