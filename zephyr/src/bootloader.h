/*
 * Entry point into the Adafruit UF2 bootloader.
 *
 * The board cannot be reflashed over SWD, so getting into the bootloader
 * matters: without this, the only way in is a physical double-tap of the
 * reset pad, which means opening the enclosure.
 */

#ifndef BOOTLOADER_H_
#define BOOTLOADER_H_

/**
 * Reboot into the Adafruit UF2 bootloader (NICENANO mass-storage volume).
 *
 * Does not return.
 */
void bootloader_request(void);

#endif /* BOOTLOADER_H_ */
