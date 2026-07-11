# CR-479 Tahoe WCL link-state current-BSS association tail

> **2026-07-11 ownership and state correction:** The runtime trace below
> reached WCL's separate BssManager and is retained only as historical
> evidence. It is not the Apple driver ownership path. The bool argument to
> `setCurrentBSS` is stored separately; `isAssociated()` tests the current-BSS
> pointer itself. The implementation and ownership claims are superseded by
> `CR-479-driver-owned-bssmanager-lifecycle-20260711.md`.

Date: 2026-07-10

## Scope

This batch restores the reference WCL link-state tail that follows
`IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(...)`.

It does not add a public Apple80211 fallback gate, does not synthesize a
CoreWLAN answer, and does not write private bytes directly. The only new
writer is the exported IO80211Family direct-call surface already used by the
reference current-BSS path:

- `IO80211BssManager::getCurrentBSS() const`;
- `IO80211BssManager::setCurrentBSS(IO80211BSSBeacon *, bool)`.

## Reference Evidence

Tahoe 25C56 AppleBCMWLAN reference:

- `AppleBCMWLANInfraProtocol::setWCL_LINK_STATE_UPDATE(...)` first calls the
  base `IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(...)`;
- it then tail-calls `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)`;
- the core helper rejects null payloads with `0xe00002bc`;
- payload byte `+0x6` selects link-up versus link-down;
- payload byte `+0x8` is the third `AppleBCMWLANNetAdapter::setLinkUp(...)`
  boolean and gates the current-BSS refresh path;
- the current-BSS path calls
  `AppleBCMWLANBssManager::setCurrentBSS(...)`, which in turn calls
  `IO80211BssManager::setCurrentBSS(beacon, assocFlag)`;
- `IO80211BssManager::setCurrentBSS(...)` stores the beacon, retains/releases
  it through the exported beacon ABI, and writes the association flag at the
  BssManager ivar byte consumed by `IO80211BssManager::isAssociated()`.

The exported symbols on the live guest BootKC are:

- `_ZNK17IO80211BssManager13getCurrentBSSEv`;
- `_ZN17IO80211BssManager13setCurrentBSSEP16IO80211BSSBeaconb`.

## Local Closure

`AirportItlwmSkywalkInterface::setWCL_LINK_STATE_UPDATE(...)` now:

- keeps the base family call first;
- returns `0xe00002bc` for a null payload, matching the reference core helper;
- clears current BSS on link-down payloads;
- on link-up payloads with byte `+0x8 != 0`, reads the framework-owned current
  beacon through `IO80211BssManager::getCurrentBSS()` and republishes that same
  beacon through `IO80211BssManager::setCurrentBSS(..., true)`;
- reseeds the existing rate/MCS/LQM BssManager carriers after the associated
  current-BSS write.

The local path uses `tahoeRecoverWclBssManager(...)`, which recovers the
framework-owned WCL `IO80211BssManager` object; it does not allocate or fake a
BssManager.

## Runtime Validation

Validated on the Tahoe 26.2 guest after AuxKC install and reboot:

- loaded kext UUID `45FDE7B5-81BA-37CE-B65D-E8A47736F016`;
- installed/staged binary SHA-256
  `ccfc31c5122903cef3f737e2194289d662dcdf3e04340c22f2ffe2f180f25790`;
- `scripts/test_payload_builders.sh` passed;
- `scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  passed and reported all 949 undefined symbols resolved against BootKC;
- controlled join to `AIAMlab6235` reached DHCP `10.77.0.47`.

Live DTrace during the controlled join showed the previous WCL-created beacon
publish and the new reference-tail association publish on the same
framework-owned BssManager:

```text
TRACE_BEGIN
SET_CURRENT_BSS this=ffffff9030af5aa0 beacon=ffffff94fe2cc6a0 assoc=0
GET_CURRENT_BSS ret=ffffff94fe2cc6a0
SET_CURRENT_BSS this=ffffff9030af5aa0 beacon=ffffff94fe2cc6a0 assoc=1
```

Data-path regression gate:

- unpaced 240-second TCP `iperf3` saturation completed with `IPERF_RC=0`,
  `863 MBytes` sent at `30.2 Mbits/sec`, and the interface stayed active at
  DHCP `10.77.0.47`; ping under full saturation reported `237/240` replies;
- paced 240-second `iperf3 -b 20M` plus concurrent ping completed with
  `PING_RC=0` and `IPERF_RC=0`;
- paced ping reported `240 packets transmitted, 240 packets received, 0.0%
  packet loss`, RTT `0.602/14.998/75.354/14.835 ms`;
- paced iperf3 transferred `572 MBytes` at `20.0 Mbits/sec` sender and
  receiver;
- post-stress `en1` remained active at DHCP `10.77.0.47`.

## Non-Claims

This closes the WCL link-state current-BSS association tail only.

`networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.` on the same runtime, even
after DTrace proves `IO80211BssManager::setCurrentBSS(..., true)` was called.
A later item-220 follow-up classifies the matching top-level Dynamic Store
SSID/BSSID redaction as reference airportd pruning through
`wifi_allow_sensitive_info`. This note therefore remains limited to the WCL
link-state current-BSS tail and is not a reason to re-add the rejected public
fallback gate.
