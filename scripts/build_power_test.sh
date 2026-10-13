#!/usr/bin/env bash
# ============================================================
#  Build the bare-board current probe (run inside WSL).
#
#    bash scripts/build_power_test.sh
#
#  Same toolchain setup as build.sh, but a different application directory and
#  a BOARD_ROOT pointing at the product app so the shared board definition
#  (../zephyr/boards/nordic/nrf52840_supermini) is found.
#
#  Output goes to firmware/power_test.uf2 so both images can coexist and
#  flash_wait.ps1 -Image can pick either one.
# ============================================================
set -euo pipefail

NCS="${NCS_ROOT:-$HOME/ncs}"
SDK="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-0.16.8}"
REPO="/mnt/d/ESP32-IDF/ESP32S3_Zephyr"
APP_SRC="$REPO/power_test"
BUILD="${BUILD_DIR:-$HOME/build/power_test}"
OUT="$REPO/firmware"

# shellcheck disable=SC1091
source "$HOME/ncs-venv/bin/activate"

export ZEPHYR_BASE="$NCS/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$SDK"

HOSTTOOLS="$HOME/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin"
if [ -x "$HOSTTOOLS/dtc" ]; then
    export PATH="$HOSTTOOLS:$PATH"
fi

export ZEPHYR_MODULES="$NCS/modules/hal/nordic;$NCS/modules/hal/cmsis;$NCS/modules/lib/picolibc;$NCS/modules/crypto/tinycrypt"

if [ "${1:-}" = "clean" ]; then
    rm -rf "$BUILD"
    echo "removed $BUILD"
    exit 0
fi

mkdir -p "$BUILD" "$OUT"

echo "=== configuring (BOARD_ROOT=$REPO/zephyr) ==="
cmake -B "$BUILD" -GNinja \
      -DBOARD=nrf52840_supermini \
      -DBOARD_ROOT="$REPO/zephyr" \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DZEPHYR_MODULES="$ZEPHYR_MODULES" \
      "$APP_SRC"

echo
echo "=== building ==="
ninja -C "$BUILD"

echo
echo "=== artifacts ==="
ls -l "$BUILD/zephyr/zephyr.elf" "$BUILD/zephyr/zephyr.uf2" 2>/dev/null || true
cp -f "$BUILD/zephyr/zephyr.uf2" "$OUT/power_test.uf2"

echo
echo "=== memory usage ==="
"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-size" "$BUILD/zephyr/zephyr.elf" || true
echo
echo "output copied to $OUT/power_test.uf2"
