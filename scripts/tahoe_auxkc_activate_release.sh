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
  --expected-sha256 SHA256 \
  --expected-uuid UUID

Performs a transactional replacement of the canonical AirportItlwm bundle and
AuxKC only after validating the exact five-member collection.  It creates
immutable, timestamped rollback copies, never direct-loads/unloads a kext, and
does not reboot.  Candidate and work-root must resolve below /private.
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

candidate=""
work_root=""
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

[ -n "$candidate" ] && [ -n "$work_root" ] && [ -n "$expected_sha" ] && [ -n "$expected_uuid" ] || {
    usage
    exit 2
}

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
[ -f "$candidate/Contents/MacOS/AirportItlwm" ] || fail "candidate Mach-O is missing"

[ -d "$work_root" ] || fail "work root is missing"
work_root="$(cd -P -- "$work_root" && pwd)"
require_private_path "$work_root" "work root"

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

for path in "$CANONICAL_BUNDLE" "$CANONICAL_AUXKC" "$BOOTKC" "$SYSTEMKC"; do
    [ -e "$path" ] || fail "required current system path is missing: $path"
done
for path in "$work" "$new_bundle" "$bundle_backup" "$bundle_displaced" \
            "$new_auxkc" "$auxkc_backup" "$auxkc_displaced"; do
    [ ! -e "$path" ] && [ ! -L "$path" ] || fail "refusing to overwrite existing path: $path"
done

candidate_sha="$(/usr/bin/shasum -a 256 "$candidate/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
[ "$candidate_sha" = "$expected_sha" ] || fail "candidate SHA-256 mismatch"
candidate_uuid="$(/usr/bin/dwarfdump --uuid "$candidate/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
[ "$candidate_uuid" = "$expected_uuid" ] || fail "candidate UUID mismatch"

/bin/mkdir "$work"
/bin/chmod 700 "$work"
if ! validate_auxkc "$CANONICAL_AUXKC" "canonical_before"; then
    fail "canonical collection is not the approved five-member set"
fi

if ! /usr/bin/ditto "$CANONICAL_BUNDLE" "$bundle_backup"; then
    fail "bundle backup failed"
fi
if ! /usr/bin/ditto "$candidate" "$new_bundle"; then
    fail "candidate staging failed"
fi
if ! /usr/sbin/chown -R root:wheel "$new_bundle" || ! /bin/chmod -R go-w "$new_bundle"; then
    fail "candidate staging permissions failed"
fi
staged_sha="$(/usr/bin/shasum -a 256 "$new_bundle/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $1}')"
staged_uuid="$(/usr/bin/dwarfdump --uuid "$new_bundle/Contents/MacOS/AirportItlwm" | /usr/bin/awk '{print $2}')"
if [ "$staged_sha" != "$expected_sha" ] || [ "$staged_uuid" != "$expected_uuid" ]; then
    fail "candidate staging identity mismatch"
fi

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
printf 'ACTIVATION_READY:%s\n' "$work"
