#!/usr/bin/env bash
# ============================================================
#  Build the BLE environmental sensor node (run inside WSL).
#
#    bash scripts/build.sh            # build
#    bash scripts/build.sh clean      # wipe the build directory
#
#  Why raw CMake instead of "west build":
#  a full NCS workspace needs the sdk-nrf manifest repository and a
#  .west/config, and this machine reaches github at 10-250 KB/s. The
#  sources are therefore fetched as resumable tarballs and wired up with
#  ZEPHYR_MODULES, which needs no west workspace at all. A normal machine
#  with "west init/update" gets the identical result from
#  "west build -b nrf52840_supermini zephyr/".
# ============================================================
set -euo pipefail

NCS="${NCS_ROOT:-$HOME/ncs}"
SDK="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-0.16.8}"
APP_SRC="/mnt/d/ESP32-IDF/ESP32S3_Zephyr/zephyr"
BUILD="${BUILD_DIR:-$HOME/build/env_node}"
OUT="/mnt/d/ESP32-IDF/ESP32S3_Zephyr/firmware"

# shellcheck disable=SC1091
source "$HOME/ncs-venv/bin/activate"

export ZEPHYR_BASE="$NCS/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$SDK"

# dtc comes from the Zephyr SDK's host tools bundle rather than from apt, so
# it has to be on PATH. (gperf is only needed for userspace/kobject builds.)
HOSTTOOLS="$HOME/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin"
if [ -x "$HOSTTOOLS/dtc" ]; then
    export PATH="$HOSTTOOLS:$PATH"
fi

# Only the modules this application actually uses. Zephyr treats this as the
# complete list, which also keeps a half-fetched module from breaking the
# build.
export ZEPHYR_MODULES="$NCS/modules/hal/nordic;$NCS/modules/hal/cmsis;$NCS/modules/lib/picolibc;$NCS/modules/crypto/tinycrypt"

if [ "${1:-}" = "clean" ]; then
    rm -rf "$BUILD"
    echo "removed $BUILD"
    exit 0
fi

mkdir -p "$BUILD" "$OUT"

echo "=== configuring ==="
cmake -B "$BUILD" -GNinja \
      -DBOARD=nrf52840_supermini \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DZEPHYR_MODULES="$ZEPHYR_MODULES" \
      "$APP_SRC"

echo
echo "=== building ==="
ninja -C "$BUILD"

echo
echo "=== artifacts ==="
ls -l "$BUILD/zephyr/zephyr.elf" "$BUILD/zephyr/zephyr.hex" \
      "$BUILD/zephyr/zephyr.uf2" 2>/dev/null || true

# Copy the flashable images next to the sources so they can be dragged onto
# the NICENANO volume straight from Windows.
mkdir -p "$OUT"
cp -f "$BUILD/zephyr/zephyr.uf2" "$OUT/" 2>/dev/null || true
cp -f "$BUILD/zephyr/zephyr.hex" "$OUT/" 2>/dev/null || true

echo
echo "=== memory usage ==="
"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-size" "$BUILD/zephyr/zephyr.elf" || true
echo
echo "output copied to $OUT"
