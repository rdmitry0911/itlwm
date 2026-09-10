#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
# Extract the production value layout; fixtures supply only kernel boundaries.
awk '
    /^#define[[:space:]]+IEEE80211_(CHAN_MAX|NWID_LEN)[[:space:]]/ { print }
    /^#define IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS/ { selected=1 }
    selected { print }
    selected && /^};/ { selected=0 }
' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
  "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h"
