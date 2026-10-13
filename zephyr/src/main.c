/*
 * BLE environmental sensor node - application entry point.
 *
 * Boot order matters:
 *   0. board power pins     (straps that must not be left floating, so this
 *                            comes before anything else touches a GPIO)
 *   1. history buffer       (nothing else may use it before this)
 *   2. status LED           (gives immediate visual feedback)
 *   3. sensors              (so the first read already has real values)
 *   4. Bluetooth            (brings up the controller and registers the
 *                            statically defined GATT service)
 *   5. advertising + cadence
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hal/nrf_power.h>

#include "app_config.h"
#include "board_power.h"
#include "env_sensor.h"
#include "env_service.h"
#include "history.h"
#include "power.h"
#include "status_led.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	/*
	 * Before anything else, including the banner: put the board's power
	 * straps into their non-drawing state. See board_power.c for why a
	 * floating strap on the charger costs more than the whole firmware
	 * budget.
	 */
	board_power_init();

	LOG_INF("=================================================");
	LOG_INF(" BLE environmental sensor node v%s", APP_VERSION_STRING);
	LOG_INF(" board: %s", CONFIG_BOARD_TARGET);
	LOG_INF(" build: nRF Connect SDK " APP_NCS_VERSION_STRING);
	LOG_INF("=================================================");

#if defined(NRF_POWER)
	/*
	 * Clear the reset-reason flags. This is the application's job, as the
	 * bootloader's own main.c says: "RESETREAS is not cleared by
	 * bootloader, it should be cleared by application upon init()".
	 *
	 * It matters because RESETREAS is sticky - a bit stays set until it is
	 * written back - and the bootloader reads the RESETPIN bit to decide
	 * whether a reset came from the reset pin. That bit is half of the
	 * double-tap-to-DFU condition:
	 *
	 *     dfu_start = (*dbl_reset_mem == DFU_DBL_RESET_MAGIC)
	 *                 && reason_reset_pin
	 *
	 * Leaving stale bits set makes the bootloader see "pin reset" for
	 * resets that were not, which turns a 500 ms timing window into
	 * something that only half works.
	 */
	NRF_POWER->RESETREAS = NRF_POWER->RESETREAS;
#endif

	history_init();
	status_led_init();

	if (env_sensor_init() != 0) {
		LOG_ERR("sensor init failed - the service will report invalid values");
	}

	int err = bt_enable(NULL);

	if (err != 0) {
		LOG_ERR("bt_enable failed (%d)", err);
		return 0;
	}
	LOG_INF("Bluetooth stack enabled");

	err = env_service_init();
	if (err != 0) {
		LOG_ERR("GATT service init failed (%d)", err);
	}

	power_init();

	/* Everything from here on is event driven: the work queue samples,
	 * the Bluetooth host takes care of the radio. Sleeping forever lets
	 * the idle thread park the CPU in System ON Idle.
	 */
	for (;;) {
		k_sleep(K_FOREVER);
	}
}
