#!/usr/bin/env python3
"""Create a clean-tree Tahoe candidate provenance manifest.

The manifest binds one immutable source commit and source identity to one local
KEXT ZIP.  It contains only candidate digests and typed build metadata; it
never installs, loads, or probes a guest.  A later identity capture validates
this manifest against the archive before it binds installed and loaded code.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

from tahoe_source_identity import source_identity


SCHEMA_VERSION = "itlwm-tahoe-candidate-provenance/v1"
SOURCE_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
SEMANTIC_RELEASE_RE = re.compile(
    r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?"
)


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return Path(result.stdout.strip())


def git_text(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=str(root),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def require_clean_head(root: Path) -> str:
    status = git_text(root, "status", "--porcelain=v1", "--untracked-files=all")
    if status:
        raise ValueError("candidate source worktree is not clean")
    head = git_text(root, "rev-parse", "HEAD").lower()
    if SOURCE_COMMIT_RE.fullmatch(head) is None:
        raise ValueError("candidate source HEAD is not a full commit")
    return head


def candidate_from_manifest(document: object) -> dict[str, Any]:
    if not isinstance(document, dict) or document.get("schema") != SCHEMA_VERSION:
        raise ValueError("candidate provenance schema")
    candidate = document.get("candidate")
    if not isinstance(candidate, dict):
        raise ValueError("candidate provenance candidate section")
    source_commit = candidate.get("source_commit")
    source_identity = candidate.get("source_identity_sha256")
    source_paths = candidate.get("source_identity_paths_count")
    release_tag = candidate.get("release_tag")
    if not isinstance(source_commit, str) or SOURCE_COMMIT_RE.fullmatch(source_commit) is None:
        raise ValueError("candidate provenance source commit")
    if not isinstance(source_identity, str) or SHA256_RE.fullmatch(source_identity) is None:
        raise ValueError("candidate provenance source identity")
    if not isinstance(source_paths, int) or source_paths < 1:
        raise ValueError("candidate provenance source-path count")
    if not isinstance(release_tag, str) or SEMANTIC_RELEASE_RE.fullmatch(release_tag) is None:
        raise ValueError("candidate provenance semantic release tag")
    for key in ("archive_sha256", "binary_sha256"):
        value = candidate.get(key)
        if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
            raise ValueError(f"candidate provenance {key}")
    uuid = candidate.get("macho_uuid")
    if not isinstance(uuid, str) or UUID_RE.fullmatch(uuid) is None:
        raise ValueError("candidate provenance Mach-O UUID")
    if candidate.get("bundle_id") != "com.zxystd.AirportItlwm":
        raise ValueError("candidate provenance bundle identifier")
    return candidate


def load_candidate_provenance(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise ValueError(f"candidate provenance read: {exc}") from exc
    return candidate_from_manifest(document)


def bind_candidate_provenance(
    provenance: dict[str, Any], archive_identity: dict[str, str]
) -> dict[str, str]:
    for key in ("release_tag", "archive_sha256", "binary_sha256", "macho_uuid"):
        if provenance.get(key) != archive_identity.get(key):
            raise ValueError(f"candidate provenance does not match archive {key}")
    bound = archive_identity.copy()
    bound["source_commit"] = str(provenance["source_commit"])
    bound["source_identity_sha256"] = str(provenance["source_identity_sha256"])
    return bound


def make_manifest(root: Path, release_zip: Path, release_tag: str) -> dict[str, object]:
    if SEMANTIC_RELEASE_RE.fullmatch(release_tag) is None:
        raise ValueError("release tag is not a semantic version")
    # Import lazily: the read-only identity capture imports this module to
    # validate a manifest, while creation is the only path that needs archive
    # parsing.  This keeps that validation path free of an import cycle.
    from capture_tahoe_lab_kext_identity import release_identity

    source_commit = require_clean_head(root)
    archive = release_identity(release_zip, release_tag)
    identity = source_identity(root, "HEAD")
    return {
        "schema": SCHEMA_VERSION,
        "candidate": {
            "source_commit": source_commit,
            "source_identity_sha256": identity["identity"],
            "source_identity_paths_count": identity["included_paths_count"],
            "release_tag": archive["release_tag"],
            "archive_sha256": archive["archive_sha256"],
            "binary_sha256": archive["binary_sha256"],
            "macho_uuid": archive["macho_uuid"],
            "bundle_id": archive["bundle_id"],
            "bundle_version": archive["bundle_version"],
            "short_version": archive["short_version"],
        },
        "creation": {
            "source_tree_clean": True,
            "archive_validated": True,
        },
        "non_claims": {
            "candidate_kext_installed": False,
            "candidate_kext_loaded": False,
            "auxkc_admission": False,
            "guest_rebooted": False,
            "association_tested": False,
            "data_transfer_tested": False,
        },
    }


def write_json(document: dict[str, object], output: str) -> None:
    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if output == "-":
        sys.stdout.write(rendered)
        return
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(rendered, encoding="utf-8")


def self_test() -> int:
    archive = {
        "release_tag": "v2.4.0-alpha",
        "archive_sha256": "a" * 64,
        "binary_sha256": "b" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
        "bundle_id": "com.zxystd.AirportItlwm",
    }
    candidate = {
        "source_commit": "c" * 40,
        "source_identity_sha256": "d" * 64,
        "source_identity_paths_count": 1,
        **archive,
    }
    bound = bind_candidate_provenance(candidate, archive)
    if bound["source_commit"] != "c" * 40:
        raise SystemExit("self-test: source commit did not bind")
    document = {"schema": SCHEMA_VERSION, "candidate": candidate}
    if candidate_from_manifest(document)["source_identity_sha256"] != "d" * 64:
        raise SystemExit("self-test: source identity did not round-trip")
    candidate["archive_sha256"] = "e" * 64
    try:
        bind_candidate_provenance(candidate, archive)
    except ValueError:
        pass
    else:
        raise SystemExit("self-test: mismatched archive was accepted")
    print("PASS: Tahoe candidate provenance self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-zip", type=Path)
    parser.add_argument("--release-tag")
    parser.add_argument("--output", default="-")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.release_zip is None or args.release_tag is None:
        parser.error("--release-zip and --release-tag are required unless --self-test is used")
    try:
        document = make_manifest(repo_root(), args.release_zip, args.release_tag)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: Tahoe candidate provenance: {exc}", file=sys.stderr)
        return 2
    write_json(document, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
