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
state is RUN they fill the carrier only from:

1. `ic->ic_bss->ni_essid` and `ni_esslen` when a current BSS exists and the
   length is within `APPLE80211_MAX_SSID_LEN`.

No producer now uses `strlen` for SSID length.

The earlier bounded `ic_des_essid/ic_des_esslen` fallback was removed in the
2026-07-10 follow-up. The desired SSID is a join target, not an associated-BSS
identity source; the reference-associated cache is seeded through
`IO80211BssManager::setAssocSSID(...)`.

## Runtime Validation

The 2026-07-10 follow-up was loaded on Tahoe 25C56 as
`com.zxystd.AirportItlwm` UUID `5834C5A1-C828-3AFC-85CA-5929EF2C4E90`,
binary SHA-256
`cf622092fb2af3e2eae36a677aa7c899b96678b01161ad00358385d3e3e4f3df`.

After joining `AIAMlab6235`, raw Tahoe and legacy Apple80211 probes both
returned SSID `AIAMlab6235`, BSSID `80:e4:ba:20:ef:f9`, `STATE=4`, and a
populated `CURRENT_NETWORK` carrier on channel `6`. `CWFApple80211` direct
queries also returned the same SSID/BSSID/current network, while the known
public CoreWLAN wrapper symptom remained open.

The accepted 240-second concurrent stress passed with `PING_RC=0` and
`IPERF_RC=0`: ping reported `240 packets transmitted, 240 packets received,
0.0% packet loss`, RTT `0.550/3.798/37.910/5.359 ms`, and iperf3 UDP reported
`572 MBytes` received at `20.0 Mbits/sec` with `0/414365` datagrams lost.
Post-stress `en1` remained active at DHCP `10.77.0.47`, and
`system_profiler SPAirPortDataType` reported `Status: Connected`, channel `6`,
signal/noise `-33 dBm / -92 dBm`, transmit rate `104`, and MCS `13`.

The stress-window fault filter found no panic, firmware crash, NoCTL, missed
beacon, deauth, disassoc, CoreCapture, `driver not available`, `0xe0822403`,
or `IO80211QueueCall` signatures.
