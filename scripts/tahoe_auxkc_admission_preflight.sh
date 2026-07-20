#!/usr/bin/env bash
# Build and inspect a private Tahoe AuxKC candidate without touching the
# canonical AirportItlwm bundle or canonical AuxiliaryKernelExtensions.kc.
set -euo pipefail

readonly AIRPORT_ID="com.zxystd.AirportItlwm"
readonly CANONICAL_AUXKC="/Library/KernelCollections/AuxiliaryKernelExtensions.kc"
readonly BOOTKC="/System/Library/KernelCollections/BootKernelExtensions.kc"
readonly SYSTEMKC="/System/Library/KernelCollections/SystemKernelExtensions.kc"
readonly INSTALLED_AIRPORT="/Library/Extensions/AirportItlwm.kext"
readonly INSTALLED_AIRPORT_BINARY="$INSTALLED_AIRPORT/Contents/MacOS/AirportItlwm"
readonly HIGHPOINT_IOP="/Library/Extensions/HighPointIOP.kext"
readonly HIGHPOINT_RR="/Library/Extensions/HighPointRR.kext"
readonly APPLE_MOBILE_DEVICE="/Library/Apple/System/Library/Extensions/AppleMobileDevice.kext"
readonly REMOTE_VIRTUAL_INTERFACE="/Library/Apple/System/Library/Extensions/RemoteVirtualInterface.kext"
readonly NO_UUID_MEMBER_ID="com.apple.driver.AppleMobileDevice"

readonly -a REQUIRED_IDS=(
    "com.zxystd.AirportItlwm"
    "com.apple.nke.rvi"
    "com.apple.driver.AppleMobileDevice"
    "com.highpoint-tech.kext.HighPointIOP"
    "com.highpoint-tech.kext.HighPointRR"
)

usage() {
    cat >&2 <<'EOF'
usage: tahoe_auxkc_admission_preflight.sh --candidate /private/path/AirportItlwm.kext --out /private/evidence-dir

Creates and inspects only a private temporary AuxKC. It never installs, loads,
unloads, swaps, or reboots a kext. The candidate and output directory must not
resolve outside /private.
EOF
}

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

reject_canonical_path() {
    local path="$1"
    local label="$2"
    case "$path" in
        /Library/Extensions|/Library/Extensions/*|/Library/KernelCollections|/Library/KernelCollections/*)
            fail "$label must not be in a canonical extension or collection path"
            ;;
    esac
}

require_private_path() {
    local path="$1"
    local label="$2"
    case "$path" in
        /private|/private/*) ;;
        *) fail "$label must resolve under /private" ;;
    esac
}

candidate=""
out=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --candidate)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            candidate="$2"
            shift 2
            ;;
        --out)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            out="$2"
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

[ -n "$candidate" ] && [ -n "$out" ] || { usage; exit 2; }
case "$candidate" in
    /*) ;;
    *) fail "candidate must be an absolute private path" ;;
esac
case "$out" in
    /*) ;;
    *) fail "output must be an absolute private path" ;;
esac

[ -d "$candidate" ] || fail "candidate bundle is missing: $candidate"
candidate="$(cd -P -- "$candidate" && pwd)"
reject_canonical_path "$candidate" "candidate"
require_private_path "$candidate" "candidate"
candidate_binary="$candidate/Contents/MacOS/AirportItlwm"
[ -f "$candidate_binary" ] || fail "candidate Mach-O is missing"

out_parent="$(dirname "$out")"
out_leaf="$(basename "$out")"
[ "$out" != "/" ] || fail "output must name a private evidence directory"
case "$out_leaf" in
    .|..|/)
        fail "output must name a private evidence directory"
        ;;
esac
[ -d "$out_parent" ] || fail "output parent is missing: $out_parent"
out_parent="$(cd -P -- "$out_parent" && pwd)"
out="$out_parent/$out_leaf"
reject_canonical_path "$out" "output"
require_private_path "$out" "output"
[ ! -L "$out" ] || fail "output must not be a symlink"
[ ! -e "$out" ] && [ ! -L "$out" ] ||
    fail "output must be a new private evidence directory: $out"

for path in "$CANONICAL_AUXKC" "$BOOTKC" "$SYSTEMKC" \
            "$INSTALLED_AIRPORT" "$INSTALLED_AIRPORT_BINARY" \
            "$HIGHPOINT_IOP" "$HIGHPOINT_RR" "$APPLE_MOBILE_DEVICE" \
            "$REMOTE_VIRTUAL_INTERFACE"; do
    [ -e "$path" ] || fail "required current system path is missing: $path"
done

umask 077
mkdir "$out"
out="$(cd -P -- "$out" && pwd)"
reject_canonical_path "$out" "output"
require_private_path "$out" "output"
private_candidate="$out/AirportItlwm.kext"
temp_auxkc="$out/exact-5.kc"
[ ! -e "$private_candidate" ] && [ ! -L "$private_candidate" ] ||
    fail "refusing to overwrite existing private candidate"
[ ! -e "$temp_auxkc" ] && [ ! -L "$temp_auxkc" ] ||
    fail "refusing to overwrite existing private AuxKC"
summary="$out/summary.txt"
: > "$summary"
baseline_captured=0
before_inventory_verified=0
before_airport_sha=""
before_auxkc_sha=""

member_manifest() {
    local inspect="$1"
    # Preserve every raw member row.  Do not pre-filter unknown or malformed
    # identifiers: validation must see and reject them before normalization.
    awk '
        $0 == "Extension Information:" { in_members = 1; saw_header = 1; next }
        in_members && $0 == "" { exit }
        in_members { print }
        END { if (!saw_header) exit 2 }
    ' "$inspect"
}

validate_exact_member_multiset() {
    local manifest="$1"
    local report="$2"
    local required_csv

    required_csv="$(IFS=,; printf '%s' "${REQUIRED_IDS[*]}")"
    if [ ! -f "$manifest" ]; then
        printf 'status=FAIL\nreason=manifest_missing\n' > "$report"
        return 1
    fi

    LC_ALL=C awk -F '\t' -v required_ids="$required_csv" \
        -v no_uuid_member="$NO_UUID_MEMBER_ID" '
        function is_parenthesized_uuid(value, hex) {
            if (length(value) != 38 || substr(value, 1, 1) != "(" ||
                substr(value, 38, 1) != ")") {
                return 0
            }
            hex = substr(value, 2, 36)
            if (substr(hex, 9, 1) != "-" ||
                substr(hex, 14, 1) != "-" ||
                substr(hex, 19, 1) != "-" ||
                substr(hex, 24, 1) != "-") {
                return 0
            }
            gsub(/-/, "", hex)
            return length(hex) == 32 && hex ~ /^[0-9A-Fa-f]+$/
        }
        BEGIN {
            expected_count = split(required_ids, required, ",")
            for (idx = 1; idx <= expected_count; idx++) {
                expected[required[idx]] = 1
                expected_fields[required[idx]] = 4
            }
        }
        {
            records++
            identifier = $1
            if (identifier == "") {
                printf "malformed=line:%d,reason=identity_empty\n", NR
                invalid = 1
                next
            }
            seen[identifier]++
            if (NF < 3) {
                printf "malformed=line:%d,reason=field_count:actual:%d,minimum:3\n", NR, NF
                invalid = 1
            }
            if ($2 == "") {
                printf "malformed=line:%d,reason=version_empty\n", NR
                invalid = 1
            }
            if ($3 == "") {
                printf "malformed=line:%d,reason=path_empty\n", NR
                invalid = 1
            } else if ($3 !~ /^\//) {
                printf "malformed=line:%d,reason=path_not_absolute\n", NR
                invalid = 1
            }
            if (identifier in expected) {
                if (NF != expected_fields[identifier]) {
                    printf "malformed=line:%d,reason=field_count:actual:%d,expected:%d\n", \
                        NR, NF, expected_fields[identifier]
                    invalid = 1
                }
                if (identifier == no_uuid_member && $4 != "") {
                    printf "malformed=line:%d,reason=uuid_unexpected\n", NR
                    invalid = 1
                } else if (identifier != no_uuid_member &&
                           !is_parenthesized_uuid($4)) {
                    printf "malformed=line:%d,reason=uuid_invalid\n", NR
                    invalid = 1
                }
            } else if (!(identifier in unknown_seen)) {
                unknown_seen[identifier] = 1
                unknown_order[++unknown_count] = identifier
            }
        }
        END {
            printf "records=%d\nexpected_records=%d\n", records + 0, expected_count
            if (records != expected_count) {
                printf "record_count_mismatch=expected:%d,actual:%d\n", expected_count, records + 0
                invalid = 1
            }
            for (idx = 1; idx <= expected_count; idx++) {
                identifier = required[idx]
                if (seen[identifier] == 0) {
                    printf "missing=%s\n", identifier
                    invalid = 1
                } else if (seen[identifier] > 1) {
                    printf "duplicate=%s,count=%d\n", identifier, seen[identifier]
                    invalid = 1
                }
            }
            for (idx = 1; idx <= unknown_count; idx++) {
                identifier = unknown_order[idx]
                printf "unknown=%s,count=%d\n", identifier, seen[identifier]
                if (seen[identifier] > 1) {
                    printf "duplicate=%s,count=%d\n", identifier, seen[identifier]
                }
                invalid = 1
            }
            if (invalid) {
                print "status=FAIL"
                exit 1
            }
            print "status=PASS"
        }
    ' "$manifest" > "$report"
}

normalize_member_manifest() {
    local manifest="$1"
    local normalized="$2"
    # Keep duplicate rows visible; validation proves a true multiset first.
    LC_ALL=C sort "$manifest" > "$normalized"
}

member_line() {
    local manifest="$1"
    local identifier="$2"
    # The validator runs before every caller; reject ambiguity defensively.
    awk -F '\t' -v id="$identifier" '
        $1 == id { matches++; line = $0 }
        END { if (matches == 1) print line; else exit 1 }
    ' "$manifest"
}

record_canonical_postflight() {
    local result=0
    local mutation="none"
    local after_airport_sha=""
    local after_auxkc_sha=""
    local inspect_status=0
    local airport_sha_status=0
    local auxkc_sha_status=0
    local member_extract_status=1
    local member_validation_status=1
    local member_normalize_status=1

    sudo -n kmutil inspect --show-kext-uuids -A "$CANONICAL_AUXKC" \
        > "$out/canonical-auxkc-after.txt"
    inspect_status=$?
    printf 'canonical_after_inspect_exit=%s\n' "$inspect_status" >> "$summary"
    if [ "$inspect_status" -ne 0 ]; then
        result=1
        mutation="unverified"
    else
        member_manifest "$out/canonical-auxkc-after.txt" \
            > "$out/canonical-members-after.tsv"
        member_extract_status=$?
        if [ "$member_extract_status" -ne 0 ]; then
            result=1
            mutation="unverified"
        else
            validate_exact_member_multiset \
                "$out/canonical-members-after.tsv" \
                "$out/canonical-members-after.validation.txt"
            member_validation_status=$?
            if [ "$member_validation_status" -ne 0 ]; then
                result=1
                mutation="unverified"
            else
                normalize_member_manifest \
                    "$out/canonical-members-after.tsv" \
                    "$out/canonical-members-after.normalized.tsv"
                member_normalize_status=$?
                if [ "$member_normalize_status" -ne 0 ]; then
                    result=1
                    mutation="unverified"
                fi
            fi
        fi
    fi
    printf 'canonical_after_member_extract_exit=%s\n' "$member_extract_status" >> "$summary"
    printf 'canonical_after_member_validation_exit=%s\n' "$member_validation_status" >> "$summary"
    printf 'canonical_after_member_normalize_exit=%s\n' "$member_normalize_status" >> "$summary"

    after_airport_sha="$(sudo -n shasum -a 256 "$INSTALLED_AIRPORT_BINARY" | awk '{print $1}')"
    airport_sha_status=$?
    after_auxkc_sha="$(sudo -n shasum -a 256 "$CANONICAL_AUXKC" | awk '{print $1}')"
    auxkc_sha_status=$?
    printf 'canonical_airport_sha256_after=%s\n' "$after_airport_sha" >> "$summary"
    printf 'canonical_auxkc_sha256_after=%s\n' "$after_auxkc_sha" >> "$summary"

    if [ "$airport_sha_status" -ne 0 ] || [ -z "$before_airport_sha" ] ||
       [ -z "$after_airport_sha" ]; then
        result=1
        [ "$mutation" = "detected" ] || mutation="unverified"
    elif [ "$before_airport_sha" != "$after_airport_sha" ]; then
        printf 'FAIL: canonical AirportItlwm bundle changed during private preflight\n' \
            >> "$summary"
        result=1
        mutation="detected"
    fi

    if [ "$auxkc_sha_status" -ne 0 ] || [ -z "$before_auxkc_sha" ] ||
       [ -z "$after_auxkc_sha" ]; then
        result=1
        [ "$mutation" = "detected" ] || mutation="unverified"
    elif [ "$before_auxkc_sha" != "$after_auxkc_sha" ]; then
        printf 'FAIL: canonical AuxKC changed during private preflight\n' >> "$summary"
        result=1
        mutation="detected"
    fi

    if [ "$before_inventory_verified" -ne 1 ] ||
       [ "$inspect_status" -ne 0 ] || [ "$member_extract_status" -ne 0 ] ||
       [ "$member_validation_status" -ne 0 ] || [ "$member_normalize_status" -ne 0 ] ||
       [ ! -f "$out/canonical-members-before.tsv" ] ||
       [ ! -f "$out/canonical-members-before.validation.txt" ] ||
       [ ! -f "$out/canonical-members-before.normalized.tsv" ] ||
       [ ! -f "$out/canonical-members-after.tsv" ] ||
       [ ! -f "$out/canonical-members-after.validation.txt" ] ||
       [ ! -f "$out/canonical-members-after.normalized.tsv" ]; then
        result=1
        [ "$mutation" = "detected" ] || mutation="unverified"
    elif ! cmp -s "$out/canonical-members-before.normalized.tsv" \
                  "$out/canonical-members-after.normalized.tsv"; then
        printf 'FAIL: canonical AuxKC normalized member multiset changed during private preflight\n' \
            >> "$summary"
        result=1
        mutation="detected"
    fi

    if [ "$result" -eq 0 ]; then
        printf 'canonical_postflight=PASS\n' >> "$summary"
        printf 'canonical_mutation=none\n' >> "$summary"
        printf 'auxkc_members=5\n' >> "$summary"
    else
        printf 'canonical_postflight=FAIL\n' >> "$summary"
        printf 'canonical_mutation=%s\n' "$mutation" >> "$summary"
    fi
    return "$result"
}

on_exit() {
    local original_status=$?
    local final_status="$original_status"
    local postflight_status=0
    set +e
    if [ "$baseline_captured" -eq 1 ]; then
        record_canonical_postflight
        postflight_status=$?
        if [ "$postflight_status" -ne 0 ]; then
            final_status=1
        fi
    else
        postflight_status=1
        final_status=1
        printf 'canonical_postflight=NOT_STARTED\n' >> "$summary"
        printf 'canonical_mutation=unverified\n' >> "$summary"
    fi
    if [ "$original_status" -eq 0 ] && [ "$postflight_status" -eq 0 ]; then
        printf 'private_admission_result=PASS\n' >> "$summary"
    else
        printf 'private_admission_result=FAIL\n' >> "$summary"
    fi
    printf 'preflight_command_exit=%s\n' "$original_status" >> "$summary"
    printf 'exit_status=%s\n' "$final_status" >> "$summary"
    trap - EXIT
    exit "$final_status"
}
trap on_exit EXIT

sudo -n true
source_candidate_sha="$(shasum -a 256 "$candidate_binary" | awk '{print $1}')"
set +e
codesign --verify --deep --strict "$candidate" > "$out/candidate-source-codesign.txt" 2>&1
source_codesign_status=$?
set -e
sudo -n ditto "$candidate" "$private_candidate"
sudo -n chown -R root:wheel "$private_candidate"
sudo -n chmod -R go-w "$private_candidate"
private_binary="$private_candidate/Contents/MacOS/AirportItlwm"
candidate_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$private_candidate/Contents/Info.plist")"
[ "$candidate_id" = "$AIRPORT_ID" ] || fail "candidate bundle identifier is $candidate_id"
candidate_uuid="$(dwarfdump --uuid "$private_binary" | awk '/UUID:/{print $2; exit}')"
[ -n "$candidate_uuid" ] || fail "candidate Mach-O has no UUID"
if nm -u "$private_binary" | grep -qx '_thread_call_cancel_wait'; then
    fail "candidate still imports _thread_call_cancel_wait"
fi
candidate_sha="$(shasum -a 256 "$private_binary" | awk '{print $1}')"
[ "$source_candidate_sha" = "$candidate_sha" ] || fail "private candidate copy changed Mach-O bytes"
set +e
codesign --verify --deep --strict "$private_candidate" > "$out/private-candidate-codesign.txt" 2>&1
private_codesign_status=$?
set -e
before_airport_sha="$(sudo -n shasum -a 256 "$INSTALLED_AIRPORT_BINARY" | awk '{print $1}')"
before_auxkc_sha="$(sudo -n shasum -a 256 "$CANONICAL_AUXKC" | awk '{print $1}')"
printf 'candidate_uuid=%s\n' "$candidate_uuid" >> "$summary"
printf 'candidate_source_sha256=%s\n' "$source_candidate_sha" >> "$summary"
printf 'candidate_sha256=%s\n' "$candidate_sha" >> "$summary"
printf 'candidate_source_codesign_verify_exit=%s\n' "$source_codesign_status" >> "$summary"
printf 'candidate_private_codesign_verify_exit=%s\n' "$private_codesign_status" >> "$summary"
printf 'canonical_airport_sha256_before=%s\n' "$before_airport_sha" >> "$summary"
printf 'canonical_auxkc_sha256_before=%s\n' "$before_auxkc_sha" >> "$summary"
baseline_captured=1

sudo -n kmutil inspect --show-kext-uuids -A "$CANONICAL_AUXKC" \
    > "$out/canonical-auxkc-before.txt"
set +e
member_manifest "$out/canonical-auxkc-before.txt" > "$out/canonical-members-before.tsv"
before_member_extract_status=$?
if [ "$before_member_extract_status" -eq 0 ]; then
    validate_exact_member_multiset \
        "$out/canonical-members-before.tsv" \
        "$out/canonical-members-before.validation.txt"
    before_member_validation_status=$?
else
    before_member_validation_status=1
fi
if [ "$before_member_validation_status" -eq 0 ]; then
    normalize_member_manifest \
        "$out/canonical-members-before.tsv" \
        "$out/canonical-members-before.normalized.tsv"
    before_member_normalize_status=$?
else
    before_member_normalize_status=1
fi
set -e
printf 'canonical_before_member_extract_exit=%s\n' "$before_member_extract_status" >> "$summary"
printf 'canonical_before_member_validation_exit=%s\n' "$before_member_validation_status" >> "$summary"
printf 'canonical_before_member_normalize_exit=%s\n' "$before_member_normalize_status" >> "$summary"
[ "$before_member_extract_status" -eq 0 ] ||
    fail "canonical baseline member extraction failed"
[ "$before_member_validation_status" -eq 0 ] ||
    fail "canonical baseline member multiset is not exact; see canonical-members-before.validation.txt"
[ "$before_member_normalize_status" -eq 0 ] ||
    fail "canonical baseline member normalization failed"
before_inventory_verified=1

set +e
sudo -n kmutil create -v --new aux --explicit-only \
    -p "$private_candidate" \
    -p "$HIGHPOINT_RR" \
    -p "$HIGHPOINT_IOP" \
    -p "$REMOTE_VIRTUAL_INTERFACE" \
    -p "$APPLE_MOBILE_DEVICE" \
    -B "$BOOTKC" \
    -S "$SYSTEMKC" \
    -A "$temp_auxkc" \
    --allow-missing-kdk \
    > "$out/kmutil-create.txt" 2>&1
kmutil_create_status=$?
set -e
printf 'kmutil_create_exit=%s\n' "$kmutil_create_status" >> "$summary"
[ "$kmutil_create_status" -eq 0 ] || fail "private AuxKC creation failed"

set +e
sudo -n kmutil inspect --show-kext-uuids -A "$temp_auxkc" \
    > "$out/private-auxkc.txt"
private_inspect_status=$?
sudo -n kmutil inspect --show-kext-uuids -A "$temp_auxkc" -B "$BOOTKC" \
    > "$out/private-auxkc-with-bootkc.txt"
private_bootkc_inspect_status=$?
set -e
printf 'private_auxkc_inspect_exit=%s\n' "$private_inspect_status" >> "$summary"
printf 'private_auxkc_bootkc_inspect_exit=%s\n' "$private_bootkc_inspect_status" >> "$summary"
[ "$private_inspect_status" -eq 0 ] || fail "private AuxKC inspection failed"
[ "$private_bootkc_inspect_status" -eq 0 ] ||
    fail "private AuxKC BootKC inspection failed"
set +e
member_manifest "$out/private-auxkc.txt" > "$out/private-members.tsv"
private_member_extract_status=$?
if [ "$private_member_extract_status" -eq 0 ]; then
    validate_exact_member_multiset \
        "$out/private-members.tsv" \
        "$out/private-members.validation.txt"
    private_member_validation_status=$?
else
    private_member_validation_status=1
fi
if [ "$private_member_validation_status" -eq 0 ]; then
    normalize_member_manifest \
        "$out/private-members.tsv" \
        "$out/private-members.normalized.tsv"
    private_member_normalize_status=$?
else
    private_member_normalize_status=1
fi
set -e
printf 'private_member_extract_exit=%s\n' "$private_member_extract_status" >> "$summary"
printf 'private_member_validation_exit=%s\n' "$private_member_validation_status" >> "$summary"
printf 'private_member_normalize_exit=%s\n' "$private_member_normalize_status" >> "$summary"
[ "$private_member_extract_status" -eq 0 ] ||
    fail "private AuxKC member extraction failed"
[ "$private_member_validation_status" -eq 0 ] ||
    fail "private AuxKC member multiset is not exact; see private-members.validation.txt"
[ "$private_member_normalize_status" -eq 0 ] ||
    fail "private AuxKC member normalization failed"

candidate_member="$(member_line "$out/private-members.tsv" "$AIRPORT_ID")"
case "$candidate_member" in
    *"($candidate_uuid)"*) ;;
    *) fail "private AuxKC does not contain the exact candidate UUID" ;;
esac
for identifier in "${REQUIRED_IDS[@]:1}"; do
    before_line="$(member_line "$out/canonical-members-before.tsv" "$identifier")"
    private_line="$(member_line "$out/private-members.tsv" "$identifier")"
    [ "$before_line" = "$private_line" ] ||
        fail "non-Airport auxiliary member changed: $identifier"
done
