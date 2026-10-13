#!/usr/bin/env bash
# ============================================================
#  Step 1 (v3): fetch NCS sources as RESUMABLE tarballs.
#
#  git clone over this proxy drops mid-transfer ("fetch-pack:
#  unexpected disconnect"), and every retry restarts from zero.
#  codeload tarballs + `curl -C -` resume instead, so they always
#  finish. Revisions are taken verbatim from the NCS v2.7.0 manifest
#  (sdk-zephyr v3.6.99-ncs2 west.yml) so this is a genuine NCS tree.
#
#  Resulting layout is west-compatible:
#    ~/ncs/zephyr, ~/ncs/nrf, ~/ncs/nrfxlib,
#    ~/ncs/modules/hal/nordic, ~/ncs/modules/hal/cmsis, ...
# ============================================================
set -uo pipefail

PX=http://127.0.0.1:7892
export https_proxy=$PX http_proxy=$PX
NCS="$HOME/ncs"
TB="$HOME/tarballs"
CL=https://codeload.github.com
mkdir -p "$TB"

fetch() {
    local repo="$1" ref="$2" dest="$3" label="$4"
    local name url tb
    name=$(echo "$repo" | tr '/' '_')
    url="$CL/$repo/tar.gz/$ref"
    tb="$TB/${name}_$(echo "$ref" | tr '/' '_').tar.gz"

    if [ -d "$dest" ] && [ -n "$(ls -A "$dest" 2>/dev/null)" ]; then
        echo "== $label : already present, skipping"
        return 0
    fi

    echo "== $label"
    echo "   $url"

    if ! ( [ -f "$tb" ] && gzip -t "$tb" 2>/dev/null ); then
        local ok=0
        for i in $(seq 1 25); do
            curl -L --connect-timeout 30 --max-time 1800 \
                 --retry 3 --retry-delay 5 --retry-all-errors \
                 -C - -o "$tb" -x "$PX" "$url" >/dev/null 2>&1
            if gzip -t "$tb" 2>/dev/null; then ok=1; break; fi
            printf '   resume attempt %s (%s so far)\n' "$i" "$(du -h "$tb" 2>/dev/null | cut -f1)"
            sleep 3
        done
        if [ "$ok" != 1 ]; then
            echo "   FAILED: $label"
            rm -f "$tb"
            return 1
        fi
    fi

    echo "   size: $(du -h "$tb" | cut -f1)"
    mkdir -p "$dest"
    rm -rf "${dest:?}"/*
    tar -xzf "$tb" -C "$dest" --strip-components=1
    echo "   extracted -> $dest ($(du -sh "$dest" | cut -f1))"
}

echo "########## NCS source fetch started $(date -Is) ##########"

# ---- tier 1: required to build -------------------------------------
fetch nrfconnect/sdk-zephyr        v3.6.99-ncs2 "$NCS/zephyr"                  "zephyr (NCS fork)"
fetch zephyrproject-rtos/hal_nordic ab5cb2e2faeb1edfad7a25286dcb513929ae55da   "$NCS/modules/hal/nordic" "hal_nordic"
fetch zephyrproject-rtos/cmsis     4b96cbb174678dcd3ca86e11e1f24bc5f8726da0   "$NCS/modules/hal/cmsis"  "cmsis"
fetch zephyrproject-rtos/picolibc  764ef4e401a8f4c6a86ab723533841f072885a5b   "$NCS/modules/lib/picolibc" "picolibc"

# ---- tier 2: NCS glue, BLE controller, crypto ----------------------
fetch nrfconnect/sdk-nrf           v2.7.0 "$NCS/nrf"                 "nrf (NCS app/Kconfig glue)"
fetch nrfconnect/sdk-mbedtls       v3.5.2-ncs2 "$NCS/modules/crypto/mbedtls" "mbedtls (NCS fork)"
fetch nrfconnect/sdk-nrfxlib       v2.7.0 "$NCS/nrfxlib"             "nrfxlib (SoftDevice Controller)"

echo "########## DONE $(date -Is) ##########"
du -sh "$NCS"
df -h / | tail -1
