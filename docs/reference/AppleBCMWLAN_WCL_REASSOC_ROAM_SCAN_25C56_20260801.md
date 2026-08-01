# AppleBCMWLAN WCL_REASSOC roam-scan parity (25C56, 2026-08-01)

## Reference artifact

- Tahoe 26.2 / build 25C56 DEXT:
  `/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
- SHA-256:
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`
- Fresh Ghidra project (40-core analysis):
  `/home/dima/Projects/ghidra_output/aiam_applebcm_reassoc_25C56_20260801/applebcm_reassoc_25C56.gpr`
- Fresh decompile:
  `/home/dima/Projects/ghidra_output/aiam_applebcm_reassoc_25C56_20260801/reassoc.txt`

## Recovered behavior

`AppleBCMWLANCore::setWCL_REASSOC` at `0x1001e8702` does not complete a
same-BSS reassociation in the producer. It:

1. rejects a null carrier with `0xe00002bc`;
2. calls `RoamAdapter::setReassocParams((int8_t)data[0x99],
   (data[0x98] & 4) >> 2)`;
3. delegates to `NetAdapter::sendReassocCommand(data)`;
4. restores reassociation parameters only when command submission fails.

`sendReassocCommand` selects V3 for firmware-interface versions above `0x14`,
V1 above `0x10`, and legacy otherwise. All three implementations require an
associated interface. V1/V3 carry up to 50 scan chanspecs and seven candidate
preferences; legacy caps both lists at seven.

The exact public carrier is 0x9c bytes:

| Offset | Meaning |
|---:|---|
| `0x00` | 50 `AppleChannelSpec_t` values (100 bytes) |
| `0x64` | seven packed `{ uint32_t score; uint16_t channel_spec; }` tuples |
| `0x90` | candidate tuple count, capped at seven |
| `0x94` | scan chanspec count, capped at 50 for V1/V3 |
| `0x98` | feature flags; bit 0 changes command flags, bit 2 resets candidate boost |
| `0x99` | signed roam-prune RSSI threshold |

`getChanspecArray` converts every `AppleChannelSpec_t` in the base array to a
firmware chanspec. The command therefore starts a bounded firmware roam scan;
it is not an OTA reassociation request to the current BSS. The asynchronous
start callback posts selector `0xcf` only for a nonzero command result. A zero
start result is progress, not the terminal reassociation success.

## Local parity mapping

Intel firmware exposes no equivalent `WLC_REASSOC` command, so the smallest
real lower owner is the existing HAL background scan shared by IWN/IWM/IWX:

- `setWCL_REASSOC` decodes the exact carrier and calls
  `ieee80211_begin_wcl_reassoc_bgscan`;
- the request is not suppressed by the preference that disables autonomous
  RSSI roaming, because it is explicit airportd intent;
- the fresh scan candidate set is filtered by the supplied chanspec list,
  signed prune threshold, candidate channel scores, same ESS/security match,
  and a different source BSSID;
- a request-admitted roam candidate is not rejected by the BSSID pin of the
  current association; the reference sends the target policy directly to
  firmware rather than reapplying the old host-side association pin;
- no candidate posts asynchronous `0xcf` while retaining the live source;
- open/WPA2 selection uses the existing deferred net80211 BSS switch on all
  three HALs;
- IWN pure SAE retargets its already validated private ESS credential to the
  selected BSSID and starts the existing driver-resident SAE engine;
- terminal success is posted only after the selected target reaches RUN and,
  for RSN, port-valid.

IWM/IWX do not yet have a driver-resident SAE credential engine. Their pure
SAE WCL roam path therefore fails without tearing down the current validated
link; open and WPA2 use the common real switch path.

## Removed false behavior

The previous local implementation returned immediate success for a protected
current BSS and otherwise emitted `REASSOC_REQ` to that same BSS. Runtime logs
showed airportd issuing a real best-connected roam request while this branch
left the radio on the original local AP. The fresh 25C56 decompile disproves
that model, so both the fabricated same-BSS success and the same-BSS OTA path
were removed.

## Verification

- `scripts/test_tahoe_wcl_reassoc_roam_scan_contract.sh`
- `scripts/test_net80211_pae_epoch_contract.sh`
- `scripts/test_tahoe_iwn_wnm_bss_transition_contract.sh`
- Tahoe Debug build against 25C56 BootKC: succeeded; all 1074 undefined
  symbols resolved.
- Disposable-guest candidate UUID:
  `A740A626-6DD2-3DD2-9091-FD0DDD526CA9`; binary SHA-256:
  `721f78130babf89cc5d2b4e0600bb090f07d38fea8a9e5670369e242ef4ce1b8`.
- On-air pure-SAE WCL roam changed the guest from the channel-13 source to
  BSSID `82:c3:97:84:51:ca` on channel 9. Serial evidence recorded, in order,
  `REAL_SCAN_STARTED`, `TARGET_SELECTED`, `DRIVER_RESIDENT_WCL_STARTED`, and
  `TARGET_PORT_VALID`.
- The roamed link retained `172.16.66.120`, passed 5/5 source-bound ICMP
  packets, and returned HTTP 200 through `en1`.
- A subsequent real S3 interval lasted 99 seconds. After QEMU `system_wakeup`,
  the driver reconnected by SAE on channel 13, retained the DHCP address, and
  again passed 5/5 source-bound ICMP packets plus HTTP 200 through `en1`.
  QEMU's separate virtio user-NAT SSH path did not resume; direct Wi-Fi SSH
  and all Wi-Fi data checks remained healthy.
