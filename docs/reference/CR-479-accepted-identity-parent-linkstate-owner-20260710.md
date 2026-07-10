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
- The passive `setCurrentApAddress(...)` event-3 hook is removed because the
  25C56 Apple implementation does not publish BSSID_CHANGED there. The existing
  last-published BSSID tracker remains only on the accepted identity publisher
  and link-down reset path.

## Validation

Validated on the Tahoe 26.2 25C56 guest after AuxKC install and reboot:

- loaded kext UUID `880DAF86-B329-3ED6-B2ED-CBB4826DBA26`;
- installed binary SHA-256
  `e369434157dce4de6c88b2f99c602bd4ba9cbb835de810fa93f4b5d005abadd7`;
- host and guest `scripts/test_payload_builders.sh` passed;
- `scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  passed and resolved all 949 undefined symbols against BootKC;
- controlled rejoin to `AIAMlab6235` reached DHCP `10.77.0.47`, and raw
  `Apple80211CopyCurrentNetwork` returned SSID `AIAMlab6235`, BSSID
  `80:e4:ba:20:ef:f9`, and channel 6;
- FBT showed RSN key-done posting WCL link-up and WCL connect-complete first,
  then `setCurrentApAddress(...)` returning without any BSSID helper call, then
  the parent-success link-up transition calling the identity owner:

```text
87772658715 WCL_LINK_UP entry raw=0
87772674832 WCL_CONNECT_COMPLETE entry
87772833955 SET_CURRENT_AP entry ptr=...
87772839184 SET_CURRENT_AP return
87772958986 SKY_SET_LINK_INTERNAL entry link=2 code=0 up=0 a=0 b=0
87773011102 ACCEPT_IDENTITY_METHOD entry srcptr=...
87773015800 ACCEPT_IDENTITY entry srcptr=...
87773020150 ACCEPT_BSSID entry srcptr=...
87773025484 BSSID_HELPER entry data=... srcptr=...
87776373753 BSSID_HELPER return
87776377773 SKY_PUBLISH_BSSID return lowbit=1
87776381498 ACCEPT_SSID entry
87776384744 ACCEPT_SSID return bool=1
87776387592 ACCEPT_IDENTITY return bool=1
87776390877 ACCEPT_IDENTITY_METHOD return bool=1
87776397289 SKY_SET_LINK_INTERNAL return ...001
```

With the passive `setCurrentApAddress(...)` hook removed, the accepted identity
owner is the first event-3 publisher on the parent-success link-up edge.

Data-path regression gate:

- 240-second concurrent ping plus `/usr/local/bin/iperf3 -b 20M` completed
  with `PING_RC=0` and `IPERF_RC=0`;
- ping reported `240 packets transmitted, 240 packets received, 0.0% packet
  loss`, RTT `0.582/28.069/245.362/38.885 ms`;
- iperf3 transferred `572 MBytes` at `20.0 Mbits/sec` sender and receiver;
- post-stress `en1` remained active at DHCP `10.77.0.47`, and raw
  current-network still returned state 4, SSID `AIAMlab6235`, BSSID
  `80:e4:ba:20:ef:f9`, channel 6, RSSI `-33`, noise `-92`, and IE length
  `163`;
- `Apple80211CopyCurrentNetwork`, compact `CopyValue(1)`, `CopyValue(9)`,
  `CopyValue(0xd)`, and `CopyValue(0x67)` returned the same associated state;
- `CWFApple80211 SSID:`, `BSSID:`, and `currentNetwork:` returned the same
  SSID/BSSID/channel, while public `CWInterface.ssid/bssid` remained nil;
- the post-stress 4-minute fault filter found no panic, CoreCapture, NoCTL,
  missed beacon, deauth, disassoc, `driver not available`, `0xe0822403`, or
  `IO80211QueueCall` signatures. A wider 15-minute filter still captures the
  known rejoin/public-CoreWLAN `0xe0822403` and auto-join
  `driver not available` noise, which remains part of the open public-surface
  mismatch.

Public CoreWLAN and `networksetup` remain open after this layer:
`CWInterface.ssid/bssid` and `CWFInterface.ssid/bssid` are still nil,
CoreWLAN channel is 6, and `networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.`
