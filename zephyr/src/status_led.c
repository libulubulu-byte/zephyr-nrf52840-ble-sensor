/*
 * Connection indicator.
 *
 * Deliberately not a heartbeat: see app_config.h for the current budget
 * reasoning. The LED is off whenever no central is connected, which is the
 * state the <50 uA measurement is taken in.
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "status_led.h"

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_ALIAS(led0))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define LED_PRESENT 1
#else
#define LED_PRESENT 0
#endif

/*
 * Turn the LED on or off.
 *
 * "Off" is a push-pull output held at the inactive level - NOT a disconnected
 * pin.
 *
 * It was briefly the other way round, on the reasoning that a high-Z pin is
 * dark under both common-anode and common-cathode wiring and so avoids having
 * to know which this board uses. That reasoning is sound as far as it goes, and
 * it was reverted anyway, for a reason worth recording: one product build with
 * the pin disconnected measured 200 uA where one with it driven measured
 * 103 uA, and the 97 uA difference was attributed to this pin.
 *
 * The bare-board probe then contradicted it - 99 uA with the pin floating, 99
 * uA with it driven - so the attribution was wrong, and the reverted behaviour
 * was restored because it matches the board's own state rather than because a
 * number demanded it.
 *
 * The polarity question is still open. The board definition says
 * GPIO_ACTIVE_HIGH (from the bootloader's board.h) while the board's own notes
 * describe its LEDs as common-anode - which would mean this LED is lit whenever
 * the firmware believes it is off. If it is visibly lit with no central
 * connected, that is the answer, and it is worth exactly the LED's current
 * rather than a guess.
 */
void status_led_set(bool on)
{
#if LED_PRESENT
	if (!device_is_ready(led.port)) {
		return;
	}
	gpio_pin_set_dt(&led, on ? 1 : 0);
#else
	ARG_UNUSED(on);
#endif
}

void status_led_init(void)
{
#if LED_PRESENT
	if (!device_is_ready(led.port)) {
		LOG_WRN("LED port not ready");
		return;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
		LOG_WRN("could not configure LED");
		return;
	}

	/* Three short blinks say "the application is running", then off. */
	for (int i = 0; i < 3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_sleep(K_MSEC(60));
		gpio_pin_set_dt(&led, 0);
		k_sleep(K_MSEC(120));
	}

	LOG_INF("status LED on %s.%u (off while disconnected)",
		led.port->name, led.pin);
#else
	LOG_WRN("no led0 alias in the devicetree, status LED disabled");
#endif
}

#if APP_LED_CONNECTED_ENABLE
static void led_connected(struct bt_conn *conn, uint8_t err)
{
	if (err == 0U) {
		status_led_set(true);
	}
}

static void led_disconnected(struct bt_conn *conn, uint8_t reason)
{
	status_led_set(false);
}

BT_CONN_CB_DEFINE(status_led_callbacks) = {
	.connected = led_connected,
	.disconnected = led_disconnected,
};
#endif /* APP_LED_CONNECTED_ENABLE */

#if APP_LED_HEARTBEAT_ENABLE
static void heartbeat(struct k_work *work)
{
	status_led_set(true);
	k_sleep(K_MSEC(3));
	status_led_set(false);
	k_work_reschedule((struct k_work_delayable *)work, K_SECONDS(1));
}
#endif
