# CR-499 — public IOC 29 DEAUTH blind-success quarantine

Date: 2026-07-15

## Scope

This correction covers only the Skywalk BSD bridge SET half of
APPLE80211_IOC_DEAUTH (numeric 29):
AirportItlwmSkywalkInterface::setDEAUTH(apple80211_deauth_data *). It removes
its unconditional local success acknowledgement. It preserves the typed
carrier, the paired GET DEAUTH reader, the SIOCSA80211 route, APSTA DEAUTH,
the older AirportSTAIOCTL.cpp dispatcher, and the distinct void
APPLE80211_IOC_DISASSOCIATE (22) lifecycle. This correction does not
substitute one selector for the other.

## Recovered current BootKC selector topology

The selected current x86_64 BootKernelExtensions.kc identity is:

~~~text
path: /System/Library/KernelCollections/BootKernelExtensions.kc
SHA-256: eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d
UUID: F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5
~~~

apple80211setDEAUTH at 0xffffff80021c3a1f retains the original typed carrier
in RBX, invokes interface virtual +0xcc8 with selector 0x1d, and returns the
first handler's nonzero status immediately. For zero it performs
OSMetaClassBase::safeMetaCast; a successful cast tail-dispatches virtual
+0x2e0 with the original interface/carrier pair, while a failed cast returns
0xe082280e.

The selected capture proves gate/type/terminal-vtable topology. It is current
BootKC evidence, not canonical 25C56 AppleBCMWLAN DEXT evidence, and does not
establish the terminal owner's behavior, null-input behavior, complete carrier
layout, valid-input return code, firmware transaction, state mutation, or
management-frame transmission. The private symbols named setDEAUTH and
setDeauth are recorded only as unproven candidates; this correction does not
conflate them with the terminal virtual target. The capture is
docs/reference/artifacts/deauth-selector-dispatch-bootkc-current/raw.txt.

## Local correction

IOC 29 has a typed apple80211_deauth_data carrier with version, reason, and
BSSID, but the Skywalk local setter returned kIOReturnSuccess without reading
it, changing local deauthentication state, using firmware transport, or
publishing a management/event result. That was a blind successful
acknowledgement. The method now leaves the carrier unread and returns
kIOReturnUnsupported.

This is a no-owner safety boundary, not Apple null-input, valid-input
return-code, terminal-handler, carrier-layout, management-frame, state,
firmware, or runtime-selector parity. It invokes no private selector, IOVAR,
firmware command, scan, radio transition, deployment, association, or traffic
path.

## Deterministic guard

scripts/deauth_blind_success_quarantine_report.py --check verifies the current
BootKC identity/raw gate and terminal-vtable anchors, preserved typed IOC 29
route, the proven-terminal DEAUTH implementation (see the 2026-09-15
superseding section), preservation of the separate DISASSOCIATE boundary, and
the correction record in the signal-chain audit.

## 2026-09-15 superseding: proven WCL terminal justifies implementing DEAUTH

The fail-closed `kIOReturnUnsupported` above existed ONLY because the reference
terminal owner was unproven at the time (the BootKC capture established the
public gate/type/tail-dispatch topology but not the terminal's behavior). The
terminal is now proven, so this correction supersedes the fail-closed quarantine
and IOC 29 `setDEAUTH` is implemented as a faithful net80211 mirror.

Proven reference contract:
`WCLNetManager::setDEAUTH(bulletinBoardMessage&)` @0xffffff80020f06f4 (25C56)
validates its carrier (non-null AND `carrier_len == 0x10`), stamps
`carrier[0x28] = 1`, then calls
`leaveNetworkCommand(this, deauth_reason = *(carrier+4), 0,1,1,1,0,1,0,0,
ether_addr = NULL, "setDEAUTH")` and returns its result; an invalid carrier
returns `0xe0000001`. `leaveNetworkCommand` is the WCL network-teardown/leave —
the SAME routine the missed-beacons timeout drives. The carrier is
`apple80211_deauth_data { u32 version; u32 deauth_reason; ether_addr deauth_ea; }`
(deauth_reason at +4). Net effect: **leave/disconnect the current network
carrying the caller's apple80211 deauth_reason**; the carrier BSSID is NOT used
(`ether_addr` is NULL), so the teardown targets the current association.

Implemented behavior (AirportItlwmSkywalkInterface::setDEAUTH): a faithful
mirror of the same-file `setDISASSOCIATE` net80211 teardown
(ieee80211_wcl_join_cancel, public_initial_bssid_pin_disarm, publicAssociation
reset, roam_link_cancel, clearExternalPmkEligibilityLocked("setDEAUTH"),
ic_pae_mfp_requested = 0, postTahoeWclInternalLinkDownInd under __MAC_26_0, the
ic_state < SCAN early return, the ic_state > AUTH SEND_MGMT DEAUTH, the
ASSOC/AUTH early return, disassocIsVoluntary = true, del_ess, deselect_ess,
ic_assoc_status = UNAVAILABLE, new_state SCAN) — differing only in that it
publishes the caller's reason via `ic->ic_deauth_reason = da->deauth_reason`
(the local analog of leaveNetworkCommand carrying `*(carrier+4)`) instead of the
fixed `APPLE80211_REASON_ASSOC_LEAVING`. A null carrier returns
`kIOReturnBadArgumentTahoe`, the local analog of the reference's invalid-carrier
`0xe0000001` rejection. This is a functional-equivalence implementation of the
proven leave/disconnect contract, not a blind acknowledgement.

### 2026-09-15 runtime verification (lab AX211/iwx, macOS 25C56)

Built, materialized, and rebooted on the lab guest, then exercised via a tiny
SIOCSA80211 IOC 29 program on en1 (WPA3/SAE, associated with DHCP + gateway +
internet reachability):

- `setDEAUTH(reason=R)` returns success and disconnects en1 (net80211
  RUN -> SCAN, DHCP lease dropped), then the stack auto-reconnects to the same
  WPA3 network with a restored IP in ~7s. The golden (fail-closed) kext instead
  returned `kIOReturnUnsupported` (errno 102) and did not disconnect.
- On this Skywalk SET path the family marshals only the outer `apple80211req`;
  `req_data` reaches the handler as a raw userspace pointer (confirmed by
  dtrace: `da` is a user VA; a supervisor deref yields a stale value while
  `copyin` reads the true reason). The setter therefore reads the reason with
  `copyin`, which is why the caller's reason is published correctly.
- dtrace confirms the setter publishes the caller's reason: at `setDEAUTH`
  return, `ic_deauth_reason` holds exactly the value passed (observed 4660 for
  reason 4660). The paired getDEAUTH read from userspace typically returns
  `APPLE80211_REASON_ASSOC_LEAVING` (8) instead, because CoreWiFi reacts to the
  link-down by issuing its own `setWCL_LEAVE_NETWORK` ioctl ~250us later, which
  re-stamps `ic_deauth_reason = 8`. This follow-up WCL leave overwrites the
  shared field identically for the void `setDISASSOCIATE` selector, so it is a
  live-stack race on a shared field, not a defect in `setDEAUTH`. A null carrier
  is rejected (no blind success), and the high-priority surface (boot, WPA3/SAE
  assoc, DHCP, ping, scan, no panic) remained intact throughout.
