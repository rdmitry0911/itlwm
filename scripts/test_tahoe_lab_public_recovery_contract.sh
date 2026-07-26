#!/usr/bin/env bash
# Static safety contract for the bounded public-CoreWLAN recovery client.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash -n "$root/scripts/build_tahoe_lab_public_recovery.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
source = (root / "AirportItlwmLabPublicRecovery" /
          "airport_itlwm_lab_public_recovery.m").read_text()
build = (root / "scripts" / "build_tahoe_lab_public_recovery.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"public CoreWLAN recovery contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"unexpected {label}: {token}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


def body(text: str, marker: str, label: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {label}")


# The target is opaque at every ingress.  The only accepted wireless identity
# carriers are fixed-width SHA-256 values, and the credential has no argv or
# environment route.
for token in (
    "AIRPORT_ITLWM_LAB_TARGET_SSID_SHA256",
    "AIRPORT_ITLWM_LAB_TARGET_BSSID_SHA256",
    "CC_SHA256_DIGEST_LENGTH * 2u",
    "decode_digest_environment",
    "string_matches_digest",
    "bssid_matches_digest",
    "read_credential",
):
    require(source, token, "opaque target/credential ingress")
for forbidden in (
    "getpass(",
    "getenv(\"AIRPORT_ITLWM_LAB_PASSWORD",
    "argv[1]",
    "argv[2]",
):
    forbid(source, forbidden, "credential argv/environment route")

credential = body(source, "read_credential(uint8_t credential", "credential reader")
ordered(credential, "credential reader is a bounded pipe record",
        "isatty(STDIN_FILENO) != 0",
        "fstat(STDIN_FILENO, &input_status)",
        "S_ISFIFO(input_status.st_mode)",
        "kCredentialInputDeadlineMilliseconds",
        "read_bounded_line")
require(credential, "credential_is_valid", "credential length/ASCII gate")
reader = body(source, "read_bounded_line(uint8_t *out", "bounded line reader")
for token in ("wait_for_stdin_until(deadline)", "read(STDIN_FILENO", "byte == '\\n'",
              "secure_bzero(out, capacity)"):
    require(reader, token, "bounded stdin record")
poller = body(source, "wait_for_stdin_until(int64_t deadline)", "stdin deadline poll")
require(poller, "poll(&descriptor", "bounded stdin poll")

# The client obtains the actual AirportItlwm BSD endpoint, scans with the
# public CoreWLAN surface, and selects exactly one BSS by both opaque values.
for token in (
    'IOServiceMatching("AirportItlwm")',
    "copy_airport_itlwm_bsd_name",
    "[client interfaceWithName:endpoint]",
    "[interface scanForNetworksWithName:nil error:&scan_error]",
    "string_matches_digest([network ssid], ssid_digest)",
    "bssid_matches_digest(network_bssid, bssid_digest)",
    "return matches == 1u ? target : nil;",
):
    require(source, token, "exact public target selection")

# It must use the public association once, wait until the exact first BSS is
# active, and only then accept the host's withdrawal acknowledgement.  A
# second join would mask the driver recovery behaviour under test.
if source.count("associateToNetwork:") != 1:
    fail("public association must have exactly one call site")
require(source, "[interface associateToNetwork:target", "public association")
require(source, "password:password", "credential supplied only to public association")
initial = body(source, "int\nmain(int argc", "client main")
ordered(initial, "public association before recovery control",
        "wait_for_exact_target",
        "[interface associateToNetwork:target",
        "associated = 1;",
        "wait_for_exact_initial_identity",
        "emit_result(\"initial-ready\"",
        "withdrawal_arm_accepted = read_withdrawal_arm_control();",
        "pre_withdrawal_identity_exact = interface_has_exact_initial_identity",
        "emit_result(\"withdraw-armed\"",
        "withdrawal_control_accepted = read_withdrawal_control();",
        "wait_for_same_ssid_different_bss")
control = body(source, "read_control_token(const char *expected", "control reader")
for token in ("kControlInputDeadlineMilliseconds", "read_bounded_line(control",
              "memcmp(control, expected", "if (!require_eof)",
              "accepted = count == 0"):
    require(control, token, "bounded control token reader")
for marker, token, label in (
    ("read_withdrawal_arm_control(void)", 'read_control_token("arm-withdraw", 0)',
     "pre-withdrawal arm control"),
    ("read_withdrawal_control(void)", 'read_control_token("withdraw", 1)',
     "post-withdrawal control"),
):
    require(body(source, marker, label), token, label)

# Recovery is deliberately automatic: same opaque SSID plus a BSSID different
# from the initial target.  No raw identity reaches output.
selection = body(source, "scan_for_exact_target(CWInterface", "exact target scan")
ordered(selection, "same-ESS alternate is observed without rendering it",
        "string_matches_digest([network ssid], ssid_digest)",
        "network_bssid = [network bssid]",
        "!bssid_matches_digest(network_bssid, bssid_digest)",
        "*alternate_bss_visible = 1")
bssid = body(source, "bssid_matches_digest(NSString *value", "BSSID canonicalizer")
for token in ("data.length != sizeof(canonical)", "index % 3u == 2u",
              "byte >= 'A' && byte <= 'F'", "CC_SHA256(canonical",
              "secure_bzero(canonical", "secure_bzero(digest"):
    require(bssid, token, "canonical lower-case BSSID digest")
wait_target = body(source, "wait_for_exact_target(CWInterface", "target wait")
require(wait_target, "if (target != nil && alternate)",
        "initial association waits for an observable same-ESS alternate")
recovery = body(source, "wait_for_same_ssid_different_bss(", "recovery poll")
ordered(recovery, "same-ESS alternate-BSS proof",
        "string_matches_digest([interface ssid], ssid_digest)",
        "current_bssid != nil",
        "!bssid_matches_digest(current_bssid, initial_bssid_digest)")
emit = body(source, "emit_result(const char *result", "aggregate result")
for token in ("public_corewlan_recovery=%s", "endpoint_binding=%s",
              "alternate_bss_visible=%u",
              "initial_identity_exact=%u", "withdrawal_control_accepted=%u",
              "withdrawal_arm_accepted=%u",
              "pre_withdrawal_identity_exact=%u",
              "recovery_same_ssid=%u", "recovery_different_bss=%u"):
    require(emit, token, "categorical aggregate field")
for forbidden in ("[interface ssid]", "[interface bssid]", "%@",
                  "NSError", "localizedDescription"):
    forbid(emit, forbidden, "identity/error rendering")
if source.count("printf(") != 1:
    fail("client must have one aggregate-only output site")

# Credential bytes are scrubbed after constructing the short-lived Objective-C
# value and again on every terminal path.  The client leaves the association
# rather than silently creating a persistent configuration owner.
ordered(initial, "credential scrub precedes public association",
        "initWithBytes:credential length:credential_length",
        "secure_bzero(credential, sizeof(credential));",
        "[interface associateToNetwork:target")
ordered(initial, "associated client leaves during cleanup",
        "if (associated && interface != nil)",
        "[interface disassociate];",
        "cleanup_disassociate_attempted = 1;")
for forbidden in (
    "commitConfiguration",
    "CWMutableConfiguration",
    "CWMutableNetworkProfile",
    "CWNetworkProfile",
    "networkProfiles",
    "rememberJoinedNetworks",
    "setPairwiseMasterKey",
    "setWEPKey",
    "setWLANChannel",
    "SecItem",
    "Security.framework",
    "networksetup",
    "NSTask",
    "system(",
    "popen(",
    "fopen(",
):
    forbid(source, forbidden, "profile/keychain/shell side effect")

# Build stays a small userland CoreWLAN client; it is not a driver build or a
# staging/activation operation.
for token in (
    "xcrun --sdk macosx clang",
    "-fobjc-arc -fmodules",
    "-framework CoreWLAN -framework Foundation -framework IOKit",
    "airport_itlwm_lab_public_recovery.m",
):
    require(build, token, "bounded client build")
for forbidden in ("xcodebuild", "kmutil", "kextload", "kextutil", "sudo ",
                  "ssh ", "scp ", "networksetup"):
    forbid(build, forbidden, "driver/network deployment in client build")


class RecoveryModel:
    INIT = 0
    READY = 1
    ARMED = 2
    WITHDRAWN = 3
    RECOVERED = 4

    def __init__(self) -> None:
        self.state = self.INIT
        self.associate_calls = 0

    def associate_exact_initial(self) -> None:
        assert self.state == self.INIT
        self.associate_calls += 1
        self.state = self.READY

    def acknowledge_withdrawal(self) -> None:
        assert self.state == self.ARMED
        self.state = self.WITHDRAWN

    def arm_withdrawal(self, still_initial: bool) -> None:
        assert self.state == self.READY
        assert still_initial
        self.state = self.ARMED

    def observe(self, same_ssid: bool, different_bssid: bool) -> None:
        if self.state == self.WITHDRAWN and same_ssid and different_bssid:
            self.state = self.RECOVERED


model = RecoveryModel()
model.associate_exact_initial()
model.observe(True, True)
assert model.state == RecoveryModel.READY
model.arm_withdrawal(True)
assert model.state == RecoveryModel.ARMED
model.acknowledge_withdrawal()
model.observe(True, False)
assert model.state == RecoveryModel.WITHDRAWN
model.observe(False, True)
assert model.state == RecoveryModel.WITHDRAWN
model.observe(True, True)
assert model.state == RecoveryModel.RECOVERED
assert model.associate_calls == 1

print("PASS: bounded public-CoreWLAN same-ESS recovery harness contract")
PY
