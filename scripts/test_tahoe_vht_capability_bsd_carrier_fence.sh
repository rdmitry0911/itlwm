#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-621-tahoe-bsd-vht-capability-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-vht-capability-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe VHT_CAPABILITY BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_VHT_CAPABILITY 214' "$header" || fail 'selector no longer resolves to 214'
grep -Fq 'static_assert(sizeof(apple80211_vht_capability) == 0x12,' "$source" || fail 'compact local VHT IE prefix is no longer pinned'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'canonical_get_ioctl=0xc02869c9' "$artifact" || fail 'canonical GET ioctl evidence is missing'
grep -Fq 'canonical_set_ioctl=0x802869c8' "$artifact" || fail 'canonical SET ioctl evidence is missing'
grep -Fq 'standard_alias_private_table_identity=not_proven' "$artifact" || fail 'alias caveat is missing'
grep -Fq 'standard_alias_fence_action=not_fenced' "$artifact" || fail 'alias fence scope is missing'
grep -Fq 'selector=0xd6' "$artifact" || fail 'family VHT selector evidence is missing'
grep -Fq 'get_handler_entry_raw_qword=0x00400000020d8ac2' "$artifact" || fail 'raw GET table pointer is missing'
grep -Fq 'get_handler_entry_decoded_target_vmaddr=0xffffff80021d8ac2' "$artifact" || fail 'raw GET table target is missing'
grep -Fq 'set_handler_entry_raw_qword=0x00800000020e55fd' "$artifact" || fail 'raw SET table pointer is missing'
grep -Fq 'set_handler_entry_decoded_target_vmaddr=0xffffff80021e55fd' "$artifact" || fail 'raw SET table target is missing'
grep -Fq 'get_public_req_len=0x14' "$artifact" || fail 'GET 0x14 public ABI evidence is missing'
grep -Fq 'set_public_req_len=0x14' "$artifact" || fail 'SET 0x14 public ABI evidence is missing'
grep -Fq 'get_owner_metaclass=IO80211AWDLPeerManager::gMetaClass' "$artifact" || fail 'GET owner gate is missing'
grep -Fq 'get_copyout_start_offset=0x04' "$artifact" || fail 'GET body offset is missing'
grep -Fq 'get_copyout_length=0x0e' "$artifact" || fail 'GET body length is missing'
grep -Fq 'get_preserves_tail_12_13=true' "$artifact" || fail 'GET tail-preservation evidence is missing'
grep -Fq 'set_ignores_tail_12_13=true' "$artifact" || fail 'SET tail-ignorance evidence is missing'
grep -Fq 'peer_setter_commits_on_status=0' "$artifact" || fail 'SET commit gate is missing'
grep -Fq 'internal_set_entry_decoded_target_vmaddr=0xffffff80021c4eb9' "$artifact" || fail 'internal SET public route is missing'
grep -Fq 'public_set_tail_slot=0x1170' "$artifact" || fail 'local SET vtable route is missing'
grep -Fq 'super_process_bsd_label=IO80211InfraInterface::processBSDCommand' "$artifact" || fail 'system super route is missing'
grep -Fq 'external_router_label=apple80211RouteExternal' "$artifact" || fail 'system external router is missing'
grep -Fq 'external_router_raw_len=0x2d1' "$artifact" || fail 'system external router length is missing'
grep -Fq 'external_router_raw_sha256=a21f852cd45428122be6a8012e5e85f251f5f374d4f3e5d96808826476a09315' "$artifact" || fail 'system external router bytes are not pinned'
grep -Fq 'card_specific_route=false' "$artifact" || fail 'absence of a card route is missing'
grep -Fq 'standard `c030/8030` ioctl aliases are not fenced' "$evidence" || fail 'alias caveat is missing from evidence'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_VHT_CAPABILITY' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
normalize_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'UInt normalizedCmd =' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$normalize_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD fence runs after the local dispatcher'
[ "$fence_line" -lt "$normalize_line" ] || fail 'standard aliases cannot reach local normalization'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD fence is not Tahoe-only'
printf '%s\n' "$bsd_bridge" | grep -Fq 'isApple80211GetIoctl(cmd) ? SIOCGA80211 : SIOCSA80211;' || fail 'standard ioctl normalization disappeared'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 2)),$((fence_line + 20))p")
printf '%s\n' "$fence" | grep -Fq '(cmd == kApple80211LegacyGetIoctl ||' || fail 'BSD VHT fence does not start with canonical GET'
printf '%s\n' "$fence" | grep -Fq 'cmd == kApple80211LegacySetIoctl)' || fail 'BSD VHT fence does not include canonical SET'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD VHT fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Fq 'isApple80211GetIoctl(cmd) || isApple80211SetIoctl(cmd)'; then
    fail 'BSD VHT fence extends to unproven standard aliases'
fi
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD VHT fence inspects the nested carrier'
fi

dispatcher=$(sed -n '/case APPLE80211_IOC_VHT_CAPABILITY:/,/case APPLE80211_IOC_AWDL_STRATEGY:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return getVHT_CAPABILITY((apple80211_vht_capability *)req->req_data);' || fail 'local GET virtual route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'return setVHT_CAPABILITY((apple80211_vht_capability *)req->req_data);' || fail 'local SET virtual route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'return kIOReturnUnsupported;' || fail 'unexpected VHT fallback change'

get_producer=$(sed -n '/getVHT_CAPABILITY(struct apple80211_vht_capability \*data)/,/^}/p' "$source")
printf '%s\n' "$get_producer" | grep -Fq 'memset(data, 0, sizeof(*data));' || fail 'local internal VHT producer changed unexpectedly'
printf '%s\n' "$get_producer" | grep -Fq 'data->len = sizeof(struct ieee80211_ie_vhtcap) - 2;' || fail 'local VHT IE prefix disappeared'
set_producer=$(sed -n '/setVHT_CAPABILITY(apple80211_vht_capability \*data)/,/^}/p' "$source")
printf '%s\n' "$set_producer" | grep -Fq 'memcpy(&cachedVhtCapability, data, sizeof(cachedVhtCapability));' || fail 'local internal VHT setter changed unexpectedly'
if grep -Eq 'APPLE80211_IOC_VHT_CAPABILITY' "$routes"; then
    fail 'a Tahoe card-specific VHT route was added without fresh contract evidence'
fi

printf 'Tahoe VHT_CAPABILITY BSD carrier fence: PASS\n'
