#!/usr/bin/env bash
# Thin source-rooted entry point for the strict guest-only activation bridge.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
exec /usr/bin/python3 -E -s "$ROOT/scripts/run_tahoe_iwn_candidate_activation_reboot.py" "$@"
