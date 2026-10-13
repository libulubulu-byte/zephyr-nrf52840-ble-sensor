#!/usr/bin/env bash
# ============================================================
#  Install NCS/Zephyr build dependencies
#  RUN THIS YOURSELF - it needs your sudo password.
#
#    wsl -d Ubuntu-22.04 -- bash /mnt/d/ESP32-IDF/ESP32S3_Zephyr/scripts/00_apt_deps.sh
#
#  NOTE: no packages are installed under C:\ - WSL root is D:\wsl\Ubuntu-22.04
# ============================================================
set -e

# This machine reaches the internet only through the local proxy.
export http_proxy=http://127.0.0.1:7892
export https_proxy=http://127.0.0.1:7892
export HTTP_PROXY="$http_proxy"
export HTTPS_PROXY="$https_proxy"
# Ubuntu archives are reachable directly in most cases; keep them out of the proxy.
export no_proxy="localhost,127.0.0.1,archive.ubuntu.com,security.ubuntu.com,cn.archive.ubuntu.com,mirrors.aliyun.com,mirrors.tuna.tsinghua.edu.cn"

echo "=== apt update ==="
sudo -E apt-get update

echo
echo "=== apt install build deps ==="
sudo -E apt-get install -y --no-install-recommends \
    git cmake ninja-build gperf ccache dfu-util device-tree-compiler \
    wget python3-dev python3-pip python3-setuptools python3-tk python3-wheel \
    xz-utils file make gcc gcc-multilib g++-multilib \
    libsdl2-dev libmagic1 unzip

echo
echo "=== verification ==="
for t in cmake ninja dtc gperf ccache make gcc unzip file; do
    printf '%-9s ' "$t"
    command -v "$t" >/dev/null 2>&1 && echo "OK  ($($t --version 2>/dev/null | head -1))" || echo "STILL MISSING"
done

echo
echo "DONE - build dependencies installed."
