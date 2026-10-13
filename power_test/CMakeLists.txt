# =============================================================================
#  Bare-board current probe.
#
#  A separate application rather than a Kconfig option on the product build:
#  the whole value of it is that the Bluetooth stack, the USB stack and the
#  console are genuinely not there, and a compile-time flag that has to remove
#  all of them is easier to get wrong than a build that never adds them.
#
#  It reuses the board definition from ../zephyr/boards, so it is built with
#
#      cmake -DBOARD_ROOT=<repo>/zephyr -DBOARD=nrf52840_supermini ...
#
#  (scripts/build_power_test.sh does exactly that).
# =============================================================================
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(power_test)

target_sources(app PRIVATE src/main.c)
