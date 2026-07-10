# CR-479 accepted identity parent-linkstate owner

Date: 2026-07-10

## Scope

This batch keeps Tahoe `WCL_CONNECT_COMPLETE` on the RSN key-done edge for the
WCL join FSM, and moves the accepted identity publication edge for populated
`BSSID_CHANGED` and status-carrier `SSID_CHANGED` to the accepted Skywalk
parent link-up transition.

It does not add a public CoreWLAN/networksetup fallback, does not synthesize
Dynamic Store values, and does not change the raw Apple80211 SSID, BSSID, or
current-network IOCTL producers.

## Evidence

Tahoe airportd's recovered event path makes the ordering requirement visible:

- `CWXPCInterfaceContext::bssidChanged` refreshes BSSID/current-network side
  state from `wifiClient -> interfaceWithName:` and the driver-backed
  BSSID query before scheduling the associated-network refresh;
- `CWXPCInterfaceContext::ssidChanged` schedules `__associatedNetwork` and
  forwards that result through `setAssociatedNetwork:`;
- `__associatedNetwork` first requires non-null current interface `ssidData`,
  then compares that value with the current/associated network object's
  `ssidData` before returning a CoreWiFi scan result.

Live Tahoe 25C56 FBT on the lab AP `AIAMlab6235` showed the local identity edge
was too early. During a controlled rejoin, BSSID/SSID identity events were
delivered from `IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE` before the framework
current-AP and parent link-up transition:

```text
516645 WCL_LINK_UP entry raw=0
516645 ACCEPT_PROFILE entry srcptr=...
516645 WCL_CONNECT_COMPLETE entry
516645 ACCEPT_BSSID entry srcptr=...
516645 BSSID_HELPER entry data=... srcptr=...
516648 ACCEPT_SSID entry
516648 ACCEPT_PROFILE return bool=1
516649 SET_CURRENT_AP entry ptr=...
516649 SKY_SET_LINK_INTERNAL entry link=2 code=0 ...
516649 SKY_SET_LINK_INTERNAL return ...001
```

The low return bit from `setLinkStateInternal` is the parent bool success
result. Older notes that described this branch as closed were therefore
misclassified.

A follow-up run with `WCL_CONNECT_COMPLETE` removed from the RSN edge showed
the WCL join FSM timing out before link-up: the AP saw the station
associated/authorized, but macOS kept `en1` inactive, raw current-network was
empty, and no link-up `setLinkStateInternal` occurred before the later
link-down transition. Therefore only BSSID/SSID identity events move to the
parent-success link-up edge; WCL connect-complete remains on RSN key-done.

The same runtime still had valid driver-originated current-link data below the
public CoreWLAN surface:

- `Apple80211CopyCurrentNetwork` returned SSID `AIAMlab6235`, BSSID
  `80:e4:ba:20:ef:f9`, and channel 6;
- compact `Apple80211CopyValue` selectors 1, 9, and 0x67 returned SSID,
  BSSID, and current-network data;
- public `CWInterface.ssid/bssid` and `networksetup -getairportnetwork en1`
  still returned nil / `You are not associated with an AirPort network.`;
- Dynamic Store still had top-level empty `SSID` and synthetic
  `BSSID = 02:00:00:00:00:00`, while `CachedScanRecord` contained the real
  BSS.

That public symptom remains a driver-facing model mismatch, not a reason to
add a userspace answer or broad request fallback.

## Local Closure

- `IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE` publishes RSN key-done, WCL link-up,
  and WCL connect-complete for the WCL join FSM.
- The existing exact BSSID/SSID identity carrier sequence is exposed through
  `AirportItlwm::publishTahoeAcceptedJoinIdentityEvents(...)`.
- `AirportItlwmSkywalkInterface::setLinkStateInternal(...)` calls that
  sequence on the parent-success link-up edge after the inherited
  `IO80211InfraInterface::setLinkStateInternal(...)` transition has accepted.
- The duplicate `setLinkStateGated(...)` accepted-identity call is removed.
- The existing last-published BSSID tracker remains the single duplicate
  suppression mechanism; when the active accepted edge publishes BSSID first,
  the passive link-state/setCurrentApAddress BSSID publishers classify the same
  BSS as reason 1 and suppress their duplicate.

## Validation

Validated on the Tahoe 26.2 25C56 guest after AuxKC install and reboot:

- loaded kext UUID `3A9F0DDD-EC3A-3274-A11D-CB91E71A12FA`;
- installed binary SHA-256
  `265b38eed9b0990c3659ef8d2308cc064393868567501137b94c53b5881a738d`;
- host and guest `scripts/test_payload_builders.sh` passed;
- `scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  passed and resolved all 949 undefined symbols against BootKC;
- controlled rejoin to `AIAMlab6235` reached DHCP `10.77.0.47`, and raw
  `Apple80211CopyCurrentNetwork` returned SSID `AIAMlab6235`, BSSID
  `80:e4:ba:20:ef:f9`, and channel 6;
- FBT showed RSN key-done posting WCL link-up and WCL connect-complete first,
  then the parent-success link-up transition calling the identity owner:

```text
24574 WCL_LINK_UP entry raw=0
24574 WCL_CONNECT_COMPLETE entry
24575 SET_CURRENT_AP entry ptr=...
24575 BSSID_HELPER entry data=... srcptr=...
24577 SKY_SET_LINK_INTERNAL entry link=2 code=0 ...
24577 ACCEPT_IDENTITY_METHOD entry srcptr=...
24577 ACCEPT_IDENTITY entry srcptr=...
24577 ACCEPT_BSSID entry srcptr=...
24577 SKY_PUBLISH_BSSID return bool=0
24577 ACCEPT_SSID entry
24577 ACCEPT_SSID return bool=1
24577 SKY_SET_LINK_INTERNAL return ...001
```

The `SKY_PUBLISH_BSSID return bool=0` result is expected in this order because
the passive `setCurrentApAddress(...)` BSSID helper already published the same
BSS and updated the shared duplicate-suppression tracker before the identity
owner ran.

Data-path regression gate:

- 240-second concurrent ping plus `/usr/local/bin/iperf3 -b 20M` completed
  with `PING_RC=0` and `IPERF_RC=0`;
- ping reported `240 packets transmitted, 240 packets received, 0.0% packet
  loss`, RTT `0.571/15.376/146.618/17.904 ms`;
- iperf3 transferred `572 MBytes` at `20.0 Mbits/sec` sender and receiver;
- post-stress `en1` remained active at DHCP `10.77.0.47`, and raw
  current-network still returned the real SSID/BSSID/channel;
- the stress-window fault filter found no panic, CoreCapture, NoCTL, missed
  beacon, deauth, disassoc, `driver not available`, `0xe0822403`, or
  `IO80211QueueCall` signatures.

Public CoreWLAN and `networksetup` remain open after this layer:
`CWInterface.ssid/bssid` and `CWFInterface.ssid/bssid` are still nil,
CoreWLAN channel is 6, and `networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.`
