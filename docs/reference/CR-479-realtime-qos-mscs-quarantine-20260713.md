# CR-479 — REALTIME_QOS_MSCS false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setREALTIME_QOS_MSCS` slot. The former local
implementation accepted a non-null `apple80211_state_data`, copied its state
dword at `+0x4` into a dead local cache, and returned success. There is no
local QoS/MSCS owner, firmware request path, or completion-event consumer for
that pseudo-state.

The local null guard remains a defensive `0x16` return. A non-null request now
returns `kIOReturnUnsupported` before mutation, and only the dead cache and
its two reset sites are removed.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setREALTIME_QOS_MSCS` at `0x1000189ac` loads
  the core and dispatches through virtual `+0x7b0`.
- `AppleBCMWLANCore::setREALTIME_QOS_MSCS` at `0x1001e81a4` obtains the
  current BSS, checks feature bit `0x5f`, checks configuration byte `+0x7579`,
  and calls the current-BSS virtual capability predicate at `+0x290`.
- Only after those gates does the reference examine the public carrier: null
  reaches raw `0x16`; a non-null state dword at `+0x4` sets core byte
  `+0x757b` and calls `sendQoSMgmtMSCSReq` at `0x10013d028`.
- The sender rechecks the QoS/BSS conditions and calls
  `confiQoSMgmtMSCS` at `0x10013cda6`. The terminal builds the
  `WL_QOS_CMD_RAV_MSCS` request and calls `qosSetIOVar` with its recovered
  `0x10`-byte request carrier.
- `handleMSCSEvent` at `0x1001de8dc` handles the matching firmware event and
  updates QoS/MSCS state.

This proves real owner, firmware, and event work; it does not justify lifting
that private path from a cache write. In particular, the reference's null
branch is reachable only after its feature/current-BSS gates, so this change
does not claim Apple null-input status parity. It is also not Apple valid-input return-code parity:
the core and downstream work have gated and asynchronous behavior outside the
local cache stub.

## Local boundary and non-claims

Scoped local source inspection found `cachedRealTimeQosMscs` only in its
declaration, two initialization resets, and this setter. It found no matching
`sendQoSMgmtMSCSReq`, `confiQoSMgmtMSCS`, `qosSetIOVar`, or `handleMSCSEvent`
implementation. The change therefore does not introduce a guessed carrier,
private IOCTL, direct firmware request, or synthetic completion event.

No direct runtime invocation of the setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a QoS/MSCS regression signal. This note makes no complete public carrier allocation claim.
