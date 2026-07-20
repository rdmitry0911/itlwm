#!/usr/bin/env bash
# Static guard for the private-only Tahoe AuxKC admission preflight.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
script = (root / "scripts/tahoe_auxkc_admission_preflight.sh").read_text()
doc = (root / "docs/tahoe_lineage_build_reproducibility.md").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe private AuxKC preflight contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"forbidden {label}: {needle}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


def function_body(text: str, name: str) -> str:
    marker = f"{name}() {{"
    start = text.find(marker)
    if start < 0:
        fail(f"missing function: {name}")
    opening = text.find("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated function: {name}")


for needle in (
    'readonly CANONICAL_AUXKC="/Library/KernelCollections/AuxiliaryKernelExtensions.kc"',
    'readonly INSTALLED_AIRPORT="/Library/Extensions/AirportItlwm.kext"',
    'readonly HIGHPOINT_IOP="/Library/Extensions/HighPointIOP.kext"',
    'readonly HIGHPOINT_RR="/Library/Extensions/HighPointRR.kext"',
    'readonly APPLE_MOBILE_DEVICE="/Library/Apple/System/Library/Extensions/AppleMobileDevice.kext"',
    'readonly REMOTE_VIRTUAL_INTERFACE="/Library/Apple/System/Library/Extensions/RemoteVirtualInterface.kext"',
    'readonly NO_UUID_MEMBER_ID="com.apple.driver.AppleMobileDevice"',
    '"com.zxystd.AirportItlwm"',
    '"com.apple.nke.rvi"',
    '"com.apple.driver.AppleMobileDevice"',
    '"com.highpoint-tech.kext.HighPointIOP"',
    '"com.highpoint-tech.kext.HighPointRR"',
    'reject_canonical_path() {',
    'require_private_path() {',
    'candidate="$(cd -P -- "$candidate" && pwd)"',
    'reject_canonical_path "$candidate" "candidate"',
    'require_private_path "$candidate" "candidate"',
    'out_parent="$(cd -P -- "$out_parent" && pwd)"',
    'reject_canonical_path "$out" "output"',
    'require_private_path "$out" "output"',
    '[ ! -L "$out" ] || fail "output must not be a symlink"',
    '[ ! -e "$out" ] && [ ! -L "$out" ] ||',
    'fail "output must be a new private evidence directory: $out"',
    'mkdir "$out"',
    'private_candidate="$out/AirportItlwm.kext"',
    '[ ! -e "$private_candidate" ] && [ ! -L "$private_candidate" ] ||',
    '[ ! -e "$temp_auxkc" ] && [ ! -L "$temp_auxkc" ] ||',
    'sudo -n ditto "$candidate" "$private_candidate"',
    'sudo -n chown -R root:wheel "$private_candidate"',
    'sudo -n chmod -R go-w "$private_candidate"',
    'codesign --verify --deep --strict "$private_candidate"',
    'candidate still imports _thread_call_cancel_wait',
    'canonical AirportItlwm bundle changed during private preflight',
    'canonical AuxKC changed during private preflight',
    'canonical AuxKC normalized member multiset changed during private preflight',
    'baseline_captured=0',
    'before_inventory_verified=0',
    'validate_exact_member_multiset() {',
    'normalize_member_manifest() {',
    'is_parenthesized_uuid(value, hex)',
    'reason=field_count:actual:%d,expected:%d',
    'reason=version_empty',
    'reason=path_empty',
    'reason=uuid_invalid',
    'canonical-members-before.validation.txt',
    'canonical-members-before.normalized.tsv',
    'canonical-members-after.validation.txt',
    'canonical-members-after.normalized.tsv',
    'private-members.validation.txt',
    'private-members.normalized.tsv',
    'canonical_after_member_validation_exit=%s',
    'canonical_after_member_normalize_exit=%s',
    'record_canonical_postflight() {',
    'canonical_after_inspect_exit=%s',
    'canonical_postflight=PASS',
    'canonical_postflight=FAIL',
    'private_admission_result=FAIL',
    'kmutil_create_exit=%s',
    'private AuxKC creation failed',
):
    require(script, needle, "private admission guard")

ordered(script, "private full AuxKC command",
        'sudo -n kmutil create -v --new aux --explicit-only',
        '-p "$private_candidate"', '-p "$HIGHPOINT_RR"', '-p "$HIGHPOINT_IOP"',
        '-p "$REMOTE_VIRTUAL_INTERFACE"', '-p "$APPLE_MOBILE_DEVICE"',
        '-B "$BOOTKC"', '-S "$SYSTEMKC"', '-A "$temp_auxkc"',
        '--allow-missing-kdk')
ordered(script, "physical private-path validation before copy",
        'candidate="$(cd -P -- "$candidate" && pwd)"',
        'reject_canonical_path "$candidate" "candidate"',
        'require_private_path "$candidate" "candidate"',
        'out_parent="$(cd -P -- "$out_parent" && pwd)"',
        'reject_canonical_path "$out" "output"',
        'require_private_path "$out" "output"',
        '[ ! -L "$out" ] || fail "output must not be a symlink"',
        '[ ! -e "$out" ] && [ ! -L "$out" ] ||',
        'mkdir "$out"',
        'sudo -n ditto "$candidate" "$private_candidate"')

main_start = script.find('sudo -n true')
if main_start < 0:
    fail("missing preflight main body")
main = script[main_start:]
postflight = function_body(script, "record_canonical_postflight")
exit_hook = function_body(script, "on_exit")
ordered(main, "canonical baseline before private materialization",
        'before_airport_sha="$(sudo -n shasum -a 256',
        'before_auxkc_sha="$(sudo -n shasum -a 256',
        'baseline_captured=1',
        'kmutil inspect --show-kext-uuids -A "$CANONICAL_AUXKC"',
        'member_manifest "$out/canonical-auxkc-before.txt"',
        'validate_exact_member_multiset',
        'normalize_member_manifest',
        'before_inventory_verified=1',
        'kmutil create -v --new aux',
        'kmutil_create_status=$?',
        'kmutil_create_exit=%s',
        'private AuxKC creation failed')
ordered(postflight, "canonical postflight witnesses",
        'kmutil inspect --show-kext-uuids -A "$CANONICAL_AUXKC"',
        'canonical_after_inspect_exit=%s',
        'member_manifest "$out/canonical-auxkc-after.txt"',
        'validate_exact_member_multiset',
        'normalize_member_manifest',
        'canonical-members-after.normalized.tsv',
        'canonical_airport_sha256_after=%s',
        'canonical_auxkc_sha256_after=%s',
        'canonical_postflight=PASS',
        'canonical_mutation=none')
ordered(postflight, "normalized canonical multiset comparison",
        'canonical_airport_sha256_after=%s',
        'canonical_auxkc_sha256_after=%s',
        'canonical-members-before.normalized.tsv',
        'canonical-members-after.normalized.tsv',
        'cmp -s "$out/canonical-members-before.normalized.tsv"')
forbid(postflight, 'fail ', "exit from canonical postflight")
ordered(exit_hook, "failure-safe canonical postflight",
        'set +e', 'if [ "$baseline_captured" -eq 1 ]; then',
        'record_canonical_postflight', 'private_admission_result=PASS',
        'private_admission_result=FAIL', 'exit_status=%s')

forbidden = (
    ('kmutil load', 'live kext load'),
    ('kextload', 'legacy kext load'),
    ('kextunload', 'legacy kext unload'),
    ('/sbin/reboot', 'guest reboot'),
    ('shutdown -r', 'guest reboot'),
    ('networksetup ', 'radio/network mutation'),
    ('ipconfig set', 'address mutation'),
    ('route add', 'route mutation'),
    ('route delete', 'route mutation'),
    (' ping ', 'data-plane traffic'),
    ('--no-authorization', 'approval-check bypass'),
    ('--elide-identifier', 'unreviewed private-command hybrid'),
    ('--force', 'unreviewed private-command hybrid'),
    (' -r /Library/', 'repository scan that can select canonical AirportItlwm'),
    ('sudo -n rm -rf "$INSTALLED_AIRPORT"', 'canonical destructive removal'),
    ('sudo -n cp "$private_candidate" "$INSTALLED_AIRPORT"', 'canonical bundle copy'),
    ('sudo -n mv "$temp_auxkc" "$CANONICAL_AUXKC"', 'canonical collection swap'),
    ('sudo -n ditto "$private_candidate" "$INSTALLED_AIRPORT"', 'canonical bundle replacement'),
    ('mkdir -p "$out"', 'reusable output-directory creation'),
)
for needle, label in forbidden:
    forbid(script, needle, label)

forbid(script, 'sort -u', "set-deduplicating inventory normalization")
forbid(script, "NF >= 3 && $1 ~ /^com\\./", "unknown-member filtering before validation")

for needle in (
    '## Private AuxKC Admission Preflight',
    'scripts/tahoe_auxkc_admission_preflight.sh',
    'must physically resolve beneath `/private`',
    'canonical AirportItlwm bundle and canonical AuxKC remain read-only',
    '`--out` must name a non-existent private directory',
    'exit path always records and verifies the canonical after-witnesses',
    '--explicit-only',
    'Full AuxKC Install And Reboot Envelope',
    'Do not use an explicit one-bundle `--bundle-path` collection',
    'never live-unload the driver',
):
    require(doc, needle, "documented private/full AuxKC boundary")

print("PASS: Tahoe private AuxKC admission preflight contract")
PY

extract_preflight_function() {
    local function_name="$1"
    awk -v name="$function_name" '
        $0 ~ "^" name "\\(\\)[[:space:]]*\\{" { capture = 1 }
        capture { print }
        capture && /^}/ { exit }
    ' "$root/scripts/tahoe_auxkc_admission_preflight.sh"
}

run_member_multiset_fixtures() (
    set -euo pipefail

    fixture_root="$(mktemp -d "${TMPDIR:-/tmp}/aiam-auxkc-member-fixture.XXXXXX")"
    trap 'rm -rf "$fixture_root"' EXIT

    eval "$(extract_preflight_function member_manifest)"
    eval "$(extract_preflight_function validate_exact_member_multiset)"
    eval "$(extract_preflight_function normalize_member_manifest)"
    REQUIRED_IDS=(
        "com.zxystd.AirportItlwm"
        "com.apple.nke.rvi"
        "com.apple.driver.AppleMobileDevice"
        "com.highpoint-tech.kext.HighPointIOP"
        "com.highpoint-tech.kext.HighPointRR"
    )
    NO_UUID_MEMBER_ID="com.apple.driver.AppleMobileDevice"

    row_airport=$'com.zxystd.AirportItlwm\t2.4.0\t/private/AirportItlwm.kext\t(11111111-1111-1111-1111-111111111111)'
    row_rvi=$'com.apple.nke.rvi\t2.1.0\t/Library/Apple/System/Library/Extensions/RemoteVirtualInterface.kext\t(22222222-2222-2222-2222-222222222222)'
    row_mobile=$'com.apple.driver.AppleMobileDevice\t4.0\t/Library/Apple/System/Library/Extensions/AppleMobileDevice.kext\t'
    row_iop=$'com.highpoint-tech.kext.HighPointIOP\t4.4.5\t/Library/Extensions/HighPointIOP.kext\t(33333333-3333-3333-3333-333333333333)'
    row_rr=$'com.highpoint-tech.kext.HighPointRR\t4.22.1\t/Library/Extensions/HighPointRR.kext\t(44444444-4444-4444-4444-444444444444)'

    write_inspect_fixture() {
        local destination="$1"
        shift
        {
            printf '%s\n' 'auxiliary kext collection at /private/fixture.kc (00000000-0000-0000-0000-000000000000):'
            printf '%s\n' 'Extension Information:'
            printf '%s\n' "$@"
            printf '\n'
        } > "$destination"
    }

    extract_and_validate() {
        local label="$1"
        local expected="$2"
        local inspect="$fixture_root/$label.inspect.txt"
        local raw="$fixture_root/$label.raw.tsv"
        local report="$fixture_root/$label.validation.txt"
        local result

        member_manifest "$inspect" > "$raw"
        if validate_exact_member_multiset "$raw" "$report"; then
            result=0
        else
            result=1
        fi
        if [ "$expected" = pass ]; then
            [ "$result" -eq 0 ] || {
                sed -n '1,160p' "$report" >&2
                exit 1
            }
        else
            [ "$result" -ne 0 ] || {
                sed -n '1,160p' "$report" >&2
                exit 1
            }
        fi
    }

    write_inspect_fixture "$fixture_root/baseline.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate baseline pass
    normalize_member_manifest "$fixture_root/baseline.raw.tsv" \
        "$fixture_root/baseline.normalized.tsv"

    write_inspect_fixture "$fixture_root/permutation.inspect.txt" \
        "$row_rr" "$row_mobile" "$row_airport" "$row_iop" "$row_rvi"
    extract_and_validate permutation pass
    normalize_member_manifest "$fixture_root/permutation.raw.tsv" \
        "$fixture_root/permutation.normalized.tsv"
    cmp -s "$fixture_root/baseline.normalized.tsv" \
           "$fixture_root/permutation.normalized.tsv" || exit 1

    row_rvi_changed=$'com.apple.nke.rvi\t2.1.0\t/Library/Apple/System/Library/Extensions/RemoteVirtualInterface.kext\t(22222222-2222-2222-2222-222222222223)'
    write_inspect_fixture "$fixture_root/changed.inspect.txt" \
        "$row_airport" "$row_rvi_changed" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate changed pass
    normalize_member_manifest "$fixture_root/changed.raw.tsv" \
        "$fixture_root/changed.normalized.tsv"
    ! cmp -s "$fixture_root/baseline.normalized.tsv" \
             "$fixture_root/changed.normalized.tsv"

    write_inspect_fixture "$fixture_root/missing.inspect.txt" \
        "$row_airport" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate missing fail
    grep -Fxq 'missing=com.apple.nke.rvi' "$fixture_root/missing.validation.txt"

    write_inspect_fixture "$fixture_root/duplicate.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_rr" "$row_rr"
    extract_and_validate duplicate fail
    grep -Fxq 'duplicate=com.highpoint-tech.kext.HighPointRR,count=2' \
        "$fixture_root/duplicate.validation.txt"
    grep -Fxq 'missing=com.highpoint-tech.kext.HighPointIOP' \
        "$fixture_root/duplicate.validation.txt"

    row_unknown=$'org.example.Unknown\t1.0\t/private/Unknown.kext\t(55555555-5555-5555-5555-555555555555)'
    write_inspect_fixture "$fixture_root/unknown.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_iop" "$row_unknown"
    extract_and_validate unknown fail
    grep -Fxq 'unknown=org.example.Unknown,count=1' \
        "$fixture_root/unknown.validation.txt"
    grep -Fxq 'missing=com.highpoint-tech.kext.HighPointRR' \
        "$fixture_root/unknown.validation.txt"

    row_malformed=$'com.apple.nke.rvi\t2.1.0'
    write_inspect_fixture "$fixture_root/malformed.inspect.txt" \
        "$row_airport" "$row_malformed" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate malformed fail
    grep -Fq 'malformed=line:2' "$fixture_root/malformed.validation.txt"

    row_extra="$row_airport"$'\textra-field'
    write_inspect_fixture "$fixture_root/extra-field.inspect.txt" \
        "$row_extra" "$row_rvi" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate extra-field fail
    grep -Fxq 'malformed=line:1,reason=field_count:actual:5,expected:4' \
        "$fixture_root/extra-field.validation.txt"

    row_empty_version=$'com.highpoint-tech.kext.HighPointIOP\t\t/Library/Extensions/HighPointIOP.kext\t(33333333-3333-3333-3333-333333333333)'
    write_inspect_fixture "$fixture_root/empty-version.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_empty_version" "$row_rr"
    extract_and_validate empty-version fail
    grep -Fxq 'malformed=line:4,reason=version_empty' \
        "$fixture_root/empty-version.validation.txt"

    row_empty_path=$'com.apple.nke.rvi\t2.1.0\t\t(22222222-2222-2222-2222-222222222222)'
    write_inspect_fixture "$fixture_root/empty-path.inspect.txt" \
        "$row_airport" "$row_empty_path" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate empty-path fail
    grep -Fxq 'malformed=line:2,reason=path_empty' \
        "$fixture_root/empty-path.validation.txt"

    row_invalid_uuid=$'com.highpoint-tech.kext.HighPointRR\t4.22.1\t/Library/Extensions/HighPointRR.kext\t(not-a-uuid)'
    write_inspect_fixture "$fixture_root/invalid-uuid.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_iop" "$row_invalid_uuid"
    extract_and_validate invalid-uuid fail
    grep -Fxq 'malformed=line:5,reason=uuid_invalid' \
        "$fixture_root/invalid-uuid.validation.txt"

    row_relative_path=$'com.highpoint-tech.kext.HighPointIOP\t4.4.5\tLibrary/Extensions/HighPointIOP.kext\t(33333333-3333-3333-3333-333333333333)'
    write_inspect_fixture "$fixture_root/relative-path.inspect.txt" \
        "$row_airport" "$row_rvi" "$row_mobile" "$row_relative_path" "$row_rr"
    extract_and_validate relative-path fail
    grep -Fxq 'malformed=line:4,reason=path_not_absolute' \
        "$fixture_root/relative-path.validation.txt"

    row_empty_identity=$'\t2.1.0\t/Library/Apple/System/Library/Extensions/RemoteVirtualInterface.kext\t(22222222-2222-2222-2222-222222222222)'
    write_inspect_fixture "$fixture_root/empty-identity.inspect.txt" \
        "$row_airport" "$row_empty_identity" "$row_mobile" "$row_iop" "$row_rr"
    extract_and_validate empty-identity fail
    grep -Fxq 'malformed=line:2,reason=identity_empty' \
        "$fixture_root/empty-identity.validation.txt"
    grep -Fxq 'missing=com.apple.nke.rvi' \
        "$fixture_root/empty-identity.validation.txt"
)

run_member_multiset_fixtures
printf 'PASS: Tahoe private AuxKC normalized-member fixtures\n'
