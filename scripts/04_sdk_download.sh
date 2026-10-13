#!/usr/bin/env bash
# ============================================================
#  Step 2: download the Zephyr SDK (minimal) and register it
#  Runs in parallel with the NCS fetch.
# ============================================================
set -uo pipefail

export https_proxy=http://127.0.0.1:7892
export http_proxy=http://127.0.0.1:7892

SDK_VER="0.16.8"
SDK_DIR="$HOME/zephyr-sdk-$SDK_VER"
TARBALL="$HOME/zephyr-sdk-${SDK_VER}_linux-x86_64_minimal.tar.xz"
URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VER}/zephyr-sdk-${SDK_VER}_linux-x86_64_minimal.tar.xz"

echo "########## Zephyr SDK $SDK_VER download started $(date -Is) ##########"

if [ -d "$SDK_DIR" ] && [ -f "$SDK_DIR/sdk_version" ]; then
    echo "SDK already extracted at $SDK_DIR"
else
    echo "--- downloading ($(echo '41.8MB') ) ---"
    curl -L --retry 5 --retry-delay 5 --retry-all-errors \
         -C - -o "$TARBALL" -x http://127.0.0.1:7892 "$URL"
    echo "downloaded: $(du -h "$TARBALL" | cut -f1)"
    echo "--- extracting ---"
    rm -rf "$SDK_DIR"
    tar -xf "$TARBALL" -C "$HOME"
fi

echo "--- what is inside the minimal package ---"
ls -1 "$SDK_DIR" | head -30

if [ -f "$SDK_DIR/setup.sh" ]; then
    echo "--- running setup.sh (registers west extension) ---"
    ( cd "$SDK_DIR" && ./setup.sh -h >/dev/null 2>&1 || true )
fi

echo "########## SDK BASE DONE $(date -Is) ##########"
du -sh "$SDK_DIR"
