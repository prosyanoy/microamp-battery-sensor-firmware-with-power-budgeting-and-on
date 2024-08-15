#!/usr/bin/env bash
# Regenerate nanopb sources (committed under firmware/proto/).
set -euo pipefail
cd "$(dirname "$0")/.."
.venv/bin/nanopb_generator -I proto -D firmware/proto proto/microamp.proto
