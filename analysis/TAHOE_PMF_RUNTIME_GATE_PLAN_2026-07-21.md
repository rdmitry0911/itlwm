# Tahoe IWX PMF/BIP runtime-gate plan

## P0.1 — repeated GUI STA combinations and recovery

Latest user directive2026-09-12: all directed open/WPA2/WPA3 transitions,
saved-network reuse/reselection and repetitions come before independent
scan/roam or mixed-AP development. Exercise awake, explicit off/on and real
sleep/wake separately, with actual GUI actions, DHCP and bidirectional traffic.
Preserve first failures; do not call an intervening automatic fallback a direct
transition. Follow the active GUI ledger for unavailable/untested cells.

Latest same-L2 controls are in
`docs/TAHOE_GUI_SAE_LOCAL_L2_RECOVERY_20260912.md`: menu saved-SAE
disconnect/reselect and System Settings off/on restore SAE/DHCP and pass
40-second HTTP/hash.600-probe results600/600+598/600 and593/600+593/600
retain first losses; no full lossless/S3 matrix pass. Endpoint capture removes
NAT ambiguity, not driver/AP/RF uncertainty. All resources terminal; same
boot/AF16/WiFiAgent5894, guestSAE/.219/ch9, hostLabAP/.226/ch153.
Next coherent driver boundary is separately reproduced accepted-roam/public-
scan cancellation and its exact reference lifecycle, not an assumed cause
of every packet gap. Current full-lifecycle baseline fails4/4. Preserve
other eligible repeated GUI pairs and actual-S3 cells as P0.1/open.
Archive73 files, manifest4692ea2a1d31ad30be2c9f7db1ebcb77359f1e323b9714e28f6aa77e6baaf256.
No production/release byte changes.

Preceding single-BSS/recovery controls are in
`docs/TAHOE_GUI_SETTINGS_WPA2_SAE_RECOVERY_20260912.md`: seven actual
System Settings controls, five PASS/two retained SAE forward59/60 losses.
Saved WPA2 profile change, two SAE-to-WPA2 returns, repeated WPA2-to-SAE,
and WPA2 off/on pass60/60 both ways (WPA2 also HTTP/hash). Native power-on
to DHCP BOUND3.150s. First and post-off/on SAE transitions lose a reply
during ch13-to9 roaming; marked lp7 endpoint capture proves server reply
emission but not exact lower-layer drop location. Fixture cleanup overlaps
lp7; no isolated RF interval is claimed. Final10:34:57 same boot/AF16,
guestSAE/ch9/.219, hostmanagedLabAP/.226, WiFiAgent5894 healthy, all terminal.
Next same-L2 endpoint GUI saved-network/recovery tests remove routed/NAT
ambiguity; remaining cross-security and actual-S3 cells remain P0.1.
Archive171 files, manifest61c7b1b95db2d2746905c3f0b3aad1b75519c742dccc6d91deb777cc489e6286.
No production/release byte changes.

Preceding System Settings controls are in
`docs/TAHOE_GUI_SETTINGS_SECURITY_PAIRS_20260912.md`: SAE-to-open,
open-to-WPA2 and WPA2-to-open pass60/60 each way. Repeat open-to-WPA2 is
60/60 forward,59/60 reverse during autonomous same-SSID ch161-to1 roaming.
Source seq25 exists but guest en1 lacks it; overlapping RSN completion and
later ROAMED do not establish a lower-layer cause. Three PASS/one loss;
second return to open unexecuted. Fixture terminal10:09:40, host restored;
guest10:13:26 OpenWrt/WPA2/ch1/.212, same boot/AF16/nativeWiFiAgent5894.
Next: single-BSS WPA2/System Settings repeats and explicit off/on cross-profile
recovery. Full six-edge/S3 GUI recovery stays open. Archive92 files verifies,
manifest1278927e67e9e524048254a06b6d6477064978020b51fbb9af3ac2bf5978a414.
No production/release byte changes.

Preceding open saved-security controls are in
`docs/TAHOE_GUI_OPEN_SAVED_RECOVERY_20260912.md`: saved open selection,
manual disconnect/reselect and open-start GUI off/on auto-recovery pass
DHCP, 60/60 each way and HTTP/hash. First subsequent open-to-SAE retains
60/60 forward, 59/60 reverse; request17 is missing from guest capture during
scan activity, without proof of a scan/driver cause. Return to open passes;
later open-to-SAE passes with its 60 reverse request/reply pairs matched
between source and guest. Six controls, five PASS and one loss; second
return to open still unexecuted. Fixture ends 09:42:33 with host restored;
guest ends LabAP/SAE/ch13/.219, same boot/AF16 and native WiFiAgent 5894.
Next: remaining SAE-to-open return, actual-security-checked open/WPA2 pairs
and both GUI frontends. Keep the observed loss attribution and real-S3 GUI
failure open. Archive 120 files verifies, manifest
`49331994688c2339c2f61f906fa7dc6c32ea596df2d72c5b3e0534143544067d`.
No production driver or release bytes change in this cycle.

Preceding strict saved-security controls are in
`docs/TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md`: real saved WPA2-only
selection, manual disconnect/reselect, WPA2-start GUI off/on auto-recovery,
and two direct WPA2-only/LabAP-SAE round trips all pass. Seven minute controls
are 60/60 each way; five WPA2 controls also pass HTTP 200/payload hash. Preserve
the first AP-side wrong-password failure and its native fallback separately.
No driver bytes changed. Fixture is terminal 09:19:13 with host restored;
guest ends on LabAP/SAE/ch13/.219, same boot and WiFiAgent 5894/runs 576.
Next: saved open manual-disconnect/reselect, open-start off/on and repeated
open/SAE pairs, in a fresh scratch. Keep full six-edge recovery and real-S3
GUI cells open. All 187 archive files verify, manifest
`9612f68f5f498299f0e75aacd49f4f1e030426695c8af1ebd18737dc6fbcaca1`.

The preceding GUI dependency is resolved for the tested laboratory path in
`docs/TAHOE_GUI_WIFIAGENT_READINESS_20260912.md`: exact reference init requires
unprivileged reading of the airport preferences plist. Root-only 0600 prevents
native WiFiAgent initialization and causes repeated 80-second ControlCenter
diagnostic waits. A metadata-only restoration to 0644 makes WiFiAgent start
normally without daemon restart or reboot. Two actual menu disconnects now
reach airportd in 13 ms; same-profile LabAP/SAE and OpenWrt/SAE reconnections
each pass DHCP and 60/60 traffic both ways. OpenWrt also advertises transition
PSK/SAE BSSes, so its name alone cannot qualify WPA2. Continue with a unique
WPA2-only fixture, open same-profile tests, and repeated recovery combinations.
The new read-only GUI-service prerequisite must precede these tests. No new
production kext or release identity is claimed for this environment repair.
Explicit off/on from OpenWrt/SAE auto-recovers it in about 12 seconds, followed
by a delayed 60/60+60/60 service control. Same native WiFiAgent PID and readable
preferences survive. That preceding cycle ended OpenWrt/SAE on channel 100, IPv4
172.16.66.212; no AP fixture is running. This is not a WPA2-only off/on pass.

The preceding executed result is in
`docs/TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md`: awake six-pair round
has DHCP6/6, four lossless minute controls and two WPA3-target59/60+59/60
controls. After explicit off/on, recovery and five repeated pairs pass;
last open→WPA3 joins/gets DHCP then delayed ControlCenter user-disconnect
requests interrupt service. Its observed GUI dependency was the roughly
10-minute interval from ControlCenter unjoin entry to airportd DISASSOC
receipt, not a proven spontaneous SAE failure. The original cell stays failed;
the later prerequisite repair does not retrospectively pass it. Retest the
interrupted pair and continue saved-profile/recovery combinations. USB
management remains intact; physical .22 is untouched and no new reboot occurs.

## P0 — actual GUI matrix (user priority reaffirmed 2026-09-12)

The first work item is
[`TAHOE_GUI_CONNECTION_MATRIX_20260912.md`](../docs/TAHOE_GUI_CONNECTION_MATRIX_20260912.md):
real UI joins/reconnects, saved-network changes across open/WPA2/WPA3, Wi-Fi
off/on and real sleep/wake, with AP and ad hoc cells tracked explicitly.
Retain failed first attempts; require DHCP and bidirectional traffic, not just
a Connected label. CLI/API joins do not qualify as GUI passes.

This current priority supersedes the historical July gate ordering below.
Protocol implementation, reference/decomp work and capability changes precede
a GUI cell only when they unblock its observed failure or a safety prerequisite.
Mixed PSK/SAE AP remains a GUI-cell dependency, not a separate higher priority.
Use the owned lab guest; preserve management access and do not modify/reboot
physical 10.90.10.22. Keep IWN and the shared IWM/IWX paths in scope.

## Persistent layer-selection criterion (2026-07-22)

For every next surface-reduction cycle, select the eligible discrepancy that
affects the most frequently used user-facing function first. A smaller
diagnostic/evidence discrepancy is not a higher priority solely because it is
easy to test. It may run first only if it is a hard prerequisite for the
higher-frequency path or prevents a false claim about that path; record that
dependency in the corresponding `FIX_CANDIDATE`.

Date: 2026-07-21

## Status

The committed BIP/PMF ownership layer (`459ef19`) has passed source contracts,
model tests, a clean Tahoe kext build, BootKC symbol resolution, and a
private-only AuxKC admission.  It has not passed a physical PMF-required PSK
association, group rekey, or traffic run.

That distinction is deliberate.  The existing post-PLTI trace is an IWN
ordered association evaluator.  IWX contributes only three categorical PMF
owner observations:

1. PMF EAPOL RX delivered;
2. q0 command doorbelled;
3. q0 completion observed.

Its generic evaluator correctly returns `BACKEND_UNSUPPORTED` for IWX.  These
three markers alone cannot prove an IGTK publication, active-slot transition,
or BIP rekey lifetime.

## Required new trace surface

Extend the safe-only trace ABI with an IWX-specific PMF/BIP classifier rather
than loosening the existing IWN evaluator.  The new surface must retain all
current privacy constraints:

- fixed categorical event IDs only;
- no key bytes, key hashes, PN/IPN values, status codes, firmware values,
  pointers, MAC/BSSID/SSID, addresses, channel, packet data, or timestamps;
- no allocation, logging, property publication, object retention, or frame
  inspection in a producer;
- one controller-bound capture generation and one sealed episode per verdict;
- dropped/overflow/mixed-generation/mixed-episode observations are always
  inconclusive.

The minimum positive evidence must distinguish both slot categories and their
active transition.  A suitable append-only vocabulary is:

| Categorical fact | Purpose |
| --- | --- |
| IGTK slot 4 published | Initial/rekey value reached normal BIP lifetime. |
| IGTK slot 5 published | The alternate slot reached normal BIP lifetime. |
| slot 4 selected for TX | Active PMF transmitter selection is slot 4. |
| slot 5 selected for TX | Active PMF transmitter selection is slot 5. |

The exact names/IDs must be append-only and versioned with the trace ABI.  The
event sources must be limited to post-success BIP publication under the
selected-BSS lock, after the corresponding firmware acknowledgement is owned.
Do not emit an event for a local/prepared descriptor, a failed backend command,
or a raw table access.  Any generic/BIP fallback path that can publish an IGTK
must use the same narrow helper so the evaluator cannot miss a valid source.

## Required evaluator matrix

Implement a dedicated C-compatible evaluator and C/C++ unit fixtures.  It may
consume the existing sanitized `AirportItlwmPostPltiTraceEntry` ring, but must
only accept the IWX backend and must not modify the IWN success semantics.

Positive fixtures:

1. Initial PMF transaction: PMF RX, q0 submit/completion, one slot publication
   and matching active-slot event, then normal port-valid/episode completion.
2. Slot 4 initial publication followed by slot 5 rekey and active-slot change.
3. Slot 5 initial publication followed by slot 4 rekey and active-slot change.
4. A valid traffic/recovery attestation is represented separately from the
   trace; neither a ping nor trace alone establishes the full verdict.

Negative/inconclusive fixtures at minimum:

- missing q0 completion;
- publication without the correct PMF owner sequence;
- active-slot event before its publication;
- repeated active selection without an intervening rekey;
- same-slot replacement mistaken for cross-slot rekey;
- slot 4/5 publication with a stale/mixed episode or generation;
- event after terminal seal, abort, overflow, or drop;
- cancellation/detach before final publication;
- generic IWN or unknown backend supplied to the IWX evaluator.

The evaluator must report the first categorical missing/invalid stage without
serialising raw trace data into committed evidence.

## Runtime runner requirements

Create a new bounded runner; do not repurpose the SAE profile runner as PMF
proof.  It must:

1. require a pre-existing Keychain/saved profile and never accept a password;
2. use only the pinned QEMU guest and its fixed management transport;
3. capture candidate identity before and after the test;
4. reset, arm, seal, and read only the safe trace properties;
5. require the IWX PMF/BIP evaluator verdict plus bounded traffic success;
6. preserve the default-route signature on `en0`, a direct lab route on `en1`,
   and the expected lab address invariant at every phase;
7. retain raw traces, interface output, and route dumps only locally; commit a
   sanitized hash/count/verdict attestation only;
8. fail closed if the trace is not sealed, has a drop, has a mismatched
   candidate identity, or the AP/route/address guard changes.

The AP procedure must be a separate one-at-a-time hostapd switchover helper:

- validate current AP process/config and channel-width invariants first;
- switch only the lab AP to the staged required-PMF PSK configuration;
- guarantee rollback to the current optional-PMF configuration on every exit;
- never alter host IP, NAT, forwarding, or default routes;
- restore the original AP before reporting a result;
- use a bounded short group-rekey only after the initial PMF test is proven.

## Candidate activation sequence

Once the runner and static/build gates exist, use a **fresh disposable guest
overlay**.  The base image is not a test target.  The only permitted sequence
is:

1. build the clean committed candidate in the pinned guest;
2. private-only AuxKC preflight;
3. transactional `tahoe_auxkc_activate_release.sh` activation;
4. guest-only reboot;
5. `capture_tahoe_lab_kext_identity.py` exact archive/installed/loaded binding;
6. four-cycle A2DF recovery baseline;
7. controlled PMF-required initial association, traffic, and rekey test;
8. AP rollback and another four-cycle A2DF recovery baseline.

Direct kext load/unload, physical-host actions, host reboot, arbitrary join
commands, or a PMF claim based on ordinary WPA2 traffic are out of scope.

## Saved-WPA3 priority lane — bounded Algorithm-3 peer RX (2026-07-22)

Under the persistent frequency-first rule, the next eligible user-facing
layer is saved WPA3-profile join, rather than additional independent PMF
diagnostic work.  Its first hard prerequisite is now implemented and
build-only verified: a selected-BSS-bound peer Algorithm-3 Commit/Confirm
copy path through net80211, a separate bounded AirportItlwm mailbox, and the
real IWX TX-terminal fence.

The layer remains dormant because no join owner publishes it.  The next
implementation must be an exact selected-BSS join handoff, not an ad-hoc
association enable:

1. form the controller target only from the actual selected BSS at the real
   join edge;
2. bind the controller mailbox and net80211 admission immediately before
   `ieee80211_new_state(..., S_AUTH)`, while snapshot RX itself continues to
   reject any frame before `S_AUTH`;
3. wake the Agent only after the state transition, preventing a first
   Algorithm-3 TX from racing the builder's `S_AUTH` condition;
4. route epoch/replacement/state cancellation nonblockingly to a controller
   relay clear/wakeup, so a quiet cancelled attempt cannot leave an old
   waiter/identity live;
5. retain PSK-only AKM and the generic Open-System quarantine until all of
   the above, Agent cryptography, SAE AKM/PMF policy, and key/association
   ownership layers are independently ready.

The 2026-07-22 isolated build (`dirtyc3403b973130`, 959/959 BootKC symbols)
is compile/link evidence only.  It is not permission for candidate activation
or a WPA3/SAE/PMF runtime claim; all runtime gates above remain in force.
