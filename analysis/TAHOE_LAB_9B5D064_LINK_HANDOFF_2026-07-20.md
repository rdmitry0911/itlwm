# Tahoe release-bound link-handoff laboratory result — 2026-07-20

## Exact candidate and activation boundary

Release `v2.4.0-alpha-9b5d064` was verified to contain an actual
`AirportItlwm.kext`, not a loose Mach-O. Its archive SHA-256 is
`654ab9c57d7546871c0c313ff7f09e77f4b1c59c49f468905e98a4f1fe602811`;
the kext binary SHA-256 is
`93d7f53dd54608ae76e5c99d8dd61de1437059280ab72ab53c3db42914f37317` and
its LC_UUID is `ACCF10BE-552A-34D3-8908-7706514B2E3C`.

Before activation, the exact release bundle passed a private AuxKC admission
run: private `kmutil create` and both inspections returned zero, the exact
five-member collection was present, and canonical AirportItlwm and AuxKC
witnesses remained unchanged. The laboratory guest then received a full
two-repository AuxKC rebuild. Its new collection contained exactly five
members, retained all four non-AirportItlwm members unchanged, contained the
candidate UUID, and was atomically swapped only after that check. Rollback
copies of both bundle and collection were retained. Only the laboratory guest
was rebooted; no physical host or `10.90.10.22` action occurred.

After reboot, the pinned read-only identity gate proved archive, installed
bundle, and loaded kext identity equal. The sanitized identity evidence is
`evidence/runtime/tahoe_lab_kext_identity_9b5d064.json`, SHA-256
`dc529fcda6b534278e8e9b98986f8bada6b91d813df185a1c112b83ccb532c5e`.
This is a successful exact-candidate identity and controlled-activation
scenario; it is not a Wi-Fi association claim.

## One credential-safe multi-boundary epoch

The release-bound guest ran one 15-second saved-profile epoch. The profile was
selected only by the committed SHA-256 identifier
`552cb0ce6725ac3c13feef9e1c7947737b14201acd794cf5cd7c45808a8cac7d`;
no SSID, BSSID, passphrase, key material, raw trace, packet, snapshot, route
dump, or interface dump is versioned. The trigger exited zero, and the
RegDiag epoch was coherent: ABI 2, `mode=0x35`, `block=0x0`, 14 trace entries,
and no trace overflow.

The exact carrier reached the driver once: `auth=0x400`, `policy=0x6`,
`pmf=0`, with one accepted PLTI publication and delivery. It did **not**
produce EAPOL TX/RX or a successful link-up, so the strict
`wpa2-sha256-psk` result is `INCONCLUSIVE_OR_FAIL`, not a WPA2 or traffic
success.

The epoch captures all adjacent link-handoff boundaries in one run:

- pre and post snapshots both showed net80211 state `RUN` and controller
  status `0x3` (already active at the snapshot boundary);
- `setLinkStatus` recorded four events: two applied and two unchanged. In
  particular, the real post-clear `0x1 → 0x3` active transition was applied;
  the later unchanged active event was only a duplicate, not a short-circuit
  of that real transition;
- link publication recorded two queued events and two `off-gate-rejected`
  events, with no accepted inherited publication;
- both rejected worker events had `onThread=1` and `inGate=1` (encoded as
  raw predicate value `3`); and
- no `JOIN_ABORT` event appeared during this 15-second settling window.

The structural result is therefore `LINK_PUBLICATION_INCOMPLETE`. The removal
of the aliased low-latency early link-up did move the observed real transition
through `setLinkStatus`; this is a successful narrower correction, not just a
theory. The remaining boundary is the pre-existing CR-479 safety condition:
the itlwm-owned work-loop event source is still serviced while the gate is
held, so relaxing the guard to call the inherited WCL publication would
reintroduce the known null-owner panic. This result neither attributes the
failure to Keychain nor calls SAE/WPA3 the cause; pure SAE and PMF were not
tested in this epoch.

## Network and evidence limits

The capture wrapper issued no route, address, DHCP, install, load, unload, or
reboot command. It did observe that the guest route-table snapshots differed
across the association attempt, so route preservation is explicitly false and
there is no connectivity, DHCP, ping, or traffic claim. The post-epoch pinned
guest query succeeded, but that is only a bounded reachability observation.

The complete machine-checkable record is
`evidence/runtime/tahoe_lab_9b5d064_link_handoff.json`, SHA-256
`151e1e2e86298eca79893997b1c8f7b6665ea571fb6cc0dccbf13c35ac0f07d6`.
It contains only release identity, numeric state/counter facts, redacted
profile identity, and SHA-256 witnesses for the private raw artifacts. The
adjacent executable contract rejects any attempt to turn this safety and
boundary result into a functional Wi-Fi success claim.

## Committed verification

The accompanying contract replays the SAE/PMK and link-handoff evaluator
fixture matrices, verifies every release/activation/runtime/non-claim field,
and binds the document to both evidence hashes. The aggregate Tahoe gate also
passed its static contracts and an isolated kext, BootKC-symbol, Agent, and
RegDiag build after this record was added. That build validation installed,
loaded, rebooted, and published nothing; it is recorded here separately from
the guest activation and runtime observation above.
