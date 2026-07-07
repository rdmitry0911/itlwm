# CR-479 APSTA station/key query boundary

Date: 2026-07-07

## Scope

The APSTA station/key body scaffold already captured fixed AppleBCMWLAN
selectors, offsets, return codes, station-table bounds, and command-buffer
carriers. The remaining local mismatch was that several public bodies still
stopped at `kIOReturnUnsupported` after their reference-compatible front gates.

This batch routes those public bodies to the host-owned AP/GO HAL boundary:

- `getSTA_IE_LIST`
- `getSTA_STATS`
- `getKEY_RSC`

It also removes the local `setCIPHER_KEY` NULL guard that the recovered
AppleBCMWLAN body does not have after AP-up state passes.

## Reference evidence

Primary reference note:

- `docs/reference/AppleBCMWLAN_APSTA_station_key_bodies_2026_04_27.md`

Recovered body facts used here:

- `setCIPHER_KEY` returns `6` when AP is down, reads cipher type at input
  `+0x08`, returns success for cipher `0` and unsupported nonzero ciphers, and
  has no NULL guard after AP-up state passes.
- `getSTA_IE_LIST` rejects NULL with raw `0x16`, scans the APSTA station table,
  returns `2` when the station is missing, uses IOVAR `wpaie`, and on success
  sets output `+0x0c` from output byte `+0x11` plus `2`.
- `getSTA_STATS` rejects AP-down with `0x39`, NULL with raw `0x16`, uses IOVAR
  `sta_info`, and maps the four recovered RX fields into output
  `+0x0c/+0x10/+0x14/+0x18`.
- `getKEY_RSC` has no NULL guard, reads key index at input `+0x0e`, uses
  virtual IOCTL get selector `0xb7`, and writes the returned 8-byte RSC plus
  length `8` at output `+0x54/+0x50`.

## Local closure

`AirportItlwmAPSTAOwner` now keeps the recovered public-method gates and uses
`ItlHalService` for the backend-owned command tail:

- `getStaIEList` copies the six-byte station-entry prefix into output `+0x10`,
  derives the requested length by subtracting the `wpaie` command-name
  overhead, calls `getAPStationIE`, then applies the recovered output-length
  rule.
- `getStaStats` calls `getAPStationStats` and only publishes the valid bit and
  four output fields after a successful backend query.
- `getKeyRsc` calls `getAPKeyRSC` with the recovered key-index source and only
  writes length `8` after backend success.
- The default HAL methods remain fail-closed with `kIOReturnUnsupported`, so
  the code no longer fabricates station IE, station stats, or key RSC data when
  the lower backend has not implemented the corresponding AP command path.

## Non-claims

This is the runtime boundary for the public APSTA station/key bodies, not the
final Intel firmware AP backend. It does not force AP mode on, does not
synthesize AP station data, and does not alter primary STA association or key
programming paths.
