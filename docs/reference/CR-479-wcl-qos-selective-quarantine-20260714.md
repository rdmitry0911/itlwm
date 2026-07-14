# CR-479 — WCL QoS selective false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setWCL_QOS_PARAMS`. It does not blanket-reject
the QoS carrier: the local RTS and powersave actions remain present. It removes
only the cache-and-success acknowledgement of QoS flag bits that require
missing Apple owners or transports.

The direct null guard remains `kIOReturnBadArgumentTahoe` (`0xe00002bc`). For
a non-null carrier, the selector reads only the flag byte `+0x17`; if any
missing-owner bit in `0x6d` is set, it returns `kIOReturnUnsupported` before
RTS or powersave mutation. The direct carrier cache and both reset groups are
removed. A carrier with only unknown bit `0x80` remains a successful no-op,
matching Tahoe's unrecognized-flag behavior.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra wrapper `0x100018dbc` forwards to Core
  `setWCL_QOS_PARAMS` `0x1001e867a`; null returns `0xe00002bc`.
- Core selects NetAdapter at `+0x15e0` and calls `setQosParams`
  `0x10019df72`. Its flag byte is carrier `+0x17`.
- Flag `0x01` calls `configureLongRetryLimit` `0x100012bb6`, which uses
  Commander `sendIOCtlSet` opcode `0x22`; `0x04` and `0x08` call
  `configureLifeTime` `0x100015f06`, which uses the `lifetime` IOVAR.
- Flag `0x02` calls the RTS configuration route and `0x10` calls the
  powersave route. The port keeps only its independent local counterparts for
  those two bits; it does not claim the Apple transport or status semantics.
- After the NetAdapter call, `0x20` invokes
  `setReatimeAppPoliciesInternal` `0x10013a06a` and writes Core `+0x78f4`.
  Feature-gated `0x40` invokes 11be `configureMloFeatures` `0x10004394a`
  through the `mlo` IOVAR.
- No branch consumes unknown bit `0x80`, so it returns the normal successful
  no-op path.

Those missing branches are real owner/transport lifecycles. They do not
justify a local write-only cache and success return. The recovered code does
not establish a complete public carrier ABI or authorize guessed QoS IOVARs.

## Local boundary and non-claims

No retry-limit IOCTL, lifetime or MLO IOVAR, Core real-time policy mutation,
synthetic callback, or partial application of a mixed request is introduced.
No link-up, FaceTime/Wi-Fi-calling, congestion, scan, roaming, coexistence, or
raw BPF path changes. No direct runtime invocation is claimed; deployment
remains gated by the separate guest forced-off lifecycle state.
