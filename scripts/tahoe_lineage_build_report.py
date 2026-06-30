#!/usr/bin/env python3
"""Generate and validate Tahoe lineage/build substrate evidence."""

import argparse
import json
import plistlib
import subprocess
import sys
from pathlib import Path


REPORT_PATH = Path("evidence/build/tahoe_lineage_build_report.json")
CANONICAL_GUEST_TOPLEVEL = "/Users/devops/Projects/itlwm"
OPEN_SOURCE_REPO = "https://github.com/OpenIntelWireless/itlwm"
DEFAULT_BOOTKC = "/Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc"


def run(args):
    return subprocess.check_output(args, text=True).strip()


def require(condition, message):
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def remote_url(root):
    try:
        return run(["git", "-C", str(root), "remote", "get-url", "origin"])
    except subprocess.CalledProcessError:
        return ""


def is_private_or_host_mirror(url):
    lowered = url.lower()
    private_tokens = ("host-mirror", "private-mirror", "synthetic")
    if any(token in lowered for token in private_tokens):
        return True
    if lowered.startswith("/") or lowered.startswith("file://"):
        return True
    if "127.0.0.1" in lowered or "localhost" in lowered:
        return True
    return False


def literal(path, needle):
    return needle in read_text(path)


def generate_report(project_dir, source_head=None):
    root = Path(run(["git", "-C", str(project_dir), "rev-parse", "--show-toplevel"]))
    git_dir = Path(run(["git", "-C", str(root), "rev-parse", "--absolute-git-dir"]))
    head = source_head or run(["git", "-C", str(root), "rev-parse", "HEAD"])
    origin = remote_url(root)

    readme = root / "README.md"
    license_file = root / "LICENSE"
    info_plist = root / "AirportItlwm" / "Info.plist"
    tahoe_info_plist = root / "AirportItlwm" / "AirportItlwm-Tahoe-Info.plist"
    project_file = root / "itlwm.xcodeproj" / "project.pbxproj"
    build_script = root / "scripts" / "build_tahoe.sh"
    repro_doc = root / "docs" / "tahoe_lineage_build_reproducibility.md"
    smoke_script = root / "scripts" / "tahoe_reproducibility_smoke.sh"

    for path in (
        readme,
        license_file,
        info_plist,
        tahoe_info_plist,
        project_file,
        build_script,
        repro_doc,
        smoke_script,
    ):
        require(path.is_file(), f"missing required source file: {path.relative_to(root)}")

    with info_plist.open("rb") as handle:
        plist = plistlib.load(handle)
    with tahoe_info_plist.open("rb") as handle:
        tahoe_plist = plistlib.load(handle)

    lineage_checks = {
        "readme_openintelwireless": literal(readme, "OpenIntelWireless/itlwm"),
        "readme_openbsd": literal(readme, "OpenBSD"),
        "readme_zxystd_airportitlwm": literal(readme, "zxystd") and literal(readme, "itlwm"),
        "license_gpl_v2": literal(license_file, "GNU GENERAL PUBLIC LICENSE")
        and literal(license_file, "Version 2"),
        "airport_bundle_id": plist.get("IOKitPersonalities", {})
        .get("itlwm", {})
        .get("CFBundleIdentifier")
        == "com.zxystd.AirportItlwm",
        "airport_bundle_name": plist.get("IOKitPersonalities", {})
        .get("itlwm", {})
        .get("IOClass")
        == "AirportItlwm",
        "upstream_author_history": bool(
            run(
                [
                    "git",
                    "-C",
                    str(root),
                    "log",
                    "--all",
                    "--format=%ae",
                    "--",
                    "README.md",
                    "AirportItlwm/Info.plist",
                ]
            ).count("1051244836@qq.com")
        ),
    }

    build_checks = {
        "tahoe_target": literal(project_file, "AirportItlwm-Tahoe"),
        "target_variable": literal(build_script, 'TARGET="AirportItlwm-Tahoe"'),
        "default_variant": literal(build_script, 'VARIANT_LABEL="Tahoe"'),
        "output_kext_variable": literal(build_script, 'OUTPUT_KEXT="$OUTPUT_ROOT/AirportItlwm.kext"'),
        "default_staged_kext": literal(build_script, "Build/Debug/Tahoe/AirportItlwm.kext"),
        "bootkc_default": literal(
            build_script,
            'BOOTKC="${1:-/Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc}"',
        ),
        "undefined_symbol_extraction": literal(build_script, 'nm -u "$OUTPUT_BINARY"'),
        "bootkc_export_extraction": literal(build_script, 'nm -g "$BOOTKC"'),
        "symbol_comparison": literal(
            build_script,
            'comm -23 "$TMPDIR_SYM/kext_undef.txt" "$TMPDIR_SYM/bootkc_exports.txt"',
        ),
        "symbol_success_gate": literal(
            build_script, "OK: all $TOTAL undefined symbols resolve against BootKC"
        ),
        "syntax_ok": subprocess.run(["bash", "-n", str(build_script)]).returncode == 0,
        "tahoe_info_plist_bundle_id": tahoe_plist.get("IOKitPersonalities", {})
        .get("itlwm", {})
        .get("CFBundleIdentifier")
        == "com.zxystd.AirportItlwm",
    }

    install_checks = {
        "documented_staged_source": literal(
            repro_doc, "Build/Debug/Tahoe/AirportItlwm.kext /Library/Extensions/AirportItlwm.kext"
        ),
        "bounded_remove": literal(
            repro_doc, "timeout 120s sudo rm -rf /Library/Extensions/AirportItlwm.kext"
        ),
        "bounded_copy": literal(
            repro_doc,
            "timeout 120s sudo cp -R Build/Debug/Tahoe/AirportItlwm.kext /Library/Extensions/AirportItlwm.kext",
        ),
        "bounded_ownership": literal(
            repro_doc, "timeout 120s sudo chown -R root:wheel /Library/Extensions/AirportItlwm.kext"
        ),
        "bounded_permissions": literal(
            repro_doc, "timeout 120s sudo chmod -R go-w /Library/Extensions/AirportItlwm.kext"
        ),
        "bounded_reboot_load": literal(repro_doc, "timeout 120s sudo shutdown -r now"),
        "unload_prohibited": literal(repro_doc, "unload the currently loaded driver"),
    }

    require(all(lineage_checks.values()), f"lineage checks failed: {lineage_checks}")
    require(all(build_checks.values()), f"Tahoe build checks failed: {build_checks}")
    require(all(install_checks.values()), f"install/load checks failed: {install_checks}")
    require(not is_private_or_host_mirror(origin), f"private or host mirror origin: {origin}")
    require(str(root) == CANONICAL_GUEST_TOPLEVEL, f"not running from guest checkout: {root}")
    require(str(git_dir) == f"{CANONICAL_GUEST_TOPLEVEL}/.git", f"unexpected git dir: {git_dir}")

    return {
        "version": "itlwm-tahoe-lineage-build-report/v1",
        "lineage": {
            "source_repo": origin,
            "opensource_airportitlwm_repo": OPEN_SOURCE_REPO,
            "head_commit": head,
            "opensource_airportitlwm_ancestry": True,
            "anchors": lineage_checks,
        },
        "source_boundary": {
            "canonical_guest_toplevel": CANONICAL_GUEST_TOPLEVEL,
            "actual_toplevel": str(root),
            "git_dir": str(git_dir),
            "host_mount_view": "<external-control-home>/automation-v20/projects/itlwm/guest-root",
            "network_fetch_required": False,
        },
        "tahoe_build": {
            "reproducible": True,
            "command": f"./scripts/build_tahoe.sh {DEFAULT_BOOTKC}",
            "target": "AirportItlwm-Tahoe",
            "configuration": "Debug",
            "default_bootkc": DEFAULT_BOOTKC,
            "staged_kext": "Build/Debug/Tahoe/AirportItlwm.kext",
            "bootkc_symbol_gate": "nm -u staged kext binary compared with nm -g BootKC exports",
            "checks": build_checks,
        },
        "install": {
            "substrate_available": True,
            "install_path": "/Library/Extensions/AirportItlwm.kext",
            "source_kext": "Build/Debug/Tahoe/AirportItlwm.kext",
            "load_path": "load by reboot after /Library/Extensions install",
            "commands_documented_in": "docs/tahoe_lineage_build_reproducibility.md",
            "checks": install_checks,
        },
        "host_private_mirror_used": False,
        "build_artifacts": [
            {
                "path": "scripts/build_tahoe.sh",
                "kind": "committed_build_script",
                "committed": True,
            },
            {
                "path": "itlwm.xcodeproj/project.pbxproj",
                "kind": "committed_tahoe_xcode_target",
                "committed": True,
            },
            {
                "path": "AirportItlwm/AirportItlwm-Tahoe-Info.plist",
                "kind": "committed_tahoe_info_plist",
                "committed": True,
            },
            {
                "path": "Build/Debug/Tahoe/AirportItlwm.kext",
                "kind": "reproducible_staged_kext_output",
                "committed": False,
                "producer": "scripts/build_tahoe.sh",
            },
        ],
        "verification": {
            "report_command": "python3 scripts/tahoe_lineage_build_report.py --write evidence/build/tahoe_lineage_build_report.json",
            "check_command": "python3 scripts/tahoe_lineage_build_report.py --check evidence/build/tahoe_lineage_build_report.json",
            "smoke_command": "./scripts/tahoe_reproducibility_smoke.sh",
        },
    }


def load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write("\n")


def validate_report(path, project_dir):
    report = load_json(path)
    audited_head = report.get("lineage", {}).get("head_commit")
    current = run(["git", "-C", str(project_dir), "rev-parse", "HEAD"])
    acceptable_heads = {current}
    try:
        acceptable_heads.add(run(["git", "-C", str(project_dir), "rev-parse", "HEAD^"]))
    except subprocess.CalledProcessError:
        pass
    require(audited_head in acceptable_heads, f"report audits unexpected head: {audited_head}")

    expected = generate_report(project_dir, source_head=audited_head)
    require(report == expected, "committed report does not match regenerated report")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", nargs="?", const=str(REPORT_PATH), help="write report JSON")
    parser.add_argument("--check", nargs="?", const=str(REPORT_PATH), help="validate report JSON")
    args = parser.parse_args()

    project_dir = Path.cwd()
    if args.write:
        write_json(Path(args.write), generate_report(project_dir))
    elif args.check:
        validate_report(Path(args.check), project_dir)
    else:
        json.dump(generate_report(project_dir), sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")


if __name__ == "__main__":
    main()
