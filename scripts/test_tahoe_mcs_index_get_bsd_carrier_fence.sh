#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-619-tahoe-bsd-mcs-index-get-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-mcs-index-get-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe MCS_INDEX_SET GET BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_MCS_INDEX_SET             66  // req_type' "$header" || fail 'selector no longer resolves to 66'
grep -Fq 'static_assert(sizeof(apple80211_mcs_index_set_data) == 0x10,' "$source" || fail 'local MCS_INDEX_SET carrier is no longer pinned to 0x10'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'get_handler_entry_raw_qword=0x00c00000020d6e10' "$artifact" || fail 'raw GET handler-table pointer is missing'
grep -Fq 'get_handler_entry_decoded_target_vmaddr=0xffffff80021d6e10' "$artifact" || fail 'raw GET handler-table target is missing'
grep -Fq 'public_req_len=0x10' "$artifact" || fail 'family 0x10 public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x42' "$artifact" || fail 'family MCS selector evidence is missing'
grep -Fq 'legacy_carrier_len=0x10' "$artifact" || fail 'family 0x10 WCL carrier evidence is missing'
grep -Fq 'carrier_mode=direct_req_data_in_place' "$artifact" || fail 'family direct-carrier evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x42' "$artifact" || fail 'public MCS selector-gate evidence is missing'
grep -Fq 'safe_metacast=IO80211NoneProtocol::gMetaClass' "$artifact" || fail 'public MCS protocol evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0x528' "$artifact" || fail 'public MCS interface-route evidence is missing'
grep -Fq 'private_raw_sha256=a59c78444dda1f28290c95dbc8c0eb3632d132e453316c17a20a3e6ddc9cf780' "$artifact" || fail 'private MCS wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=9b6eddc9e188d990fa3d5db14158286bb325fdc2f65fb842b8064bcd9ea0b818' "$artifact" || fail 'public MCS leaf bytes are not pinned'
grep -Fq 'same 0x10-byte caller buffer' "$evidence" || fail 'evidence no longer explains the carrier boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_MCS_INDEX_SET' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD GET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD GET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD GET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 13))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' || fail 'BSD MCS fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD MCS fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD MCS fence inspects the nested carrier'
fi

wrapper=$(sed -n '/getMCS_INDEX_SET(struct apple80211_mcs_index_set_data \*ad)/,/^}/p' "$source")
producer=$(sed -n '/getMCS_INDEX_SETImpl(struct apple80211_mcs_index_set_data \*ad)/,/^}/p' "$source")
wrapper_live_count=$(printf '%s\n' "$wrapper" |
    grep -Fc 'AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();' || true)
[ "$wrapper_live_count" -eq 1 ] || fail 'public GET wrapper no longer has exactly one Live admission'
printf '%s\n' "$wrapper" | grep -Fq 'return getMCS_INDEX_SETImpl(ad);' ||
    fail 'public GET wrapper no longer delegates to its unguarded implementation'
printf '%s\n' "$producer" | grep -Fq 'memset(ad, 0, sizeof(*ad));' || fail 'local producer no longer clears its complete carrier'
printf '%s\n' "$producer" | grep -Fq 'ad->version = APPLE80211_VERSION;' || fail 'local producer no longer writes its version'
printf '%s\n' "$producer" | grep -Fq 'ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];' || fail 'local producer no longer writes its MCS map'
printf '%s\n' "$producer" | grep -Fq 'return kIOReturnSuccess;' || fail 'local producer success contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_MCS_INDEX_SET:/,/case APPLE80211_IOC_MCS_VHT:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCGA80211) ? getMCS_INDEX_SET((apple80211_mcs_index_set_data *)req->req_data)' || fail 'local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' || fail 'MCS SET fallback changed unexpectedly'
if grep -Eq 'kIoc.*McsIndex|kIoc.*MCS_INDEX|APPLE80211_IOC_MCS_INDEX_SET' "$routes"; then
    fail 'a Tahoe card-specific MCS route was added without fresh contract evidence'
fi

printf 'Tahoe MCS_INDEX_SET GET BSD carrier fence: PASS\n'
