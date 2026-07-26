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
    "kHostFedToStartDeadlineMilliseconds = 60000u",
    "kStartedToArmDeadlineMilliseconds = 120000u",
    "kArmedToReleaseDeadlineMilliseconds = 90000u",
):
    require(source, token, "private opaque control protocol")
for token in ("localizedDescription", "ssid", "bssid", "SSID=", "BSSID="):
    forbid(source, token, "raw wireless identity rendering")
protocol = body(source, "airport_itlwm_lab_credential_broker_main", "broker main")
for token in (
    "kBrokerPhaseHostFed", "kBrokerPhaseStarted", "kBrokerPhaseArmed",
    "send_status(control_fd, host_fed_status", "is_fixed_packet(&packet, \"ABORT\"",
    "is_start_packet(&packet)", "write_guest_credential(guest_fd, &secret)",
    "secret_buffer_destroy(&secret)", "is_fixed_packet(&packet, \"ARM\"",
    "write_guest_arm(guest_fd)", "is_fixed_packet(&packet, \"RELEASE\"",
    "write_guest_release(guest_fd)",
    "close_once(&guest_fd)",
):
    require(protocol, token, "HOST_FED/STARTED/ARMED state transition")
abort_gate = protocol.find('is_fixed_packet(&packet, "ABORT"')
phase_gate = protocol.find("if (phase == kBrokerPhaseHostFed)")
if abort_gate < 0 or phase_gate < 0 or abort_gate > phase_gate:
    fail("ABORT is not accepted before every control-phase gate")

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
