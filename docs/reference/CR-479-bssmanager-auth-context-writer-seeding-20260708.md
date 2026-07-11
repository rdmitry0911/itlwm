# CR-479 BssManager auth-context writer seeding

> **2026-07-11 ownership correction:** The runtime evidence below remains
> historical, but its WCLConfigManager ownership claim and local pointer walk
> are superseded by
> `CR-479-driver-owned-bssmanager-lifecycle-20260711.md`. Apple owns a separate
> driver BssManager; current code never traverses or mutates WCL private state.

Date: 2026-07-08

## Scope

This batch closes the recovered `IO80211BssManager::setAuthContext(...)`
writer edge for the hidden WCL association carrier. It copies only the four
dwords Apple copies from `apple80211AssocCandidates`; it does not derive auth
policy, rewrite AKMs, set network flags, set associated-auth-type, or fabricate
association state.

## Reference Evidence

BootKC anchors from `10.7.6.112:~/Projects/ghidra_additional/kc_target_mangled.txt`:

- `0xffffff80015fbacc`
  `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)`
- `0xffffff800226701e`
  `IO80211BssManager::setAuthContext(IO80211AuthContext&)`

Static slice:

- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/01114_ffffff80015fbacc_FUN_ffffff80015fbacc.asm.txt`

Recovered caller sequence:

- copy dword `candidate +0x10` into local auth-context offset `+0x00`;
- copy dword `candidate +0x14` into local auth-context offset `+0x04`;
- copy dword `candidate +0x18` into local auth-context offset `+0x08`;
- copy dword `candidate +0x214` into local auth-context offset `+0x0c`;
- call `IO80211BssManager::setAuthContext(IO80211AuthContext&)`.

Recovered writer behavior:

- `setAuthContext` loads BssManager ivars from `this +0x10` and copies two
  qwords from the caller context into ivars `+0xf8` and `+0x100`.

Therefore the local ABI is exactly a 16-byte global `IO80211AuthContext`
carrier of four dwords. The `setAuthContext` declaration uses that named type
so the generated symbol matches `__ZN17IO80211BssManager14setAuthContextER18IO80211AuthContext`.

## Local Mapping

`AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)` already parses and stores
the hidden association carrier into `TahoeOwnerRegistry::AssociationOwner`:

- `authLower` from `+0x10`;
- `authUpper` from `+0x14`;
- `authFlags` from `+0x18`;
- `bssInfoFlags` from `+0x214`.

The patch now builds `IO80211AuthContext` from those stored fields and calls
`setAuthContext(...)` on the framework-owned BssManager recovered through the
existing WCLConfigManager route. The same writer is also called from the
post-association seed burst when a saved hidden association carrier exists, so
the context is restored if BssManager materialization was not available at the
initial hidden-association edge.

`setNetworkFlags(...)` and `setAssociatedAuthType(...)` are ABI-declared in the
follow-up 25C56 writer note, but producer seeding remains deferred. The former
needs a proven bit mask and producer polarity; the latter needs its exact
byte-array source, not just the public auth dwords.

Follow-up ABI note:
`docs/reference/CR-479-bssmanager-network-auth-writer-abi-20260709.md`.

## Validation

- `./scripts/test_payload_builders.sh`: passed.
- Tahoe guest build against
  `/System/Library/KernelCollections/BootKernelExtensions.kc`: passed; symbol
  gate resolved all 944 undefined symbols.
- Installed artifact:
  - SHA-256:
    `f9b69c6a68d5dafbcf44cc9616a8ad64458ba5db2207a709d1a2ea97430345af`;
  - UUID: `D97DAA81-96BB-3153-B5FB-C8417788BA89`.
- Manual join to `AIAMlab6235`: succeeded; `en1=10.77.0.47`.
- 240-second host-to-guest ping while guest-to-host iperf3 saturated the link:
  `239/240`, one ICMP loss, `0.416667%` packet loss, average RTT
  `487.966 ms`.
- Concurrent guest-to-host iperf3:
  `818 MBytes` transferred, receiver bitrate `28.5 Mbit/s`.
- Post-stress host-to-guest ping without iperf3: `20/20`, `0%` packet loss,
  average RTT `1.131 ms`.
- Post-stress interface state: `en1` stayed active at `10.77.0.47`; loaded
  kext UUID stayed `D97DAA81-96BB-3153-B5FB-C8417788BA89`.
- Serial log grep found no new `panic`, stack corruption, `NoCTL`,
  `IO80211QueueCall`, assert, or fatal signal. The known LQM zero counters and
  Bluetooth ACPI `0xe00002c7` lines remain.
