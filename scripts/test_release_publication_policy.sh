#!/usr/bin/env bash
# Static guard: ordinary CI pushes must never create tags or GitHub releases.
# A single semantic release is handled by the verified runtime-release path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
workflow="$root/.github/workflows/main.yml"

python3 - "$workflow" <<'PY'
from pathlib import Path
import sys

workflow = Path(sys.argv[1]).read_text()

required = (
    "permissions:\n  contents: read",
    "Check release publication policy",
    "CI is build-only.",
    "semantic version change.",
)
for needle in required:
    assert needle in workflow, needle

forbidden = (
    "contents: write",
    "dev-drprasad/delete-tag-and-release@",
    "ncipollo/release-action@",
    "gh release ",
    "GITHUB_TOKEN:",
    "Publish GitHub Release",
    "Delete Old Prerelease",
)
for needle in forbidden:
    assert needle not in workflow, needle

print("PASS: master CI is build-only; semantic release publication is external")
PY
