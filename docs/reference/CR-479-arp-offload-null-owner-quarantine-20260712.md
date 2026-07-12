# CR-479: ARP offload null-owner quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 `AppleBCMWLANCoreMac` fileset begins at VM
`0xffffff800151d000` / file `0x0141d000`. Current raw
`AppleBCMWLANCore::setOFFLOAD_ARP` begins at `0xffffff80015d97f0`: it sets the
public result to raw `0x16`, then requires both a non-null carrier and the
private keepalive owner at Core expansion `+0x2c20` before state mutation.

Only after that owner exists does the Core refresh its IPv4 address, keepalive,
gateway, and gateway-tail state. It schedules
`handleIPv4AddressNotificationGated` at `0xffffff8001595a70`, invokes the
owner at `0xffffff80022c0ed2`, and has an additional optional owner update via
Core `+0x2c28`. The public 25C56 protocol wrapper
`FUN_ffffff80021e502c` validates its exact `0x18` carrier and enters this
path. Apple success therefore represents accepted private keepalive work and
notification, not a copied IPv4 carrier.

## Local divergence

AirportItlwm has no private keepalive owner, gated IPv4 notification, ARP/GARP
firmware transport, or optional owner update. Its direct
`setOFFLOAD_ARP(...)` nevertheless treated the generic `fNetIf` as sufficient,
copied fields into Skywalk caches, and returned success. `fNetIf` is not the
Apple Core `+0x2c20` owner and no firmware operation followed that success.

`setIPV4_PARAMS(...)` is a separate public selector and remains the local IPv4
cache producer. `setWCL_ARP_MODE(...)` has an independently recovered contract
and is outside this direct-offload correction.

## Local correction

The direct OFFLOAD_ARP setter retains its existing null/local-interface gate,
then returns raw `0x16` before cache mutation. This is the observed Apple
missing-owner outcome for the local absence of the private backend. It does not
claim that Apple rejects a valid carrier on hardware which constructs the owner.

## Deterministic guard

`scripts/arp_offload_quarantine_report.py --check` requires current 25C56
anchors in this note, the local raw result before direct ARP cache mutation,
the retained independent `setIPV4_PARAMS(...)` cache producer, and absence of
the private IPv4 notification/keepalive backend in the Intel source tree.
