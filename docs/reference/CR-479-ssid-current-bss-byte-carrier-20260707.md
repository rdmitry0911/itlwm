# CR-479 SSID current-BSS byte carrier

This batch closes the public STA SSID carrier mismatch in the local
Apple80211 GET path.

## Evidence

- Current runtime on 2026-07-07 shows the link is associated and usable:
  `wdutil info` reports SSID/BSSID/RSSI/channel/IP and `system_profiler
  SPAirPortDataType` reports `Status: Connected`, while
  `networksetup -getairportnetwork en1` still reports not associated.
- BootKC target classification records
  `IO80211Controller::getSSIDData(apple80211_ssid_data*)` at
  `0xffffff8002214f12`:
  `10.7.6.112:~/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/01_inputs/bootkc_classified_targets.tsv`.
- The matching static slice shows `getSSIDData` calling the primary Skywalk
  interface and controller cache owners through the same virtual SSID carrier
  slot:
  `10.7.6.112:~/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/03838_ffffff8002214f12_FUN_ffffff8002214f12.asm.txt`.
- The associated-BSS cache writer recovered in the previous batch,
  `IO80211BssManager::setAssocSSID(unsigned char const*, unsigned long)`,
  is length-based, accepts at most 32 bytes, clears the stored bytes, writes the
  length, and copies only nonzero input.

## Local Divergence

`AirportItlwmSkywalkInterface::getSSID(...)`,
`AirportItlwm::getSSID(...)` in the Tahoe V2 source, and the legacy STA ioctl
copy all returned success but populated `apple80211_ssid_data` from
`ic->ic_des_essid` using `strlen`.

That is not the Apple carrier contract:

- SSID is a byte string with an explicit length, not a C string.
- The connected/current network path must describe the associated BSS. The
  current node already carries `ni_essid`/`ni_esslen`.
- `ic_des_essid` is only a desired/join target cache and can be empty or stale
  even after the framework current-BSS path has a valid associated network.

## Closure

The three STA GET SSID producers now keep the existing Apple bootstrap
contract, success with a zeroed carrier before association, but when the local
state is RUN they fill the carrier from:

1. `ic->ic_bss->ni_essid` and `ni_esslen` when a current BSS exists and the
   length is within `APPLE80211_MAX_SSID_LEN`;
2. `ic->ic_des_essid` and `ic_des_esslen` only as a bounded fallback.

No producer now uses `strlen` for SSID length.
