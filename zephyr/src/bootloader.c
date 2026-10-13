/*
 * Hand the machine over to the Adafruit nRF52 bootloader.
 *
 * The contract is a magic value in NRF_POWER->GPREGRET, which survives a soft
 * reset. The values below are copied from the bootloader's own src/main.c
 * (the source for this board ships in the DRV2605L project under
 * tools/bootloader-src):
 *
 *   0x57  DFU_MAGIC_UF2_RESET          -> CDC + MSC, i.e. the NICENANO volume
 *   0x4e  DFU_MAGIC_SERIAL_ONLY_RESET  -> CDC only, for adafruit-nrfutil
 *   0xa8  DFU_MAGIC_OTA_RESET          -> BLE OTA
 *   0xb1  DFU_MAGIC_OTA_APPJUM         -> BLEDfu service, SoftDevice running
 *
 * 0x57 is the one that gives drag-and-drop.
 *
 * Why not the 1200 bps touch that Arduino boards use: that trick is handled by
 * the *application*, which watches for a 1200 baud line coding on the CDC port.
 * Zephyr's CDC ACM driver records the host's SET_LINE_CODING request and lets
 * you read it back with uart_line_ctrl_get(UART_LINE_CTRL_BAUD_RATE), but it
 * exposes no notification when it changes, so the only way to notice would be
 * to poll - which would burn part of a 50 uA budget on a feature that matters
 * once per firmware release. Doing it from the BLE control point instead costs
 * nothing and works from the same phone that is already connected.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <hal/nrf_power.h>

#include "bootloader.h"

LOG_MODULE_REGISTER(bootloader, LOG_LEVEL_INF);

#define DFU_MAGIC_UF2_RESET 0x57

void bootloader_request(void)
{
	LOG_INF("bootloader: requesting UF2 mode (GPREGRET=0x%02x), resetting now",
		DFU_MAGIC_UF2_RESET);

	/* GPREGRET is retained across a soft reset, which is the whole point
	 * of the handshake.
	 */
	NRF_POWER->GPREGRET = DFU_MAGIC_UF2_RESET;

	/* Let the deferred log backend drain before the core goes away. */
	k_sleep(K_MSEC(100));

	sys_reboot(SYS_REBOOT_COLD);
}
