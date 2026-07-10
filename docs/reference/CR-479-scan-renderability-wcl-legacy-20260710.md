# CR-479 Tahoe scan renderability WCL/legacy parity

Date: 2026-07-10

## Scope

This batch aligns the scan-result publication layer. It covers the WCL
`APPLE80211_M_WCL_SCAN_RESULT` bulletin producer, the legacy
`APPLE80211_IOC_SCAN_RESULT` cursor, and the WCL BGSCAN cache-result getter.

It does not synthesize SSID/BSSID values, does not change the public CoreWLAN
privacy/read gate, and does not broaden the rejected public fallback gate.

## Reference Evidence

Tahoe AppleBCMWLAN evidence:

- `WCLNetManager::updateBss(...)` rejects `BeaconMetaData.bssid` equal to
  `00:00:00:00:00:00` before constructing a `WCLBSSBeacon`;
- `AppleBCMWLANScanAdapter` builds the WCL scan `BeaconMetaData` header with
  bit 1 set and bit 2 clear. Bit 2 is not an SSID-present marker for this
  producer;
- the WCL scan carrier remains a `0x44` `BeaconMetaData` header followed by
  raw tagged beacon IEs.

## Local Closure

The local scan layer now:

- rejects zero-BSSID nodes before WCL scan-result publication;
- uses the same zero-BSSID renderability rule for the legacy `SCAN_RESULT`
  iterator and the WCL BGSCAN cache producer, so channel-only OpenBSD scan
  nodes are not exposed as renderable BSS entries;
- publishes WCL scan `BeaconMetaData.flags = 0x2`, matching the Apple scan
  adapter builder, instead of the old local `0x6`;
- records the contract in `TahoeScanContracts.hpp` and in the payload parity
  self-test.

## Runtime Validation

Validated on the Tahoe guest with AirportItlwm built from source-id
`62540bf3038b` and installed as:

- loaded kext UUID: `F9F73C84-511D-376D-B216-4E574CB679B1`;
- installed binary SHA-256:
  `ae5ec089e1d5766b907a8fe026a8ec3ea5a80a1433450b4ae66d0e3296fe74f3`;
- build: `AirportItlwm-Tahoe` Debug/Tahoe succeeded, with all 949
  undefined symbols resolved against BootKC;
- local static validation: `git diff --check` and
  `./scripts/test_payload_builders.sh` pass.

Runtime probes after reboot and re-association:

- `CWFApple80211` raw current network returns the lab SSID
  `ITLWM-Lab-3c95c7`, BSSID `80:e4:ba:20:ef:f9`, channel 6, RSSI -33,
  and real WPA2 RSN metadata;
- `system_profiler SPAirPortDataType` reports `Status: Connected` with
  channel/security/signal details for the current network;
- public CoreWLAN scan results now retain the lab BSS as a renderable
  candidate (`ch=6`, `rssi=-33`) instead of collapsing it into a
  channel-only unusable entry.

Stress validation:

- `ping -i 1 -c 240 10.77.0.1` while iperf3 was running:
  240 transmitted, 240 received, 0.0% packet loss,
  `min/avg/max/stddev = 0.598/14.929/119.978/18.671 ms`;
- `/usr/local/bin/iperf3 -c 10.77.0.1 -t 240 -b 20M`:
  572 MBytes transferred at 20.0 Mbit/sec in both sender and receiver
  summaries;
- the interface remained active with DHCP address `10.77.0.157`.

Known open surface after this batch:

- `networksetup -getairportnetwork en1` still prints
  `You are not associated with an AirPort network.`;
- public `CWInterface.ssid` / `CWInterface.bssid` and public
  `CWNetwork.ssid` / `CWNetwork.bssid` remain nil/redacted even though the
  raw CWFApple80211 current-network and scan-record data are populated.

## Non-Claims

This closes scan-result renderability parity only. The remaining public
`networksetup -getairportnetwork en1` / `CWInterface.ssid` symptom is still
open unless the final runtime run proves otherwise.
