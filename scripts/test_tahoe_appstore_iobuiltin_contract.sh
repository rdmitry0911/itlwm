#!/usr/bin/env bash
# Source-only regression gate for Tahoe App Store's en0 controller walk.
#
# This is not a runtime App Store test. It proves that the Tahoe-only kext
# personality marks the AirportItlwm controller non-built-in while preserving
# the real interface's controller-side BSD Name publication path.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PLIST="$ROOT/AirportItlwm/AirportItlwm-Tahoe-Info.plist"
PROJECT="$ROOT/itlwm.xcodeproj/project.pbxproj"
SKYWALK="$ROOT/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
TAHOE_V2="$ROOT/AirportItlwm/AirportItlwmV2.cpp"
WORKFLOW="$ROOT/.github/workflows/main.yml"
BUILD_SCRIPT="$ROOT/scripts/build_tahoe.sh"

python3 - "$PLIST" "$PROJECT" "$SKYWALK" "$TAHOE_V2" "$WORKFLOW" "$BUILD_SCRIPT" <<'PY'
import pathlib
import plistlib
import re
import sys

plist_path = pathlib.Path(sys.argv[1])
project = pathlib.Path(sys.argv[2]).read_text()
skywalk = pathlib.Path(sys.argv[3]).read_text()
tahoe_v2 = pathlib.Path(sys.argv[4]).read_text()
workflow = pathlib.Path(sys.argv[5]).read_text()
build_script = pathlib.Path(sys.argv[6]).read_text()

with plist_path.open("rb") as handle:
    info = plistlib.load(handle)

personalities = info["IOKitPersonalities"]
controller = personalities["NetworkController"]
wrapper = personalities["itlwm"]
boot_nub = personalities["AirportItlwmBootNub"]

assert controller["CFBundleIdentifier"] == "com.zxystd.AirportItlwm"
assert controller["IOClass"] == "AirportItlwm"
assert controller["IOProviderClass"] == "IOPCIEDeviceWrapper"
assert controller["IOBuiltin"] is False

# The workaround is deliberately controller-only: neither the PCI wrapper
# nor its boot nub should gain a semantic built-in classification.
assert "IOBuiltin" not in wrapper
assert "IOBuiltin" not in boot_nub

# Tahoe Debug and Release both select this plist; Sonoma and legacy targets
# retain their own personalities and are outside this narrow workaround.
target = re.search(
    r'F8E94C802B9ABFE20081A3C4 /\* AirportItlwm-Tahoe \*/ = \{'
    r'.*?buildConfigurationList = F8E94CF22B9ABFE20081A3C4',
    project, re.DOTALL)
assert target
config_list = re.search(
    r'F8E94CF22B9ABFE20081A3C4 /\* Build configuration list for '
    r'PBXNativeTarget "AirportItlwm-Tahoe" \*/ = \{(.*?)\n\t\};',
    project, re.DOTALL)
assert config_list
for config_id in ("F8E94CF32B9ABFE20081A3C4", "F8E94CF42B9ABFE20081A3C4"):
    assert config_id in config_list.group(1)
    config = re.search(
        rf'^\t\t{config_id} /\* .*? \*/ = \{{\n'
        rf'\t\t\tisa = XCBuildConfiguration;(.*?)^\t\t\}};',
        project, re.DOTALL | re.MULTILINE)
    assert config
    assert 'INFOPLIST_FILE = "AirportItlwm/AirportItlwm-Tahoe-Info.plist";' in config.group(1)

# The Tahoe target compiles the V2 controller implementation. Its init passes
# the matched personality through to IORegistry, which is the source-level
# route by which the controller receives this typed property.
tahoe_sources = re.search(
    r'F8E94CA52B9ABFE20081A3C4 /\* Sources \*/ = \{(.*?)^\t\t\};',
    project, re.DOTALL | re.MULTILINE)
assert tahoe_sources
assert "AirportItlwmV2.cpp in Sources" in tahoe_sources.group(1)
assert re.search(
    r"bool AirportItlwm::init\(OSDictionary \*properties\)\s*\{\s*"
    r"bool ret = super::init\(properties\);", tahoe_v2)

# The actual Skywalk en0 path receives a six-byte MAC before it is attached
# to the controller. Do not confuse the controller's mirrored BSD Name with
# this interface-side MAC property.
assert "static bool\nseedTahoeInitialMacAddress" in tahoe_v2
assert "netIf->setProperty(kIOMACAddress, initMac.octet, kIOEthernetAddressSize);" in tahoe_v2
seed_call = tahoe_v2.index("(void)seedTahoeInitialMacAddress(")
attach_call = tahoe_v2.index("if (!fNetIf->attach(this))", seed_call)
assert seed_call < attach_call

# Do not "fix" this by removing the separately evidenced controller-side BSD
# Name publication required by CoreWiFi. The personality change is the only
# intended behavior change in this packet.
assert "void AirportItlwmSkywalkInterface::\nsetBSDName" in skywalk
assert "IO80211InfraProtocol::setBSDName(bsdName);" in skywalk
assert 'instance->setProperty("BSD Name", value);' in skywalk
assert "setProperty(kIOMACAddress" in skywalk

# The legacy all-target scheme ends at Sonoma. A Tahoe candidate must be
# built and packaged explicitly, otherwise this plist fix cannot reach a
# GitHub release asset.
assert 'name: Build AirportItlwm Tahoe' in workflow
assert 'name: Check Tahoe App Store controller contract' in workflow
assert 'runs-on: macos-26-intel' in workflow
assert 'name: Check x86_64 release runner' in workflow
assert 'test "$(uname -m)" = x86_64' in workflow
assert 'ITLWM_SOURCE_ID_OVERRIDE="${SHORT_SHA}" ./scripts/build_tahoe.sh' in workflow
assert "TAHOE_KEXT='Build/Debug/Tahoe/AirportItlwm.kext'" in workflow
assert 'AirportItlwm-Tahoe-v${ITLWM_VER}-DEBUG-alpha-${SHORT_SHA}.zip' in workflow
assert "check_tahoe_plist()" in workflow
assert 'check_tahoe_plist "$TAHOE_KEXT/Contents/Info.plist"' in workflow
assert 'ditto -c -k --sequesterRsrc --keepParent "$TAHOE_KEXT"' in workflow
assert "unzip -Z1 \"$TAHOE_ZIP\" | grep -qx 'AirportItlwm.kext/Contents/Info.plist'" in workflow
assert 'ditto -x -k "$TAHOE_ZIP" "$VERIFY_DIR"' in workflow
assert 'check_tahoe_plist "$VERIFY_DIR/AirportItlwm.kext/Contents/Info.plist"' in workflow
assert 'test -f "$VERIFY_DIR/AirportItlwm.kext/Contents/MacOS/AirportItlwm"' in workflow
assert "lipo -archs \"$TAHOE_KEXT/Contents/MacOS/AirportItlwm\" | grep -qx 'x86_64'" in workflow
assert 'permissions:\n  contents: write' in workflow
assert 'commit: ${{ github.sha }}' in workflow
assert workflow.count("if: contains(github.event.head_commit.message, 'Bump version') == false") == 2
assert 'TARGET="AirportItlwm-Tahoe"' in build_script
assert 'OUTPUT_KEXT="$OUTPUT_ROOT/AirportItlwm.kext"' in build_script
for forbidden in ("sudo ", "kmutil ", "kextload ", "reboot"):
    assert forbidden not in build_script, forbidden

print("PASS: Tahoe controller mitigation is scoped and its kext is packaged for releases")
PY
