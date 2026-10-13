#!/usr/bin/env bash
# ============================================================
#  Step 0: bootstrap python venv + west WITHOUT sudo
#  Root filesystem lives on D: (D:\wsl\Ubuntu-22.04) -> C: stays clean
# ============================================================
set -uo pipefail

NCS_ROOT="$HOME/ncs"
VENV="$HOME/ncs-venv"
PROXY="http://127.0.0.1:7892"

echo "=== [1] network sanity ==="
curl -s -o /dev/null -w 'github via proxy : %{http_code} in %{time_total}s\n' \
     --max-time 20 -x "$PROXY" https://github.com

echo
echo "=== [2] git proxy check (ls-remote, small) ==="
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
export HTTPS_PROXY="$PROXY" HTTP_PROXY="$PROXY"
git -c http.proxy="$PROXY" ls-remote --heads https://github.com/zephyrproject-rtos/zephyr.git main 2>&1 | head -3

echo
echo "=== [3] venv (bootstraps pip without sudo) ==="
if [ ! -d "$VENV" ]; then
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
python -V
pip -V

echo
echo "=== [4] pip install west + cmake + ninja ==="
pip install -q --upgrade pip setuptools wheel
pip install -q west cmake ninja
echo "west   : $(west --version 2>&1 | head -1)"
echo "cmake  : $(cmake --version | head -1)"
echo "ninja  : $(ninja --version)"

echo
echo "=== [5] disk ==="
df -h / | tail -1
echo "DONE"
