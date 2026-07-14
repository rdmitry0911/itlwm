# CR-479 — GAS abort false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setGAS_ABORT`.
Unlike a carrier setter, its pointer is not consumed by Tahoe; the local port
must not fabricate a null distinction or a completion event. It instead stops
acknowledging an abort operation for which it has no GAS/ANQP owner or
transport.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setGAS_ABORT` at `0x100019b52` obtains Core through `+0x88` and
  dispatches virtual `+0x540`.
- Core terminal `0x100137992` selects its GASAdapter at `+0x1560` and tail
  calls `AppleBCMWLANGASAdapter::setGAS_ABORT` `0x1001a171a`.
- The adapter does not read or test the public pointer. With feature bit
  `0x11`, it calls `issueGASAbort` `0x1000205e8`, which sends the four-byte
  value `1` through Commander `runIOVarSet("anqpo_stop_query", ...)`.
- The terminal then publishes a GAS complete event (`0xdc`) and clears the
  adapter active byte. If feature bit `0x11` is absent, it still enters that
  completion/clear-state flow with status `2`. The public return is the
  event-admission status rather than the IOVAR result or a fixed success.

## Local boundary and non-claims

AirportItlwm has no `AppleBCMWLANGASAdapter`, `issueGASAbort`,
`anqpo_stop_query` transport, GAS fragment/completion publisher, or adapter
state to clear. The former unconditional success was therefore false success.
The selector now returns `kIOReturnUnsupported` unconditionally. It adds no
pointer guard, private IOVAR, synthetic event, or guessed adapter state, and
does not claim Apple feature-gate or return-code parity.

`setGAS_REQ` remains a separate prior quarantine. This change neither alters
its carrier ABI nor claims direct runtime coverage of either GAS selector while
the independent guest forced-off lifecycle gate remains active.
