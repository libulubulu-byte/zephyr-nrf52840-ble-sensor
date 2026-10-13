#!/usr/bin/env bash
# ============================================================
#  Step 2b: install the arm-zephyr-eabi toolchain into the SDK.
#
#  The SDK "minimal" package contains no compilers. setup.sh would fetch the
#  toolchain from github.com directly, which is slow on this link, so we pull
#  the same asset through the accelerator instead and unpack it in place.
#
#  Asset naming comes from setup.sh itself:
#     dl_toolchain_filename='toolchain_${host}_${toolchain}.tar.xz'
#     dl_rel_base=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${version}
# ============================================================
set -uo pipefail

SDK_VER=0.16.8
SDK_DIR="$HOME/zephyr-sdk-$SDK_VER"
ASSET="toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz"
UPSTREAM="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VER}/${ASSET}"
PX=http://127.0.0.1:7892

if [ -d "$SDK_DIR/arm-zephyr-eabi" ]; then
    echo "toolchain already installed"
    exit 0
fi

echo "########## arm-zephyr-eabi toolchain $(date -Is) ##########"

downloaded=0
for prefix in \
    "https://ghfast.top/" \
    "https://gh-proxy.com/" \
    "https://ghproxy.net/" \
    "" ; do
    url="${prefix}${UPSTREAM}"
    echo "--- trying $url"
    if curl -L --connect-timeout 30 --max-time 2400 \
            --retry 2 --retry-delay 5 --retry-all-errors \
            -o "$HOME/$ASSET" -x "$PX" "$url"; then
        if xz -t "$HOME/$ASSET" 2>/dev/null; then
            echo "--- ok, $(du -h "$HOME/$ASSET" | cut -f1)"
            downloaded=1
            break
        fi
        echo "--- corrupt archive, retrying through the next mirror"
        rm -f "$HOME/$ASSET"
    fi
done

if [ "$downloaded" != 1 ]; then
    echo "FATAL: could not download the toolchain"
    exit 1
fi

echo "--- extracting into $SDK_DIR"
tar -xf "$HOME/$ASSET" -C "$SDK_DIR"
rm -f "$HOME/$ASSET"

echo "--- registering the SDK (setup.sh)"
( cd "$SDK_DIR" && ./setup.sh -t arm-zephyr-eabi >/dev/null 2>&1 || true )

echo "--- result"
ls -d "$SDK_DIR"/arm-zephyr-eabi 2>/dev/null && \
    "$SDK_DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version | head -1
du -sh "$SDK_DIR"

echo "########## DONE $(date -Is) ##########"
