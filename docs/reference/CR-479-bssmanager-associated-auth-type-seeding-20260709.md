# CR-479 BssManager associated-auth-type seeding

> **2026-07-11 ownership correction:** The runtime evidence below remains
> historical, but its WCLConfigManager ownership claim and local pointer walk
> are superseded by
> `CR-479-driver-owned-bssmanager-lifecycle-20260711.md`. Apple owns a separate
> driver BssManager; current code never traverses or mutates WCL private state.

Date: 2026-07-09

## Scope

This batch closes the `IO80211BssManager::setAssociatedAuthType(...)` seed
edge for the Tahoe WCL association path. It does not seed
`setNetworkFlags(...)`; later 25C56 reference analysis proved that the
inspected BootKC has no static network-flags producer, so a local seed would
fabricate state. See
`CR-479-bssmanager-network-flags-closure-20260711.md`.

## Reference Evidence

Guest BootKC 25C56 writer ABI:

- `0xffffff8002243084`
  `IO80211BssManager::setAssociatedAuthType(unsigned char *, unsigned short)`.
- recovered body accepts a non-null byte pointer and a length up to `0x101`,
  copies the caller bytes to the associated-auth-type ivar, and stores the
  accepted `uint16_t` length.

Existing static slices:

- `cr479_next_layer_external_supplicant_pmk_delivery_full_remaining_layer_attempt2_20260517T0232/07_xrefs/BootKC_full_STA/5250_0xffffff8002267344_FUN_ffffff8002267344.asm.txt`
  shows `IO80211BssManager::addToEnvBssInfo(...)` building a local auth-type
  carrier and passing it into the environment/current-network publication
  callback.
- `cr479_next_layer_external_supplicant_pmk_delivery_full_remaining_layer_attempt2_20260517T0232/07_xrefs/BootKC_full_STA/5252_0xffffff80022678d8_FUN_ffffff80022678d8.asm.txt`
  shows the associated-auth-type getter path using the stored byte carrier.

Local carrier:

- `apple80211_authtype_data` is a 12-byte `{ version, authtype_lower,
  authtype_upper }` structure.
- `setWCL_ASSOCIATE(...)` already extracts `authLower` and `authUpper` from
  the recovered hidden association carrier offsets `+0x10` and `+0x14`.
- The public/local `setAUTH_TYPE(...)` path stores the same two auth dwords for
  non-hidden association flows.

## Local Closure

`AirportItlwmSkywalkInterface::seedBssManagerRateAndMcs()` now seeds
`setAssociatedAuthType(...)` through the already recovered framework-owned
BssManager:

- hidden WCL association carrier present: use `AssociationOwner.authLower` and
  `AssociationOwner.authUpper`;
- no hidden carrier: use the existing `current_authtype_lower` and
  `current_authtype_upper` state from `setAUTH_TYPE(...)`;
- skip only when both auth dwords are zero, avoiding a fabricated unassociated
  auth carrier.

`setWCL_ASSOCIATE(...)` also seeds the writer at the same early hidden-carrier
edge where the existing auth-context writer is seeded, then the post-association
seed burst repeats the publication once the BssManager object is materialized.

`tests/tahoe_payload_builders_test.cpp` now locks the 12-byte
`apple80211_authtype_data` layout used by this writer.

## Validation

- `./scripts/test_payload_builders.sh`: passed.
- `./scripts/tahoe_reproducibility_smoke.sh`: passed.
- Tahoe guest build against
  `/System/Library/KernelCollections/BootKernelExtensions.kc`: passed; symbol
  gate resolved all 947 undefined symbols.
- Installed binary:
  - SHA-256:
    `9d025fb997a0f84f7ae71c9ecbf348da2fdcb800a24aa4c7d8b29424b1104023`;
  - UUID: `072E1763-4FB8-3EC0-8BAD-BCA0F7CD24E1`.
- Controlled join to `ITLWM-Lab-3c95c7` reached DHCP `10.77.0.157`.
- 240-second ping while guest-to-host iperf3 saturated the link:
  `240/240`, `0.0%` packet loss, RTT
  `1.568/678.022/1442.828/204.252 ms`.
- Concurrent iperf3:
  `630 MBytes` sent at `22.0 Mbits/sec`; receiver saw `629 MBytes` at
  `21.9 Mbits/sec`.
- Post-stress IORegistry kept `IO80211SSID`, `IO80211BSSID`,
  `IO80211RSNDone = Yes`, controller `IOLinkStatus = 3`, and DHCP stayed
  active.
- Post-stress `system_profiler SPAirPortDataType` still reported current
  network information, but `networksetup -getairportnetwork en1` still returned
  `You are not associated with an AirPort network.` and public CoreWLAN still
  returned `ssid=(null) bssid=(null)`.

## Network-flags disposition

`setNetworkFlags(bool, unsigned int)` remains ABI-declared only by design.
The 25C56 direct-reference and raw address audits found no static Apple
producer, so AirportItlwm intentionally has no corresponding association
seed. The deterministic closure and retained evidence are in
`CR-479-bssmanager-network-flags-closure-20260711.md`.
