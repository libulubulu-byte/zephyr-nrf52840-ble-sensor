#!/usr/bin/env bash
# Zephyr's build-time python packages (no sudo needed - goes into the venv).
set -uo pipefail

export https_proxy=http://127.0.0.1:7892
export http_proxy=http://127.0.0.1:7892

# shellcheck disable=SC1091
source "$HOME/ncs-venv/bin/activate"

echo '=== pip install zephyr requirements ==='
pip install -q -r "$HOME/ncs/zephyr/scripts/requirements.txt" 2>&1 | tail -5

echo
echo '=== verify the ones the build actually needs ==='
python3 -c "import pykwalify; print('pykwalify   OK')"       2>&1
python3 -c "import yaml;      print('pyyaml      OK')"       2>&1
python3 -c "import elftools;  print('pyelftools  OK')"       2>&1
python3 -c "import packaging; print('packaging   OK')"       2>&1
python3 -c "import intelhex;  print('intelhex    OK')"       2>&1
