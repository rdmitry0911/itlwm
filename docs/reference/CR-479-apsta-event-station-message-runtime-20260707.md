# CR-479 APSTA event station-message runtime boundary

Date: 2026-07-07

## Scope

The APSTA event/station-table scaffold already captured the five-entry station
table, event ids, station-message ids, payload sizes, Apple IE scan operands,
and RSNXE parser operands. The remaining local mismatch was in the executable
net80211-to-APSTA producer: association/removal events updated the local station
table, but did not build the recovered `STA_ARRIVE` / `STA_LEAVE` message
carriers, did not preserve association IE TLVs for RSNXE / Apple IE parsing,
and did not update the `state+0x80/+0x84` event MAC shadow.

This batch closes that runtime boundary without enabling AP firmware mode.
Tahoe 25C56 follow-up on 2026-07-10 also closes the event-admission,
full-table, removal-shadow, and LPHS action-frame consumers against the current
BootKC body at `0xffffff800166f880`.

## Reference evidence

Primary reference note:

- `docs/reference/AppleBCMWLAN_APSTA_event_station_table_2026_04_27.md`

Recovered body facts used here:

- association/reassociation events `8/10` with success status/reason post STA
  message id `0x0c` with payload size `0x114`
- removal events `5/6/11/12` clear the entry and post STA message id `0x0d`
  with payload size `0x0c`
- auth-ind event `4` with success status and auth type `3` posts STA message
  id `0x98` with payload size `0x6c`; the carrier type dword is `5`, the
  status/reason dword is written at `+0x08`, the MAC at `+0x0c/+0x10`, chunk
  type `1` data at `+0x18`, and 16-byte chunk type `2` data at `+0x54`
- association message fields are MAC `+0x00/+0x04`, associated count `+0x08`,
  association flags `+0x0c`, and RSNXE output area `+0x10`
- association flags combine station-table AIHS/sharing fields and Apple IE
  presence
- RSNXE parser scans IE TLVs for element id `0xf4` and copies the full element
  including id/length

Apple OUI byte evidence was read from the BootKC data symbols on the decompile
host `10.7.6.112`, file
`~/Projects/ghidra_additional/BootKernelExtensions.kc`:

- `kAPPLE_IE_OUI` at `0xffffff80016f3ca0`: `00 17 f2`
- `kAPPLE_IE_BS_OUI` at `0xffffff80016f3d28`: `00 03 93`
- `kAPPLE_IE_DEVICE_INFO_OUI` at `0xffffff80016f3d2b`: `00 a0 40`

## Local closure

- `ieee80211_recv_assoc_req` now preserves the AP association IE TLV list in
  `ni_rsnie_tlv` after fixed fields and, for reassociation, after the current
  AP address.
- `AirportItlwmAPSTANet80211Event` passes the saved TLV list to
  `AirportItlwmAPSTAOwner`.
- `AirportItlwmAPSTAOwner` recognizes the three recovered Apple OUI byte
  sequences for association admission, applies Instant Hotspot AIHS/sharing
  flags only for primary Apple OUI `00:17:f2` with subtype `0x0b`, and sets the
  Apple-station flag when any recovered Apple IE is present.
- association/reassociation now update `state+0x80/+0x84`, build the packed
  `0x114` association message, copy RSNXE into output `+0x10`, and publish
  message id `0x0c` through the existing IO80211 `postMessage` boundary.
- association/reassociation require zero status and reason; hidden mode also
  requires a recognized Apple IE. A full five-entry table still publishes the
  message with the current count, and message Apple bit `0x04` follows the
  current event rather than a retained table flag.
- removal now updates `state+0x80/+0x84`, clears/decrements the station entry,
  builds the packed `0x0c` removal message with the post-removal count, and
  publishes message id `0x0d`.
- auth-ind now builds the recovered packed `0x6c` carrier for the reference
  success/auth-type gate and publishes message id `0x98`; malformed optional
  auth chunks simply leave the zeroed carrier tail unchanged, matching the
  recovered zero-initialized message shape.
- action event `0x4b` parses the recovered `0x0100` and `0x0200` payload forms,
  updates LPHS sleep state only for category `0x7f` actions `1/2`, and requests
  SoftAP power state `3`, reason `0x0b` only after the all-station and exact
  concurrency gates pass.

## Tahoe regression validation

- host `git diff --check`, payload builders, reproducibility smoke, payload
  parity report, and contract inventory validation passed; guest payload
  builders passed after synchronizing the complete current source tree;
- the clean Tahoe build resolved all 949 undefined symbols against BootKC and
  loaded as UUID `F73FEC4C-CEDD-365B-9445-C38A92FB1D4F`, signed binary
  SHA-256 `f75b7eb3e45cf91c7a4b44601198020570980558c1e1ec80959be7ae4c43a937`,
  CDHash `537a89a4c8ba2cfbc060d9e81a821180e3568a04`;
- controlled join to `AIAMlab6235/aa00bb0900` reached DHCP `10.77.0.47`;
  `system_profiler` reported Connected and IORegistry retained the real
  SSID/BSSID/channel/RSN state;
- concurrent 240-second ping and `/usr/local/bin/iperf3 -b 20M` passed with
  `240/240`, 0% loss, RTT `0.568/16.944/203.278/20.648 ms`, and `572 MBytes`
  at `20.0 Mbits/sec` in both directions;
- hostapd/`iw` kept the station associated, authenticated, and authorized with
  `tx failed: 0`; stress-window serial and kernel/airportd fault filters were
  empty.

The APSTA event semantics are covered by current-BootKC decompilation and
direct contract tests. The Tahoe runtime pass is a primary-STA regression
test, not a claim that dormant Intel AP/GO firmware mode emitted live station
or action-frame events.

## Non-claims

This does not enable role-7 AP firmware mode, does not remove the
`IEEE80211_APSTA_STATION_EVENT_OPT_OUT` gate, and does not fabricate auth or
action-frame payloads. Explicit userspace STA authorize/disassociate commands
remain governed by the existing `isApRunning()` / HAL gates; incoming station
events are not echoed back to the HAL because the reference event consumer has
no such reverse command.
