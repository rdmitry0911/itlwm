#!/usr/bin/env bash
# Static and synthetic-runtime contract for the Linux lab credential broker.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
source="$root/AirportItlwmLabPublicRecovery/airport_itlwm_lab_credential_broker.c"
test_source="$root/tests/airport_itlwm_lab_credential_broker_test.c"
build="$root/scripts/build_lab_credential_broker.sh"

bash -n "$build"

python3 - "$source" "$test_source" "$build" <<'PY'
from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text()
test_source = Path(sys.argv[2]).read_text()
build = Path(sys.argv[3]).read_text()


def fail(message: str) -> None:
    raise SystemExit(f"credential broker contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"unexpected {label}: {token}")


def body(text: str, marker: str, label: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


# The command surface is exactly three canonical numeric descriptor arguments;
# no string carrier can become a credential or a network identity route.
for token in (
    "airport_itlwm_lab_credential_broker_main(int argc, char *argv[])",
    "argc != 4",
    "parse_decimal_fd(argv[1], &credential_fd)",
    "parse_decimal_fd(argv[2], &host_fd)",
    "parse_decimal_fd(argv[3], &control_fd)",
    "credential_fd == host_fd",
    "credential_fd == control_fd",
    "host_fd == control_fd",
    "if (value < 3u)",
):
    require(source, token, "numeric descriptor-only command surface")
for token in (
    "getenv(", "setenv(", "putenv(", "clearenv(", "extern char **environ",
    "execve(", "execl(", "execvp(", "system(", "popen(",
    "open(", "fopen(", "freopen(", "printf(", "fprintf(",
    "perror(", "puts(", "syslog(", "STDOUT_FILENO", "STDERR_FILENO",
):
    forbid(source, token, "environment/file/process/standard-stream route")

# Credential ingress is one FIFO line, followed by EOF, with a fixed deadline.
for token in (
    "isatty(fd) != 0", "S_ISFIFO(status.st_mode)", "O_RDONLY",
    "kCredentialMinimumLength = 8u", "kCredentialMaximumLength = 63u",
    "kCredentialDeadlineMilliseconds = 15000u",
    "read_exactly_one_credential", "byte < 0x20u || byte > 0x7eu",
    "length >= kCredentialMaximumLength", "if (count != 0)",
    "feed_host_pipe_once", "close_once(host_fd) && sent",
):
    require(source, token, "bounded FIFO credential handling")
reader = body(source, "read_exactly_one_credential(int fd", "credential reader")
for token in ("wait_for_fd_until(fd, POLLIN, deadline)", "byte == (uint8_t)'\\n'",
              "secret->length = length", "count != 0"):
    require(reader, token, "exactly-one-record credential reader")
if source.count("feed_host_pipe_once(&host_fd, &secret)") != 1:
    fail("host credential pipe must be fed at one call site")

# The private controller is a same-user, connected AF_UNIX SOCK_SEQPACKET
# peer.  START is fixed-width lower-case opaque hashes plus exactly one guest
# pipe descriptor; ARM and RELEASE carry no descriptor.
for token in (
    "SOCK_SEQPACKET", "SO_DOMAIN", "AF_UNIX", "getpeername(",
    "SO_PEERCRED", "peer_credentials.uid != geteuid()", "SCM_RIGHTS",
    "MSG_CMSG_CLOEXEC", "CMSG_LEN(sizeof(int))",
    "is_start_packet", "is_lower_hex", "kDigestTextLength = 64u",
    'memcmp(packet->bytes, "START "', "is_guest_write_pipe",
    "O_WRONLY", "write_guest_credential", "write_guest_arm",
    "arm-withdraw\\n", "withdraw\\n", '"HOST_FED"', '"STARTED"',
    '"ARMED"', '"RELEASED"', '"ABORTED"',
    "kSwitcherCredentialReadBoundMilliseconds = 45000u",
    "kSwitcherSetupBoundMilliseconds = 180000u",
    "kPostCredentialSetupMarginMilliseconds = 15000u",
    "kHostFedToSetupStartedBoundMilliseconds =",
    "kSetupStartedToStartDeadlineMilliseconds =",
    "kHostFedControllerHandoffMarginMilliseconds = 10000u",
    "kHostFedToStartDeadlineMilliseconds =",
    "kVerifiedInitialLeaseFloorMilliseconds = 285000u",
    "kPostStartSessionDeadlineMilliseconds = 270000u",
    "kStartedToArmDeadlineMilliseconds = 145000u",
    "kArmedToReleaseDeadlineMilliseconds = 110000u",
    "_Static_assert(kPostStartSessionDeadlineMilliseconds <",
    "_Static_assert(kStartedToArmDeadlineMilliseconds +",
    "deadline_is_live",
    "deadline_after_capped",
):
    require(source, token, "private opaque control protocol")
for token in ("localizedDescription", "ssid", "bssid", "SSID=", "BSSID="):
    forbid(source, token, "raw wireless identity rendering")
protocol = body(source, "airport_itlwm_lab_credential_broker_main", "broker main")
for token in (
    "kBrokerPhaseHostFed", "kBrokerPhaseStarted", "kBrokerPhaseArmed",
    "send_status(control_fd, host_fed_status", "is_fixed_packet(&packet, \"ABORT\"",
    "is_start_packet(&packet)", "write_guest_credential(guest_fd, &secret,",
    "secret_buffer_destroy(&secret)", "is_fixed_packet(&packet, \"ARM\"",
    "write_guest_arm(guest_fd, operation_deadline)",
    "is_fixed_packet(&packet, \"RELEASE\"",
    "write_guest_release(guest_fd, operation_deadline)",
    "close_once(&guest_fd)",
):
    require(protocol, token, "HOST_FED/STARTED/ARMED state transition")
abort_gate = protocol.find('is_fixed_packet(&packet, "ABORT"')
phase_gate = protocol.find("if (phase == kBrokerPhaseHostFed)")
if abort_gate < 0 or phase_gate < 0 or abort_gate > phase_gate:
    fail("ABORT is not accepted before every control-phase gate")

# HOST_FED retains the secret for the full permitted host-activation path.
# A *valid* START begins a separate, shorter session after the controller has
# attested enough watchdog remainder.  This avoids charging a legitimate LAR
# wait against initial readiness, while every later phase still shares one
# non-extendable absolute ceiling.
for token in (
    "deadline_after(kHostFedToStartDeadlineMilliseconds,",
    "host_fed_deadline",
    "deadline_after(kPostStartSessionDeadlineMilliseconds,",
    "post_start_session_deadline",
    "deadline_after_capped(phase_deadline,",
    "deadline_after_capped(kPipeWriteDeadlineMilliseconds,",
):
    require(protocol, token, "bounded HOST_FED and post-START windows")
forbid(source, "kBrokerSessionLeaseDeadlineMilliseconds",
       "obsolete post-HOST_FED global lease cap")
if "deadline_after(phase_deadline, &deadline)" in protocol:
    fail("phase-local deadline can extend the post-START session")
if protocol.count("deadline_after_capped(phase_deadline,") != 2:
    fail("STARTED and ARMED must each be capped by one post-START ceiling")
# HOST_FED is the one pre-retention acknowledgement: it receives its own
# standalone 15-second send bound before the HOST_FED deadline starts.  Every
# later packet write/status must instead be capped by a live phase/session
# deadline, so keep these two classes distinct.
if protocol.count("deadline_after(kPipeWriteDeadlineMilliseconds,") != 1:
    fail("HOST_FED acknowledgement is not independently bounded exactly once")
if protocol.count("deadline_after_capped(kPipeWriteDeadlineMilliseconds,") < 4:
    fail("post-HOST_FED writes/statuses are not all capped by their live window")
host_fed_phase = protocol.find("case kBrokerPhaseHostFed:")
start_gate = protocol.find("if (phase == kBrokerPhaseHostFed)")
post_start = protocol.find("deadline_after(kPostStartSessionDeadlineMilliseconds,")
host_fed_live = protocol.find("deadline_is_live(host_fed_deadline)")
if min(host_fed_phase, start_gate, post_start, host_fed_live) < 0:
    fail("missing HOST_FED-to-START timing boundary")
if not (host_fed_phase < start_gate < post_start):
    fail("post-START session does not begin only after the valid START gate")
host_fed_status = protocol.find("send_status(control_fd, host_fed_status")
host_fed_deadline = protocol.find("deadline_after(kHostFedToStartDeadlineMilliseconds,")
if host_fed_status < 0 or host_fed_deadline < 0 or host_fed_status > host_fed_deadline:
    fail("HOST_FED deadline begins before its acknowledgement is sent")
start_live = protocol.find("deadline_is_live(host_fed_deadline)", start_gate)
if start_live < 0 or start_live > post_start:
    fail("valid START is not rejected after the HOST_FED retention deadline")
capped = body(source, "deadline_after_capped(uint32_t milliseconds", "deadline cap helper")
for token in (
    "cap_deadline <= started",
    "requested < cap_deadline ? requested : cap_deadline",
):
    require(capped, token, "non-extendable absolute deadline helper")
if "kStartedToArmDeadlineMilliseconds = 145000u" not in source or \
        "kArmedToReleaseDeadlineMilliseconds = 110000u" not in source:
    fail("cross-layer phase ceilings changed")
started_phase = protocol.find("case kBrokerPhaseStarted:")
armed_phase = protocol.find("case kBrokerPhaseArmed:")
started_cap = protocol.find("phase_deadline = kStartedToArmDeadlineMilliseconds", started_phase)
armed_cap = protocol.find("phase_deadline = kArmedToReleaseDeadlineMilliseconds", armed_phase)
if min(started_phase, armed_phase, started_cap, armed_cap) < 0 or \
        not (started_phase < started_cap < armed_phase < armed_cap):
    fail("145/110 phase ceilings are not used by the live broker state machine")

# The secret resides in a private, locked non-dumpable mapping and every
# terminal route scrubs and releases it.  SIGPIPE is handled as an error path,
# not an abrupt bypass around cleanup.
for token in (
    "mmap(NULL", "mlock(mapping", "madvise(mapping", "MADV_DONTDUMP",
    "PR_SET_DUMPABLE", "setrlimit(RLIMIT_CORE", "SIGPIPE", "secure_zero",
    "munlock", "munmap",
    "secret_buffer_destroy(&secret)",
):
    require(source, token, "secret-memory protection and cleanup")

# The build remains a local Linux userland build, and the synthetic test
# exercises a descriptor-only, SCM_RIGHTS-mediated happy path plus rejects.
for token in (
    "airport_itlwm_lab_credential_broker.c", "cc -std=c11",
    "-D_FORTIFY_SOURCE=2", "-fstack-protector-strong", "-Werror",
):
    require(build, token, "local hardened build")
for token in ("ssh ", "scp ", "sudo ", "networksetup", "kmutil", "kextload"):
    forbid(build, token, "deployment action in broker build")
for token in (
    "SOCK_SEQPACKET", "SCM_RIGHTS", "test_successful_start_and_release",
    "test_rejects_short_credential", "test_rejects_additional_credential_record",
    "test_rejects_noncanonical_start",
    "test_abort_is_explicit_and_secret_free", "\"HOST_FED\"", "\"STARTED\"",
    "\"ARMED\"", "\"ARM\"", "\"RELEASE\"", "\"ABORT\"",
):
    require(test_source, token, "synthetic behavioral coverage")

print("PASS: credential broker static contract")
PY

temporary_directory="$(mktemp -d)"
temporary_binary="$temporary_directory/airport_itlwm_lab_credential_broker_test"
cleanup() {
    if [ -e "$temporary_binary" ]; then
        unlink -- "$temporary_binary"
    fi
    rmdir -- "$temporary_directory"
}
trap cleanup EXIT
cc -std=c11 -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong \
    -Wall -Wextra -Werror -Wpedantic \
    -DAIRPORT_ITLWM_LAB_CREDENTIAL_BROKER_NO_MAIN \
    "$source" "$test_source" \
    -o "$temporary_binary"
"$temporary_binary"
