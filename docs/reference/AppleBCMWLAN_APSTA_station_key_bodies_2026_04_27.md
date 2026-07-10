# AppleBCMWLAN APSTA Station And Key Body Reference

Source binary: `/tmp/AppleBCMWLANCoreMac`.

This note records APSTA public method bodies that use station-table state,
command buffers, virtual IOCTLs, or IOVARs. It is a structural reference note
only; it does not enable local APSTA runtime.

## getSTATION_LIST

Function: `AppleBCMWLANIO80211APSTAInterface::getSTATION_LIST(...)`
Address: `0xffffff8001687e40`.

- NULL input returns raw `0x16`.
- AP-down state `state+0x26c == 0` returns `0x39`.
- allocates a `0x100` byte maclist buffer.
- allocation failure returns `0xe00002bd`.
- writes initial dword `0x2a` at buffer `+0x00`.
- selector is virtual IOCTL get `0x9f`.
- TX payload size is `0x100`.
- async-capable path installs completion owner/function/cookie and sends
  `sendVirtualIOCtlGet`.
- async submit failure frees the buffer and returns `0xe00002d8`.
- sync path uses RX payload range qword `0x0000010001000100` and
  `runVirtualIOCtlGet`.
- sync command failure returns the command result after freeing the buffer.
- sync success calls `convertBCMAssocListToAppleAssocList(buffer, output)`,
  frees the buffer, and returns `0`.

## setCIPHER_KEY

Function: `AppleBCMWLANIO80211APSTAInterface::setCIPHER_KEY(...)`
Address: `0xffffff800168f2b6`.

- AP-down state `state+0x26c == 0` returns `6`.
- no null guard is present after AP-up state passes.
- cipher type is read from input `+0x08`.
- cipher type `0` returns `0`.
- only cipher types `3` and `5` enter the key programming path.
- unsupported nonzero cipher types log and return `0`.
- optional key dump uses resource `state+0x210` and flag `0x800`.
- stack `wl_wsec_key` carrier size is `0xa4`.
- `mapAppleKeyToBcomKey(wl_wsec_key&, input)` result is returned on mapping
  failure.
- virtual IOCTL set selector is `0x2d`.
- TX payload size is `0xa4`.
- command result is returned on command failure; success returns `0`.

## getSTA_IE_LIST

Function: `AppleBCMWLANIO80211APSTAInterface::getSTA_IE_LIST(...)`
Address: `0xffffff800168f59c`.

- NULL input returns raw `0x16`.
- station MAC is read from input `+0x04`.
- station table search starts at `state+0xb9`, uses 6-byte MAC compares,
  advances by stride `0x30`, and stops before `state+0x1a9`.
- not found returns `2`.
- when found, the first four bytes of the station entry are copied to output
  `+0x10` and the next two bytes to output `+0x14`.
- IOVAR name is `wpaie`.
- requested length is derived from input/output `+0x0c` minus the command-name
  overhead visible in the reference body.
- command path uses `runVirtualIOVarGet`.
- command result is returned on failure.
- on success, output `+0x0c` is set to byte at output `+0x11` plus `2`.

## getSTA_STATS

Function: `AppleBCMWLANIO80211APSTAInterface::getSTA_STATS(...)`
Address: `0xffffff800168f808`.

- AP-down state `state+0x26c == 0` returns `0x39`.
- NULL input returns raw `0x16`.
- station-count/firmware-index source is `state+0x218 -> +0x128 -> +0x30c`.
- allocation size is `0x108` below threshold `7`.
- allocation size is `0x118` at threshold `7`.
- allocation size adds `0x10` when the source value is at least `0x0f`.
- allocation failure returns `0x0c`.
- TX payload is 6 bytes from input `+0x04`.
- IOVAR name is `sta_info`.
- command result is returned on failure.
- on success:
  - output `+0x00` is set to `1`
  - RX `+0x58` is copied to output `+0x0c`
  - RX `+0x68` is copied to output `+0x10`
  - RX `+0x54` is copied to output `+0x14`
  - RX `+0x60` is copied to output `+0x18`
- allocated buffer is always freed before return after allocation succeeds.

## getKEY_RSC

Function: `AppleBCMWLANIO80211APSTAInterface::getKEY_RSC(...)`
Address: `0xffffff800168f9e6`.

- no null guard is present.
- key index is read as a 16-bit value from input `+0x0e`.
- TX payload size is `0x08`.
- RX payload range qword is `0x0000000800040008`.
- virtual IOCTL get selector is `0xb7`.
- command result is returned on failure.
- on success, the 8-byte RX value is copied to output `+0x54` and output
  length `+0x50` is set to `8`.

## Local Scope

The local scaffold records selectors, payload sizes, allocation sizes, offsets,
return values, and carrier witnesses only. It is not an AP/GO firmware runtime
enablement note.

As of 2026-07-10, the local controller/owner wrappers preserve the recovered
caller-visible null ordering for the already-routed station/key getters:

- `getSTATION_LIST(...)` checks NULL input before AP-down/owner state and
  returns raw `0x16`;
- `getSTA_IE_LIST(...)` checks NULL input before station-table/owner lookup and
  returns raw `0x16`.
