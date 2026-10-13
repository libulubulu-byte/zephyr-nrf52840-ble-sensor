# -----------------------------------------------------------------------------
#  BLE Environmental Sensor Node - Zephyr / nRF Connect SDK application
#
#  Board: nrf52840_supermini (out-of-tree board definition, see boards/)
#  Target: nRF52840 + SHT31 (I2C) + battery divider (SAADC)
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree board definitions live next to the application.
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(env_sensor_node)

target_sources(app PRIVATE
    src/main.c
    src/board_power.c
    src/env_sensor.c
    src/history.c
    src/env_service.c
    src/power.c
    src/status_led.c
    src/bootloader.c
)
