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

## Reference evidence

Primary reference note:

- `docs/reference/AppleBCMWLAN_APSTA_event_station_table_2026_04_27.md`

Recovered body facts used here:

- association/reassociation events `8/10` with success status/reason post STA
  message id `0x0c` with payload size `0x114`
- removal events `5/6/11/12` clear the entry and post STA message id `0x0d`
  with payload size `0x0c`
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
  sequences, applies Instant Hotspot AIHS/sharing flags from byte `+0x09` when
  subtype `0x0b` is present, and sets the Apple-station flag when any recovered
  Apple IE is present.
- association/reassociation now update `state+0x80/+0x84`, build the packed
  `0x114` association message, copy RSNXE into output `+0x10`, and publish
  message id `0x0c` through the existing IO80211 `postMessage` boundary.
- removal now clears/recounts the station entry, builds the packed `0x0c`
  removal message with the post-removal count, and publishes message id `0x0d`.

## Non-claims

This does not enable role-7 AP firmware mode, does not remove the
`IEEE80211_APSTA_STATION_EVENT_OPT_OUT` gate, and does not synthesize unknown
`WLC_E_AUTH_IND` payload contents. AP backend station commands remain governed
by the existing `isApRunning()` / HAL gates.
