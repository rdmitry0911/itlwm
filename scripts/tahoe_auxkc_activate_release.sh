#!/usr/bin/env bash
# Stage a release AirportItlwm bundle into Tahoe's canonical AuxKC without
# direct-loading or unloading a kext.  This is intentionally separate from
# tahoe_auxkc_admission_preflight.sh: callers must run that preflight first.
#
# The script does not reboot.  A caller must capture its READY result and then
# explicitly reboot the *guest* before assessing the loaded identity.
set -euo pipefail

readonly AIRPORT_ID="com.zxystd.AirportItlwm"
readonly CANONICAL_BUNDLE="/Library/Extensions/AirportItlwm.kext"
readonly CANONICAL_AUXKC="/Library/KernelCollections/AuxiliaryKernelExtensions.kc"
readonly BOOTKC="/System/Library/KernelCollections/BootKernelExtensions.kc"
readonly SYSTEMKC="/System/Library/KernelCollections/SystemKernelExtensions.kc"
readonly -a REQUIRED_IDS=(
    "com.zxystd.AirportItlwm"
    "com.apple.nke.rvi"
    "com.apple.driver.AppleMobileDevice"
    "com.highpoint-tech.kext.HighPointIOP"
    "com.highpoint-tech.kext.HighPointRR"
)

usage() {
    cat >&2 <<'EOF'
usage: tahoe_auxkc_activate_release.sh \
  --candidate /private/path/AirportItlwm.kext \
  --work-root /private/path \
  --baseline /private/path/canonical-baseline.json \
  --expected-sha256 SHA256 \
  --expected-uuid UUID

Performs a transactional replacement of the canonical AirportItlwm bundle and
AuxKC only after validating the exact five-member collection.  It creates
immutable, timestamped rollback copies, never direct-loads/unloads a kext, and
does not reboot.  After its first canonical swap, an EXIT/HUP/INT/TERM path
attempts rollback before reporting failure. This root-only helper accepts a
root-owned sealed candidate and work root below /private.
EOF
}

fail() {
    printf 'ACTIVATION_FAIL:%s\n' "$*" >&2
    exit 1
}

require_private_path() {
    local path="$1"
    local label="$2"
    case "$path" in
        /private|/private/*) ;;
        *) fail "$label must resolve below /private" ;;
    esac
}

# The activation helper is intentionally narrower than a general-purpose
# installer.  It only accepts the bridge's root-owned snapshot, and checks it
# physically before any recursive metadata cleanup can touch it.
validate_root_owned_tree() {
    local path="$1"

    /usr/bin/python3 -I - "$path" <<'PY'
import os
import stat
import sys

root = sys.argv[1]

def fail():
    raise SystemExit(1)

def check(path, want_directory):
    try:
        value = os.lstat(path)
    except OSError:
        fail()
    if stat.S_ISLNK(value.st_mode) or value.st_uid != 0:
        fail()
    if stat.S_IMODE(value.st_mode) & 0o7022:
        fail()
    if want_directory:
        if not stat.S_ISDIR(value.st_mode):
            fail()
    elif not stat.S_ISREG(value.st_mode) or value.st_nlink != 1:
        fail()
    return value

pending = [root]
while pending:
    current = pending.pop()
    check(current, True)
    try:
        entries = list(os.scandir(current))
    except OSError:
        fail()
    for entry in entries:
        child = os.path.join(current, entry.name)
        value = os.lstat(child)
        if stat.S_ISDIR(value.st_mode):
            check(child, True)
            pending.append(child)
        else:
            check(child, False)
PY
}

validate_bridge_root() {
    local root="$1"

    /usr/bin/python3 -I - "$root" <<'PY'
import os
import re
import stat
import sys

root = sys.argv[1]
if re.fullmatch(r"/private/var/tmp/aiam-iwn-activation-[A-Za-z0-9][A-Za-z0-9._-]{0,63}", root) is None:
    raise SystemExit(1)
for path, want_sticky in (("/private", False), ("/private/var", False),
                          ("/private/var/tmp", True), (root, False)):
    try:
        value = os.lstat(path)
    except OSError:
        raise SystemExit(1)
    if (stat.S_ISLNK(value.st_mode) or not stat.S_ISDIR(value.st_mode) or
            value.st_uid != 0):
        raise SystemExit(1)
    if want_sticky and not value.st_mode & stat.S_ISVTX:
        raise SystemExit(1)
    if path == root and stat.S_IMODE(value.st_mode) != 0o700:
        raise SystemExit(1)
PY
    /bin/chmod -N "$root"
    /bin/chmod 700 "$root"
    /usr/bin/python3 -I - "$root" <<'PY'
import os
import stat
import sys

value = os.lstat(sys.argv[1])
if (stat.S_ISLNK(value.st_mode) or not stat.S_ISDIR(value.st_mode) or
        value.st_uid != 0 or stat.S_IMODE(value.st_mode) != 0o700):
    raise SystemExit(1)
PY
}

candidate=""
work_root=""
baseline=""
expected_sha=""
expected_uuid=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --candidate)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            candidate="$2"
            shift 2
            ;;
        --work-root)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            work_root="$2"
            shift 2
            ;;
        --baseline)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            baseline="$2"
            shift 2
            ;;
        --expected-sha256)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            expected_sha="$2"
            shift 2
            ;;
        --expected-uuid)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            expected_uuid="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

[ -n "$candidate" ] && [ -n "$work_root" ] && [ -n "$baseline" ] && [ -n "$expected_sha" ] && [ -n "$expected_uuid" ] || {
    usage
    exit 2
}
[ "$(/usr/bin/id -u)" -eq 0 ] || fail "must run as root"

[ "${#expected_sha}" -eq 64 ] || fail "expected SHA-256 must be 64 hexadecimal characters"
case "$expected_sha" in
    *[!0-9a-fA-F]*) fail "expected SHA-256 must be hexadecimal" ;;
esac
case "$expected_uuid" in
    ????????-????-????-????-????????????) ;;
    *) fail "expected UUID has an invalid shape" ;;
esac

[ -d "$candidate" ] || fail "candidate bundle is missing"
candidate="$(cd -P -- "$candidate" && pwd)"
require_private_path "$candidate" "candidate"

[ -d "$work_root" ] || fail "work root is missing"
work_root="$(cd -P -- "$work_root" && pwd)"
require_private_path "$work_root" "work root"
validate_bridge_root "$work_root" ||
    fail "work root must be the root-owned sealed bridge activation root"
[ "$candidate" = "$work_root/preflight/AirportItlwm.kext" ] ||
    fail "candidate must be the bridge preflight snapshot for this work root"
validate_root_owned_tree "$candidate" || fail "candidate tree is unsafe"
[ -f "$candidate/Contents/MacOS/AirportItlwm" ] || fail "candidate Mach-O is missing"

[ -f "$baseline" ] && [ ! -L "$baseline" ] || fail "canonical baseline is missing or unsafe"
baseline_parent="$(dirname "$baseline")"
baseline_leaf="$(basename "$baseline")"
[ -d "$baseline_parent" ] || fail "canonical baseline parent is missing"
baseline="$(cd -P -- "$baseline_parent" && pwd)/$baseline_leaf"
require_private_path "$baseline" "canonical baseline"
[ "$baseline" = "$work_root/canonical-baseline.json" ] ||
    fail "canonical baseline must be the root-owned activation baseline"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
work="$work_root/activation-${stamp}"
new_bundle="/Library/Extensions/.AirportItlwm.kext.pending-${stamp}"
bundle_backup="/Library/Extensions/AirportItlwm.kext.preinstall-bak.${stamp}"
bundle_displaced="/Library/Extensions/AirportItlwm.kext.preinstall-displaced.${stamp}"
bundle_failed="/Library/Extensions/AirportItlwm.kext.failed-${stamp}"
new_auxkc="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.new-${stamp}"
auxkc_backup="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.preinstall-bak.${stamp}"
auxkc_displaced="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.preinstall-displaced.${stamp}"
auxkc_failed="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.failed-${stamp}"
transaction_armed=0
rollback_in_progress=0
baseline_companion_expected=""

validate_auxkc() {
    local kc="$1"
    local label="$2"
    local airport_uuid="${3:-}"
    local inspect="$work/${label}.inspect"
    local members="$work/${label}.members"
    local count id rows

    /usr/bin/kmutil inspect --show-kext-uuids -A "$kc" >"$inspect"
    /usr/bin/awk '
        $0 == "Extension Information:" { in_members = 1; next }
        in_members && $0 == "" { exit }
        in_members { print }
    ' "$inspect" >"$members"
    count="$(/usr/bin/awk 'NF { count++ } END { print count + 0 }' "$members")"
    [ "$count" = 5 ] || return 1
    for id in "${REQUIRED_IDS[@]}"; do
        rows="$(/usr/bin/awk -F '\t' -v id="$id" '$1 == id { count++ } END { print count + 0 }' "$members")"
        [ "$rows" = 1 ] || return 1
    done
    if [ -n "$airport_uuid" ]; then
        /usr/bin/awk -F '\t' -v expected="$airport_uuid" '
            $1 == "com.zxystd.AirportItlwm" {
                seen++
                uuid=$4
                gsub(/[()]/, "", uuid)
                if (uuid != expected) bad=1
            }
            END { exit (seen == 1 && !bad) ? 0 : 1 }
        ' "$members"
    fi
}

clear_untrusted_metadata() {
    local path="$1"

    /bin/chmod -RN "$path" || return 1
    /usr/bin/xattr -rc "$path" || return 1
    /usr/bin/chflags -R nouchg "$path" || return 1
    /usr/bin/find "$path" -exec /bin/chmod u-s,g-s,-t {} + || return 1
}

validate_baseline_companion_members() {
    local members="$1"
    local companion_rows observed_companions

    [ -n "$baseline_companion_expected" ] || return 1
    [ -f "$members" ] || return 1
    companion_rows="$(/usr/bin/awk -F '\t' '$1 != "com.zxystd.AirportItlwm" { print }' \
        "$members" | LC_ALL=C /usr/bin/sort)"
    observed_companions="$(printf '%s' "$companion_rows" | /usr/bin/shasum -a 256 | /usr/bin/awk '{print $1}')"
    [ "$observed_companions" = "$baseline_companion_expected" ]
}

load_canonical_baseline() {
    /usr/bin/python3 -I - "$baseline" <<'PY'
import hashlib
import json
import os
import re
import stat
import sys

path = sys.argv[1]
value = os.lstat(path)
if (stat.S_ISLNK(value.st_mode) or not stat.S_ISREG(value.st_mode) or
        value.st_nlink != 1 or value.st_uid != 0 or
        stat.S_IMODE(value.st_mode) != 0o600):
    raise SystemExit(1)

def duplicate_reject(pairs):
    result = {}
    for key, item in pairs:
        if key in result:
            raise SystemExit(1)
        result[key] = item
    return result

def reject_nonfinite(_value):
    raise SystemExit(1)

try:
    document = json.loads(open(path, encoding="utf-8").read(),
                          object_pairs_hook=duplicate_reject,
                          parse_constant=reject_nonfinite)
except Exception:
    raise SystemExit(1)
if (not isinstance(document, dict) or
        set(document) != {"airport_sha256", "auxkc_sha256", "companion_members_sha256"}):
    raise SystemExit(1)
values = []
for key in ("airport_sha256", "auxkc_sha256", "companion_members_sha256"):
    item = document.get(key)
    if not isinstance(item, str) or re.fullmatch(r"[0-9a-f]{64}", item) is None:
        raise SystemExit(1)
    values.append(item)
print("|".join(values))
PY
}

validate_canonical_baseline() {
    local baseline_values expected_airport expected_auxkc expected_companions
    local observed_airport observed_auxkc observed_companions companion_rows

    baseline_values="$(load_canonical_baseline)" || return 1
    IFS='|' read -r expected_airport expected_auxkc expected_companions <<EOF
$baseline_values
EOF
    case "$expected_airport:$expected_auxkc:$expected_companions" in
        ????????*) ;;
        *) return 1 ;;
    esac
    baseline_companion_expected="$expected_companions"
    observed_airport="$(/usr/bin/shasum -a 256 "$CANONICAL_BUNDLE/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
    observed_auxkc="$(/usr/bin/shasum -a 256 "$CANONICAL_AUXKC" | /usr/bin/awk '{print $1}')"
    [ "$observed_airport" = "$expected_airport" ] || return 1
    [ "$observed_auxkc" = "$expected_auxkc" ] || return 1
    if ! validate_auxkc "$CANONICAL_AUXKC" "canonical_baseline_fence"; then
        return 1
    fi
    validate_baseline_companion_members "$work/canonical_baseline_fence.members"
}

rollback_bundle() {
    if [ -e "$CANONICAL_BUNDLE" ] && [ -e "$bundle_displaced" ] && [ ! -e "$bundle_failed" ]; then
        /bin/mv "$CANONICAL_BUNDLE" "$bundle_failed" || return 1
    fi
    if [ -e "$bundle_displaced" ] && [ ! -e "$CANONICAL_BUNDLE" ]; then
        /bin/mv "$bundle_displaced" "$CANONICAL_BUNDLE" || return 1
    fi
}

rollback_auxkc() {
    if [ -e "$CANONICAL_AUXKC" ] && [ -e "$auxkc_displaced" ] && [ ! -e "$auxkc_failed" ]; then
        /bin/mv "$CANONICAL_AUXKC" "$auxkc_failed" || return 1
    fi
    if [ -e "$auxkc_displaced" ] && [ ! -e "$CANONICAL_AUXKC" ]; then
        /bin/mv "$auxkc_displaced" "$CANONICAL_AUXKC" || return 1
    fi
}

emergency_rollback() {
    local status="$1"
    local rollback_failed=0

    # Do not recurse through EXIT while recovering a partially moved canonical
    # bundle/collection. SIGKILL cannot be trapped; this covers ordinary shell
    # failure and the interrupt signals a bounded caller can deliver.
    trap - EXIT HUP INT TERM
    [ "$transaction_armed" = 1 ] || exit "$status"
    [ "$rollback_in_progress" = 0 ] || exit "$status"
    rollback_in_progress=1

    # Restore the collection before the bundle so every recoverable
    # interruption returns the original matching canonical pair.
    if ! rollback_auxkc; then
        rollback_failed=1
    fi
    if ! rollback_bundle; then
        rollback_failed=1
    fi
    if [ "$rollback_failed" = 0 ]; then
        printf 'ACTIVATION_ABORT_ROLLED_BACK\n' >&2
    else
        printf 'ACTIVATION_ABORT_ROLLBACK_FAILED\n' >&2
    fi
    exit "$status"
}

trap 'emergency_rollback $?' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

for path in "$CANONICAL_BUNDLE" "$CANONICAL_AUXKC" "$BOOTKC" "$SYSTEMKC"; do
    [ -e "$path" ] || fail "required current system path is missing: $path"
done
for path in "$work" "$new_bundle" "$bundle_backup" "$bundle_displaced" \
            "$bundle_failed" "$new_auxkc" "$auxkc_backup" "$auxkc_displaced" "$auxkc_failed"; do
    [ ! -e "$path" ] && [ ! -L "$path" ] || fail "refusing to overwrite existing path: $path"
done

candidate_sha="$(/usr/bin/shasum -a 256 "$candidate/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
[ "$candidate_sha" = "$expected_sha" ] || fail "candidate SHA-256 mismatch"
candidate_uuid="$(/usr/bin/dwarfdump --uuid "$candidate/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
[ "$candidate_uuid" = "$expected_uuid" ] || fail "candidate UUID mismatch"

/bin/mkdir "$work"
/bin/chmod 700 "$work"
validate_root_owned_tree "$work" || fail "activation work tree is unsafe"
clear_untrusted_metadata "$work" || fail "activation work metadata cleanup failed"
/bin/chmod 700 "$work"
if ! validate_auxkc "$CANONICAL_AUXKC" "canonical_before"; then
    fail "canonical collection is not the approved five-member set"
fi

if ! /usr/bin/ditto "$CANONICAL_BUNDLE" "$bundle_backup"; then
    fail "bundle backup failed"
fi
if ! /usr/bin/ditto --norsrc --noacl --noextattr --noqtn "$candidate" "$new_bundle"; then
    fail "candidate staging failed"
fi
if ! validate_root_owned_tree "$new_bundle" ||
        ! clear_untrusted_metadata "$new_bundle" ||
        ! /usr/sbin/chown -R root:wheel "$new_bundle" || ! /bin/chmod -R go-w "$new_bundle"; then
    fail "candidate staging permissions failed"
fi
staged_sha="$(/usr/bin/shasum -a 256 "$new_bundle/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
staged_uuid="$(/usr/bin/dwarfdump --uuid "$new_bundle/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
if [ "$staged_sha" != "$expected_sha" ] || [ "$staged_uuid" != "$expected_uuid" ]; then
    fail "candidate staging identity mismatch"
fi

# This fence is intentionally the last operation before arming the
# transaction.  It binds the replacement to the root-owned baseline captured
# by the bridge, so a changed canonical pair cannot be silently activated.
if ! validate_canonical_baseline; then
    fail "canonical baseline changed before activation"
fi

# From this point a normal failure or an interrupt must restore both canonical
# artifacts before the helper returns. Arm before the first move, not after it.
transaction_armed=1
if ! /bin/mv "$CANONICAL_BUNDLE" "$bundle_displaced"; then
    fail "bundle displacement failed"
fi
if ! /bin/mv "$new_bundle" "$CANONICAL_BUNDLE"; then
    /bin/mv "$bundle_displaced" "$CANONICAL_BUNDLE" || true
    fail "bundle swap failed"
fi
if ! /usr/sbin/chown -R root:wheel "$CANONICAL_BUNDLE" || ! /bin/chmod -R go-w "$CANONICAL_BUNDLE"; then
    rollback_bundle || true
    fail "canonical bundle permissions failed; rollback attempted"
fi
installed_sha="$(/usr/bin/shasum -a 256 "$CANONICAL_BUNDLE/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
installed_uuid="$(/usr/bin/dwarfdump --uuid "$CANONICAL_BUNDLE/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
if [ "$installed_sha" != "$expected_sha" ] || [ "$installed_uuid" != "$expected_uuid" ]; then
    rollback_bundle || true
    fail "canonical bundle identity mismatch; rollback attempted"
fi

if ! /usr/bin/kmutil create -n aux --arch x86_64 \
    --elide-identifier com.apple.driver.AppleSunrise \
    -k /System/Library/Kernels/kernel \
    -B "$BOOTKC" \
    -S "$SYSTEMKC" \
    -A "$new_auxkc" \
    -r /Library/Extensions \
    -r /Library/Apple/System/Library/Extensions \
    --force; then
    rollback_bundle || true
    fail "AuxKC build failed; bundle rollback attempted"
fi
if ! validate_auxkc "$new_auxkc" "candidate_auxkc" "$expected_uuid"; then
    rollback_bundle || true
    fail "candidate AuxKC validation failed; bundle rollback attempted"
fi
if ! validate_baseline_companion_members "$work/candidate_auxkc.members"; then
    rollback_bundle || true
    fail "candidate AuxKC companion set changed; bundle rollback attempted"
fi

if ! /usr/bin/ditto "$CANONICAL_AUXKC" "$auxkc_backup"; then
    rollback_bundle || true
    fail "AuxKC backup failed; bundle rollback attempted"
fi
if ! /bin/mv "$CANONICAL_AUXKC" "$auxkc_displaced"; then
    rollback_bundle || true
    fail "AuxKC displacement failed; bundle rollback attempted"
fi
if ! /bin/mv "$new_auxkc" "$CANONICAL_AUXKC"; then
    /bin/mv "$auxkc_displaced" "$CANONICAL_AUXKC" || true
    rollback_bundle || true
    fail "AuxKC swap failed; rollback attempted"
fi
if ! /usr/sbin/chown root:wheel "$CANONICAL_AUXKC" || ! /bin/chmod 0644 "$CANONICAL_AUXKC"; then
    rollback_auxkc || true
    rollback_bundle || true
    fail "canonical AuxKC permissions failed; rollback attempted"
fi
if ! validate_auxkc "$CANONICAL_AUXKC" "canonical_after" "$expected_uuid"; then
    rollback_auxkc || true
    rollback_bundle || true
    fail "canonical AuxKC validation failed; rollback attempted"
fi
if ! validate_baseline_companion_members "$work/canonical_after.members"; then
    rollback_auxkc || true
    rollback_bundle || true
    fail "canonical AuxKC companion set changed; rollback attempted"
fi
final_sha="$(/usr/bin/shasum -a 256 "$CANONICAL_BUNDLE/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
final_uuid="$(/usr/bin/dwarfdump --uuid "$CANONICAL_BUNDLE/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
if [ "$final_sha" != "$expected_sha" ] || [ "$final_uuid" != "$expected_uuid" ]; then
    rollback_auxkc || true
    rollback_bundle || true
    fail "final installed identity mismatch; rollback attempted"
fi

/bin/sync
{
    printf 'candidate_sha256=%s\n' "$expected_sha"
    printf 'candidate_uuid=%s\n' "$expected_uuid"
    printf 'canonical_member_set=PASS\n'
    printf 'auxkc_member_set=PASS\n'
    printf 'activation_state=READY_FOR_GUEST_REBOOT\n'
} >"$work/activation-summary.txt"
/bin/chmod 600 "$work/activation-summary.txt"
transaction_armed=0
printf 'ACTIVATION_READY:%s\n' "$work"
