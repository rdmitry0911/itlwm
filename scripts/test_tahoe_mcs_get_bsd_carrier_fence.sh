#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-620-tahoe-bsd-mcs-get-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-mcs-get-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe MCS GET BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_MCS                       57  // req_type' "$header" || fail 'selector no longer resolves to 57'
grep -Fq 'static_assert(sizeof(apple80211_mcs_data) == 0x08,' "$source" || fail 'local MCS carrier is no longer pinned to 0x08'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'get_handler_entry_raw_qword=0x02400000020d6d4a' "$artifact" || fail 'raw GET handler-table pointer is missing'
grep -Fq 'get_handler_entry_decoded_target_vmaddr=0xffffff80021d6d4a' "$artifact" || fail 'raw GET handler-table target is missing'
grep -Fq 'public_req_len=0x04' "$artifact" || fail 'family four-byte public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x39' "$artifact" || fail 'family MCS selector evidence is missing'
grep -Fq 'legacy_carrier_len=0x08' "$artifact" || fail 'family eight-byte WCL carrier evidence is missing'
grep -Fq 'carrier_mode=internal_stack_copyout' "$artifact" || fail 'family stack-carrier evidence is missing'
grep -Fq 'internal_stack_carrier=true' "$artifact" || fail 'family stack-carrier ownership is missing'
grep -Fq 'wrapper_copyout_source_offset=0x04' "$artifact" || fail 'family copyout offset is missing'
grep -Fq 'wrapper_copyout_len=0x04' "$artifact" || fail 'family copyout length is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x39' "$artifact" || fail 'public MCS selector-gate evidence is missing'
grep -Fq 'safe_metacast=IO80211InfraProtocol::gMetaClass' "$artifact" || fail 'public MCS protocol evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0xee8' "$artifact" || fail 'public MCS interface-route evidence is missing'
grep -Fq 'private_raw_sha256=6d659f0b3a671104615745a76246b1e1ad005359f1e4288abff8c45c7ac0c8e0' "$artifact" || fail 'private MCS wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=f941b18e2d75eed34fc11f7f313b18f8ee3370b1429767c6f3a91618ed22c767' "$artifact" || fail 'public MCS leaf bytes are not pinned'
grep -Fq 'Only the four-byte index' "$evidence" || fail 'evidence no longer explains the copyout boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_MCS)' | cut -d: -f1)
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

producer=$(sed -n '/getMCS(struct apple80211_mcs_data\* md)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'md->version = APPLE80211_VERSION;' || fail 'local producer no longer writes its version'
printf '%s\n' "$producer" | grep -Fq 'md->index = 0;' || fail 'local producer no longer initializes its index'
printf '%s\n' "$producer" | grep -Fq 'decodeTahoeMcsIndexFromCachedNrate(rate, &index)' || fail 'local producer no longer derives the MCS index'
printf '%s\n' "$producer" | grep -Fq 'return ret;' || fail 'local producer return contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_MCS:/,/case APPLE80211_IOC_MCS_INDEX_SET:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCGA80211) ? getMCS((apple80211_mcs_data *)req->req_data)' || fail 'local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' || fail 'MCS SET fallback changed unexpectedly'
if grep -Eq 'APPLE80211_IOC_MCS([^_A-Z]|$)' "$routes"; then
    fail 'a Tahoe card-specific MCS route was added without fresh contract evidence'
fi

printf 'Tahoe MCS GET BSD carrier fence: PASS\n'
