#!/usr/bin/env bash
# Fetch pinned third-party sources and create the Python venv.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party
clone() { # url tag dir
  [ -d "third_party/$3" ] || git clone -q --depth 1 --branch "$2" "$1" "third_party/$3"
}
clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git V11.1.0 FreeRTOS-Kernel
clone https://github.com/nanopb/nanopb.git 0.4.9.1 nanopb
clone https://github.com/ThrowTheSwitch/Unity.git v2.6.0 Unity
if [ ! -d .venv ]; then
  python3 -m venv .venv
  .venv/bin/pip -q install -r requirements.txt
  .venv/bin/pip -q install cmake 2>/dev/null || true
fi
echo "bootstrap done"
