# CR-479 BssManager ad-hoc-created teardown

> **2026-07-11 ownership correction:** The runtime evidence below remains
> historical, but its WCLConfigManager ownership claim and local pointer walk
> are superseded by
> `CR-479-driver-owned-bssmanager-lifecycle-20260711.md`. Apple owns a separate
> driver BssManager; current code never traverses or mutates WCL private state.

Date: 2026-07-08

## Scope

This batch closes only the teardown half of the recovered
`IO80211BssManager::setAdHocCreated(bool)` lifecycle. It clears the
framework-owned ad-hoc-created bit on local disassociate and WCL leave edges.
It does not assert ad-hoc creation, create an IBSS, or synthesize AP/IBSS
state.

## Reference Evidence

BootKC static slices on `10.7.6.112`:

- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/00123_ffffff8001546ada___ZN22AppleBCMWLANNetAdapter16leaveNetworkSyncEj11LeaveMethodjt10ether_addrPKc.asm.txt`
- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/01114_ffffff80015fbacc_FUN_ffffff80015fbacc.asm.txt`
- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/01117_ffffff80015fcd86_FUN_ffffff80015fcd86.asm.txt`

Recovered call sites:

- `AppleBCMWLANNetAdapter::leaveNetworkSync(...)` loads the BssManager pointer,
  clears `ESI`, and calls `IO80211BssManager::setAdHocCreated(false)` at
  `0xffffff800154711d`.
- `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)` calls
  `setAdHocCreated(true)` only after successful join and only when
  `candidate +0x0c == 1`.
- `0xffffff80015fcd86` calls
  `AppleBCMWLANJoinAdapter::createAdhocNetwork(...)`; its successful tail also
  calls `setAdHocCreated(true)` and then `setAuthContext(...)` from a separate
  carrier.

The positive `true` edges are therefore tied to real Apple ad-hoc creation
paths. The local Tahoe port currently keeps `setIBSS_MODE(...)` as a public
carrier cache and does not create an IBSS through the lower net80211/HAL owner,
so setting `setAdHocCreated(true)` locally would be false framework state.

## Local Mapping

The local teardown producers already represent the reference leave edges:

- `AirportItlwmSkywalkInterface::setDISASSOCIATE(...)`
- `AirportItlwmSkywalkInterface::setWCL_LEAVE_NETWORK(...)`

Both now recover the framework-owned `IO80211BssManager *` through the existing
WCLConfigManager route and call `setAdHocCreated(false)` after the local
state gate confirms the interface has reached at least scan state.

The true edge remains deferred until the local IBSS/create-adhoc owner is
implemented against the recovered `createAdhocNetwork(...)` path.

## Validation

- `./scripts/test_payload_builders.sh`: passed on the host checkout.
- Tahoe clean build on the macOS guest from
  `/Users/devops/Projects/itlwm-codex-build`: passed, with all 945 undefined
  symbols resolving against BootKC.
- Installed kext:
  - SHA-256:
    `74b536ee46df06bced64644b9a66f3b6210ad52e1f733c06dbb03ec42a4d6fa1`
  - CDHash: `c51c2fc2b92c72498bfaca0cdf295f1ac880fd63`
  - loaded UUID: `0ABA0FB2-4DDB-3E22-9D12-FAA0C9DC4E9D`
- Joined `AIAMlab6235`; `en1` stayed active on `10.77.0.47`.
- Concurrent long validation:
  - host-to-guest ping for 240 seconds: 240 transmitted, 239 received,
    0.416667% packet loss;
  - guest-to-host `iperf3` for 240 seconds: 776 MiB received at
    27.1 Mbit/s.
- Post-stress ping: 20 transmitted, 20 received, 0% packet loss,
  0.852 ms average RTT.
- Post-stress unified log and serial-log checks found no panic, kernel stack
  memory corruption, NoCTL, IO80211QueueCall, assert, or fatal signatures.
