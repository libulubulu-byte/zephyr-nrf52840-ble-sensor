/*
 * Board-level pins that are not application resources but still have to be
 * driven into a defined state.
 *
 * The distinction this file exists to make:
 *
 *   "Unused pin"     -> no trace, no net. Nordic's guidance is to leave it
 *                       disconnected (input, buffer off) and it costs nothing.
 *   "Unconnected by   -> a pin that is *wired to something on the board*.
 *    the application"    Leaving that one floating is not the same thing at
 *                       all: it hands an input of some other chip an
 *                       undefined level, and whatever that chip does with it
 *                       is now out of your control.
 *
 * The application never "uses" the pin below, which is exactly why it was
 * missed. It is not an LED, not a button, not a bus - it is a strap on a power
 * chip, and a strap has to be strapped.
 *
 * ---------------------------------------------------------------------------
 * P0.13 - HICHG, the charger's charge-current select
 *
 * Taken from the board's own documentation (the debugging notes kept with the
 * Arduino project that ran on this hardware first, 调试记录.md):
 *
 *   "不要用 D13 当 LED 使用（它是绿灯；且 P0.13 是充电电流配置脚 HICHG，
 *    误接会干扰充电设置）"
 *
 * So P0.13 is not an LED pin and not a spare: it is the strapping input that
 * selects the LiPo charger's charge current. The charger is powered whenever
 * the board is powered, so this pin is wired to something that matters even
 * though the application never uses it.
 *
 * ---------------------------------------------------------------------------
 * WHICH LEVEL IS THE QUIET ONE IS NOT YET KNOWN - AND THAT IS RECORDED HERE
 * RATHER THAN GUESSED AT
 *
 * The first version of this file drove the pin low, on the reasoning that a
 * strap should be strapped and that low is the conservative choice. The meter
 * said otherwise, on the bare-board probe with nothing else running:
 *
 *     P0.13 as input, disconnected     102 uA
 *     P0.13 driven low                 600 uA
 *
 * Six times worse. So LOW is *not* this board's quiet setting, and "drive it
 * low because low sounds safe" was an assumption that measurement overturned.
 *
 * Until the bare-board probe's three-state sweep (STRAP_FLOAT / STRAP_LOW /
 * STRAP_HIGH, one minute each - see power_test/src/main.c) says which level
 * is both defined and quiet, this function leaves the pin in the state that
 * was actually measured to be the best one. That is deliberately not a
 * permanent answer: a driven pin is a stronger guarantee than a floating one,
 * and the sweep exists to replace this with a real level.
 *
 * There is one useful thing the 6x swing already tells us: HICHG only changes
 * anything while the charger is running, and a charger only runs when it has
 * an input. A 500 uA swing from this single pin is therefore evidence that
 * whatever is supplying the board is reaching the charger - which means a
 * measurement taken that way reads "charger plus node", not the node.
 *
 * ---------------------------------------------------------------------------
 * Why nrfx rather than the Zephyr GPIO API
 *
 * The point of this call is to be early and unconditional. Going through
 * device_is_ready() plus gpio_pin_configure_dt() would need the GPIO driver to
 * have initialised, and would silently do nothing if it had not. A strap on a
 * power chip should not depend on driver init order.
 */

#include <zephyr/kernel.h>

#include <hal/nrf_gpio.h>

#include "board_power.h"

/*
 * Charger charge-current select (HICHG).
 *
 * Left disconnected, and that is a measurement rather than a default:
 *
 *     disconnected   102 uA
 *     driven low     600 uA
 *
 * Six times worse when driven low, so the pin stays as the reset state leaves
 * it. See the file comment for why this is a decision at all.
 */
#define BOARD_PIN_CHARGER_HICHG NRF_GPIO_PIN_MAP(0, 13)

/*
 * P0.15 - the pin the board definition calls led0.
 *
 * It is driven low here, in board_power_init(), rather than left to the LED
 * module, because whether it is driven is a property of the board and not of
 * the LED feature: a pin wired to something on the board should not depend on
 * application code that might be disabled or deleted.
 *
 * This is not claimed to save current. It was briefly believed to - a product
 * build that left the pin disconnected measured 200 uA where one that drove it
 * measured 103 - but the bare-board probe, which floats every pin, measured
 * 99 uA either way. The attribution did not survive its control, so what is
 * recorded here is the honest version: the level is defined because an
 * undefined pin on a wired net is a bad idea, not because a number says so.
 */
#define BOARD_PIN_STATUS_LED NRF_GPIO_PIN_MAP(0, 15)

void board_power_init(void)
{
	/*
	 * P0.15 first: it is the expensive one. nrf_gpio_cfg_output() clears
	 * the pin before switching the direction to output, so there is no
	 * glitch high on the way in.
	 */
	nrf_gpio_cfg_output(BOARD_PIN_STATUS_LED);
	nrf_gpio_pin_clear(BOARD_PIN_STATUS_LED);

	/*
	 * P0.13 keeps the state a reset leaves it in, deliberately: driving it
	 * low was measured at six times the current. Written out anyway so the
	 * decision is visible in the code rather than implied by an absence.
	 */
	nrf_gpio_cfg_default(BOARD_PIN_CHARGER_HICHG);
}
