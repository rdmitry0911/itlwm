#!/usr/bin/env bash
# Compatibility entry point.  Generic MFP is no longer quarantined: the
# complete AX211/API-68 transaction-owner contract is its replacement.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
exec bash "$root/scripts/test_tahoe_ax211_api68_pmf_transaction_owner_contract.sh"
