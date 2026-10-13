# SPDX-License-Identifier: Apache-2.0
#
# Flashing.
#
# The SuperMini / nice!nano family has no on-board debug probe, so UF2 over
# the Adafruit bootloader is the normal path:
#
#   1. double-tap reset (or short RST to GND twice) to enter the bootloader
#   2. a mass-storage volume labelled NICENANO appears
#   3. west flash --runner uf2      (copies zephyr.uf2 onto that volume)
#
# A real SWD probe still works if one is attached.

include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)

board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")

include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
