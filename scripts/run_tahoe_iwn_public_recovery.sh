#!/usr/bin/env bash
# Thin entrypoint for the bounded public-CoreWLAN recovery supervisor.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
exec /usr/bin/python3 -I "$ROOT/scripts/run_tahoe_iwn_public_recovery.py" "$@"
