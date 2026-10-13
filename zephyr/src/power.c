/*
 * Sampling cadence.
 *
 * All of the work happens in a single delayed work item on the system work
 * queue. Between two runs the CPU has nothing to do, so the idle thread puts
 * the core into System ON Idle and the kernel's tickless timekeeping lets the
 * RTC wake it up exactly when the next sample is due - no busy polling, no
 * timer interrupt every millisecond.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "env_sensor.h"
#include "env_service.h"
#include "history.h"
#include "power.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

static struct k_work_delayable sample_work;
static bool connected_mode;

/* Last known good values, so a failed read does not push a zero into the
 * history buffer.
 */
static int16_t last_temp;
static uint16_t last_hum;

static void log_sample(const char *tag, const struct env_sample *s)
{
	int t = s->temp_c100;
	int at = (t < 0) ? -t : t;
	char sign = (t < 0) ? '-' : '+';

	LOG_INF("%s t=%c%d.%02d C rh=%u.%02u %%RH vbat=%u mV (%u buffered, %u dropped)",
		tag, sign, at / 100, at % 100,
		s->hum_pct100 / 100U, s->hum_pct100 % 100U,
		s->batt_mv, (unsigned int)history_count(), history_dropped());
}

static k_timeout_t interval_for_state(void)
{
	return connected_mode ? K_SECONDS(APP_NOTIFY_INTERVAL_S)
			      : K_SECONDS(APP_OFFLINE_SAMPLE_INTERVAL_S);
}

static void sample_work_handler(struct k_work *work)
{
	struct env_sample s;

	ARG_UNUSED(work);

	if (env_sensor_read(&s) != 0) {
		LOG_WRN("sampling failed, keeping previous values");
		k_work_reschedule(&sample_work, interval_for_state());
		return;
	}

	if (s.temp_valid) {
		last_temp = s.temp_c100;
	}
	if (s.hum_valid) {
		last_hum = s.hum_pct100;
	}

	if (connected_mode) {
		/* Streaming mode: push the readings to the central. */
		env_service_publish(&s);
		log_sample("notify", &s);
	} else {
		/* Offline mode: keep the readings for the next connection. */
		struct history_record rec = {
			.uptime_s = (uint32_t)(k_uptime_get() / 1000),
			.temp_c100 = last_temp,
			.hum_pct100 = last_hum,
		};

		history_push(&rec);
		log_sample("offline", &s);
	}

	k_work_reschedule(&sample_work, interval_for_state());
}

void power_set_connected(bool connected)
{
	if (connected_mode == connected) {
		return;
	}

	connected_mode = connected;

	LOG_INF("cadence switched to %s (%d s)",
		connected ? "streaming" : "offline",
		connected ? APP_NOTIFY_INTERVAL_S : APP_OFFLINE_SAMPLE_INTERVAL_S);

	/* Re-arm immediately so the first sample of the new cadence is not
	 * delayed by up to 30 seconds.
	 */
	k_work_reschedule(&sample_work, K_NO_WAIT);
}

void power_init(void)
{
	connected_mode = false;
	k_work_init_delayable(&sample_work, sample_work_handler);
	k_work_reschedule(&sample_work, K_NO_WAIT);
}
