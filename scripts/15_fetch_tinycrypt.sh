#!/usr/bin/env bash
# tinycrypt is what Zephyr's Bluetooth host uses for AES-CMAC (BT_HOST_CRYPTO).
# It was filtered out of the module set and the build needs it, so fetch it -
# the tarball is well under a megabyte.
set -uo pipefail

PX=http://127.0.0.1:7892
REV=1012a3ebee18c15ede5efc8332ee2fc37817670f
DEST="$HOME/ncs/modules/crypto/tinycrypt"
TB="$HOME/tarballs/zephyrproject-rtos_tinycrypt_${REV}.tar.gz"
URL="https://codeload.github.com/zephyrproject-rtos/tinycrypt/tar.gz/${REV}"

if [ -d "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
    echo "tinycrypt already present"
    exit 0
fi

echo "fetching tinycrypt @ $REV"
for i in 1 2 3 4 5; do
    curl -sL --connect-timeout 30 --max-time 600 --retry 2 --retry-delay 3 \
         -o "$TB" -x "$PX" "$URL"
    if gzip -t "$TB" 2>/dev/null; then break; fi
    echo "  retry $i"
    sleep 3
done

gzip -t "$TB" || { echo "FAILED to download tinycrypt"; exit 1; }

mkdir -p "$DEST"
rm -rf "${DEST:?}"/*
tar -xzf "$TB" -C "$DEST" --strip-components=1
echo "extracted -> $DEST ($(du -sh "$DEST" | cut -f1))"
ls "$DEST" | head
