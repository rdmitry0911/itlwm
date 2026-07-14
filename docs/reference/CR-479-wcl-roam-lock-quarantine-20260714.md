# CR-479 — WCL_SET_ROAM_LOCK false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only public
`AirportItlwmSkywalkInterface::setWCL_SET_ROAM_LOCK`. The former local handler
accepted a non-null carrier, read byte 0 into two unread cache flags, and
returned success. Scoped source found the pseudo one-byte type, cache flags,
two reset pairs, and setter, but no local RoamAdapter, `roam_off` command
transport, callback, completion/status consumer, or adaptive-roam owner.

The local raw null guard remains `kApple80211ErrInvalidArgumentRaw` (`0x16`).
A non-null request now returns `kIOReturnUnsupported` before reading the
carrier. The dead pseudo-layout/cache/flag/reset lines are removed. Reassoc,
roam profile, user-cache, scan, key, link, WCL event, and generic
adaptive-roaming platform-property paths remain unchanged.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_SET_ROAM_LOCK` at `0x100018adc`
  dispatches through virtual `+0x4b0` to Core `0x10011ed1e`.
- A null carrier takes cold path `0x1002082a6`, which writes raw `0x16`.
- A non-null carrier selects RoamAdapter at `+0x15c0`, reads byte 0 as a
  boolean, and tail-jumps to `setRoamLock` at `0x10001e4e0`.
- `setRoamLock` serializes that input as a 4-byte boolean payload for
  `"roam_off"`, calls `sendIOVarSet` `0x10017b900`, records and returns the
  enqueue/transport result, and installs
  `handleRoamOffAsyncCallBack` `0x10001e59e` for asynchronous failure handling.

Those branches are a RoamAdapter state and transport lifecycle. They do not
justify a local byte-cache-and-success substitute, and the byte-0 read alone
does not establish a complete public carrier allocation.

## Local boundary and non-claims

No guessed `roam_off` IOVAR, direct firmware request, private IOCTL, synthetic
callback/completion, scan/reassociation/key mutation, or status suppression is
introduced. The generic Intel coexistence and roaming paths are not a proven
substitute for this Apple RoamAdapter owner.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a roam-lock regression signal.
