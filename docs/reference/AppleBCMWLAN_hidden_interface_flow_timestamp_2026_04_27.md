# AppleBCMWLAN hidden interface flow/timestamp/virtual-interface contracts

Date: 2026-04-27

Scope: Tahoe hidden interface object stored in AppleBCMWLANCore private state at
`+0x1510`. This note records structural contracts used for local owner-layer
recovery after controller queue/multicast/capacity alignment.

## Hidden object offset

Several AppleBCMWLANCore methods load core-private state from `this+0x128` and
then use the object pointer stored at core-private `+0x1510`. Earlier local
recovery already modeled system-facing registry/property uses of this object
with the bound Skywalk interface. This note extends the documented surface to
flow queues, packet timestamping, log pipes, and virtual-interface delegation.

## Flow queue delegation

Functions:

- `AppleBCMWLANCore::flowIdSupported()`
- Address: `0xffffff80015b7a98`
- `AppleBCMWLANCore::requestFlowQueue(FlowIdMetadata const*)`
- Address: `0xffffff80015b7b10`
- `AppleBCMWLANCore::releaseFlowQueue(IO80211FlowQueue*)`
- Address: `0xffffff80015b7ab4`

Observed behavior:

- `flowIdSupported()` delegates directly to hidden `+0x1510` vtable slot
  `+0xa68`.
- `requestFlowQueue(...)` first delegates to hidden slot `+0xa68`.
- If flow IDs are not supported, Apple falls back to the base controller
  request path at vtable slot `+0xd60`.
- If flow IDs are supported but commands are being rejected, Apple returns
  `NULL`.
- Otherwise Apple calls hidden slot `+0xa70` with the original metadata pointer,
  `metadata+0x06`, dword `metadata+0x0c`, and byte `metadata+0x10`.
- `releaseFlowQueue(...)` delegates to hidden slot `+0xa78` when flow IDs are
  supported; otherwise it falls back to the base controller release path at
  vtable slot `+0xd68`.

Local alignment in this batch:

- Adds recovered hidden-interface constants and owner witnesses.
- Keeps `flowIdSupported` false by default until a real local flow queue owner
  is recovered.
- Does not synthesize flow queues.
- Removes the previous local debug-log side effect from `releaseFlowQueue`;
  the local path remains a no-op witness while flow IDs are disabled.

## Packet timestamping delegation

Functions:

- `AppleBCMWLANCore::enablePacketTimestamping()`
- Address: `0xffffff800162da9c`
- `AppleBCMWLANCore::enablePacketTimestampingGated()`
- Address: `0xffffff800162db4e`
- `AppleBCMWLANCore::disablePacketTimestamping()`
- Address: `0xffffff800162db6a`
- `AppleBCMWLANCore::disablePacketTimestampingGated()`
- Address: `0xffffff800162dc1c`

Observed behavior:

- The non-gated enable path calls the base/controller timestamp enable slot
  `+0xd90`.
- It then runs a command-gate action targeting
  `enablePacketTimestampingGated`.
- The gated body delegates to hidden `+0x1510` slot `+0xaa8`.
- The non-gated disable path calls base/controller slot `+0xd98`, then runs
  `disablePacketTimestampingGated`.
- The gated disable body delegates to hidden slot `+0xab0`.

Local alignment in this batch:

- Records the slot contracts and owner witnesses only.
- Does not call hidden timestamp slots locally because the concrete local
  timestamp owner backend is not recovered yet.

## Log pipes

Function:

- `AppleBCMWLANCore::getLogPipes(CCPipe**, CCPipe**, CCPipe**)`
- Address: `0xffffff8001634230`

Observed behavior:

- Loads hidden `+0x1510`.
- Reads pipe owner at hidden object `+0x88`.
- Writes `logPipe` from pipe-owner `+0x220`.
- Writes `eventPipe` from pipe-owner `+0x218`.
- Writes `snapshotsPipe` from pipe-owner `+0x230`.

Local alignment in this batch:

- Records the pipe owner and pipe offsets for later exact mapping.
- Existing local `getLogPipes` still returns the locally constructed
  `driverLogPipe`, `driverDataPathPipe`, and `driverSnapshotsPipe` objects.

## Virtual-interface delegation

Functions:

- `AppleBCMWLANCore::createVirtualInterface(ether_addr*, unsigned int)`
- Address: `0xffffff80015fc952`
- `AppleBCMWLANCore::enableVirtualInterface(IO80211VirtualInterface*)`
- Address: `0xffffff80015fc964`
- `AppleBCMWLANCore::disableVirtualInterface(IO80211VirtualInterface*)`
- Address: `0xffffff80015fcb28`

Observed behavior:

- Creation delegates through base/controller slot `+0xe10`.
- Enable delegates through base/controller slot `+0xd40`.
- Disable delegates through base/controller slot `+0xd48`.
- Null virtual interface enable/disable returns `0xe00002bc`.
- When proximity/APSTA owner `+0x2c28` is present and the interface role is
  `6`, Apple calls the proximity bring-up/down path and logs around wake flag
  `0x10000`.

Local alignment in this batch:

- Records the recovered slots, role, status, and owner offset.
- Does not enable APSTA/proximity virtual-interface runtime behavior in this
  batch.

## Non-claims

- This batch does not claim final primary STA association or data success.
- This batch does not enable a local flow queue backend.
- This batch does not enable packet timestamping through a hidden backend.
- This batch does not enable APSTA/proximity virtual-interface runtime.
- This batch does not add retry, poll, fallback, forced state, or synthetic
  flow/timestamp objects.
