# Agent handoff — Tahoe PMF/BIP runtime gate

**P0.1 — repeated GUI open/WPA2/WPA3 transitions, saved-profile reuse and
recovery now come first (latest user directive, 2026-09-12).** Cover all six
directed cross-security pairs plus same-security saved-profile reselection,
then the same paths after explicit off/on and real S3. Keep first failures,
DHCP and bidirectional service evidence; never hide an intermediate join or
recovery toggle. A stalled post-S3 GUI remains open. Standalone scan/roam
analysis and mixed AP work cannot displace the remaining eligible STA cells.
Latest single-BSS System Settings/recovery cycle:
`docs/TAHOE_GUI_SETTINGS_WPA2_SAE_RECOVERY_20260912.md`. Seven controls:
five PASS, two retained SAE forward59/60 failures. Same-security different
saved WPA2 selection, two SAE-to-WPA2 returns, repeated WPA2-to-SAE and
WPA2-start off/on recovery pass60/60 both ways; WPA2 also HTTP/hash.
Native power-on to DHCP BOUND3.150s. First WPA2-to-SAE and post-off/on
WPA2-to-SAE lose one forward reply during observed SAE ch13-to9 roaming.
lp7 marker capture proves seq24 reply emitted at server10:33:04.119309,
absent in guest en1; not an unanswered server probe or an exact RF/driver
drop location. lp7 spans fixture cleanup/restoration10:33:08; retain that
confounder. lp4 all240 packets match each endpoint, independently PASS.
Final10:34:57 same boot/AF16/WiFiAgent5894, guestLabAP/SAE/ch9/.219,
hostLabAP/ch153/.226, no fixture/collector remains, management intact.
Archive171 files verifies at
`/home/dima/Projects/itlwm/aiam-gui-local-recovery-runtime-20260912.lMETmO`,
manifest61c7b1b95db2d2746905c3f0b3aad1b75519c742dccc6d91deb777cc489e6286.
Original RAM QqnCv6/archive immutable. Production/release unchanged5e98d640.
Next scratch `/dev/shm/aiam-gui-l2-recovery-20260912.dgvxIG`: same-L2 host
STA .226/guest .219 interface-bound prerequisite, then actual saved-SAE GUI
recovery/reselection with both endpoint captures, no AP teardown/NAT hop.
Keep remaining open/WPA2 recovery pairs and real-S3 GUI open and prioritized.
The next scratch same-L2 prerequisite is already terminal PASS10:38:21:
three1400-byte packets each direction, explicitwlp0s20f3/en1, no GUI action
or profile/route change. It is not an additional GUI service cell. Use a
new helper with fail-closed identity checks; preserve the executed preflight.

Preceding System Settings cycle:
`docs/TAHOE_GUI_SETTINGS_SECURITY_PAIRS_20260912.md`. Actual SAE-to-open,
open-to-WPA2 and WPA2-to-open pass60/60 each way; open also HTTP/hash.
Repeat open-to-WPA2 joins/DHCP but is60/60 forward,59/60 reverse. It starts
e4:3a:65:44:e4:dd/ch161 and ends e4:3a:65:44:e4:dc/ch1: autonomous same-SSID
roam. Source seq25 emission10:04:50.387922 is absent in guest en1; native RSN
completion10:04:50.554, ROAMED10:04:54.408. Preserve correlation without
asserting a lower-layer cause. Four controls, three PASS/one loss; second
return to open unexecuted. Fixture terminal10:09:40, restore0. Final10:13:26
same boot/AF16, OpenWrt/WPA2/ch1/.212, WiFiAgent5894 healthy. Next eligible:
single-BSS WPA2/System Settings repeats and explicit off/on cross-profile
recovery; retain post-S3 GUI and loss attribution as open dependencies.
Archive92 files verifies at
`/home/dima/Projects/itlwm/aiam-gui-settings-pair-runtime-20260912.tdpaOJ`,
manifest1278927e67e9e524048254a06b6d6477064978020b51fbb9af3ac2bf5978a414.
Original RAM source PSNy6T and archive are terminal/immutable. New scratch
`/dev/shm/aiam-gui-local-pair-recovery-20260912.QqnCv6` is allocated for next
controls. Production/release unchanged5e98d640; no new driver bytes.

Preceding open saved-security cycle:
`docs/TAHOE_GUI_OPEN_SAVED_RECOVERY_20260912.md`. Real GUI saved open selection,
manual disconnect/reselect and open-start off/on auto-recovery pass DHCP,
60/60 each way and HTTP/hash. Unjoin reaches airportd in 11 ms; native DHCP
BOUND is about 3 seconds after power-on. First subsequent open-to-SAE has
60/60 forward, 59/60 reverse; request17 is absent in guest capture during
BEST CONNECTED SCAN activity. Preserve this loss without claiming scan/driver
causality. Return to open passes; a later open-to-SAE repeat passes with all
60 reverse requests/replies matched in both source and guest captures.
Six controls: five PASS, one retained loss; second return to open unexecuted.
The 900-second fixture ends 09:42:33 and restores the host; at 09:44:06 guest
is LabAP/SAE, 9a:fb:5d:97:a9:02/ch13, .219, same boot/AF16 and healthy native
WiFiAgent 5894/runs 576. No fixture/capture process remains active.
Next eligible P0.1: remaining SAE-to-open return, actual-security-checked
open/WPA2 repetitions and both GUI frontends. Keep loss attribution as a
GUI-observed dependency; full real-S3 GUI recovery stays open.
Archive 120 files verifies at
`/home/dima/Projects/itlwm/aiam-gui-open-recovery-runtime-20260912.KEtvcn`,
manifest `49331994688c2339c2f61f906fa7dc6c32ea596df2d72c5b3e0534143544067d`.
Archive and RAM source are immutable; use a new scratch next cycle.
Production/release remains 5e98d640; no driver bytes changed.

Preceding strict saved-security cycle:
`docs/TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md`. On the unchanged AF16
image/boot, the previously saved WPA2-only fixture passes actual GUI
selection, manual disconnect/reselect, and automatic recovery after GUI
off/on. Two subsequent direct WPA2-only/LabAP-SAE round trips also pass.
All seven service controls are 60/60 both ways; all five WPA2 targets also
pass HTTP 200/exact payload hash. The first attempt's AP-side wrong password
and native failure/fallback are preserved, not relabelled as a driver fix.
The WPA2 menu unjoin reaches airportd in 11 ms; native WiFiAgent remains
5894/runs 576 with readable 0644 preferences. Fixture terminates 09:19:13,
host profile restored; guest ends LabAP/SAE, 9a:fb:5d:97:a9:02/ch13, .219.
No AP/monitor fixture remains active. Next eligible P0.1 phase: saved open
manual disconnect/reselect, open-start off/on and repeated open/SAE pairs.
Full post-S3 GUI and all six-edge recovery combinations remain open.
Use a fresh scratch; terminal archive contains 187 verified files at
`/home/dima/Projects/itlwm/aiam-gui-strict-security-runtime-20260912.bXIHBe`,
manifest `9612f68f5f498299f0e75aacd49f4f1e030426695c8af1ebd18737dc6fbcaca1`.
Production/release is still 5e98d640; this cycle does not change driver bytes.

Preceding GUI-service cycle:
`docs/TAHOE_GUI_WIFIAGENT_READINESS_20260912.md`. The delayed ControlCenter
path is localized to repeated 80-second diagnostic waits: native WiFiAgent
cannot initialize because the airport preferences plist is root-only 0600.
Exact guest-binary decompilation (40 workers) and EACCES observations confirm
the prerequisite. At 08:33:30 only this laboratory file's read mode is restored
to 0644, with identical contents and a root-only backup. WiFiAgent PID 5894
starts normally without a reboot/daemon restart. Actual menu disconnects on
LabAP and OpenWrt reach airportd in 13 ms; same-profile SAE reselections each
pass DHCP and 60/60 traffic both ways. OpenWrt's selected BSS is transition
PSK/SAE on channel 100, so that result is NOT a WPA2-only pass. Use a uniquely
named WPA2-only fixture for the remaining strict-security cells. Run the new
read-only `scripts/check_tahoe_gui_service_readiness.sh` as the console user
before further GUI tests. Explicit off/on starting from OpenWrt/SAE also
auto-recovers that profile in about 12 seconds; the delayed minute control
passes 60/60 each way. WiFiAgent PID and repaired read mode survive the writes.
At that preceding cycle's terminal, guest is OpenWrt/SAE, BSSID 50:4f:3b:cd:dd:67/ch100,
IPv4 172.16.66.212, with independent USB management. No host AP fixture is
running. Production/release remains 5e98d640/AF16. Durable cycle evidence:
`/home/dima/Projects/itlwm/aiam-gui-unjoin-runtime-20260912.zvNP4h`.
All 132 files verify; manifest SHA256
`f4325686edd11bbeb1eb67a0c9ce37d185b0c025ee5b8512ef76ec146eb184c6`.
Archive and RAM source are terminal/immutable; use a new scratch next cycle.

Previous repeated-matrix result: all six awake direct GUI pairs associate/get
DHCP; four60/60+60/60, both WPA3-target first checks59/60+59/60. After explicit
WPA3 off/on, recovery and five repeated directed pairs pass60/60+60/60.
The final open→WPA3 joins/gets DHCP then is disconnected by ControlCenter:
GUI unjoin entries07:56:09.271/07:56:33.987 arrive at airportd as DISASSOC
only08:06:29.688/.744. That cycle ended inactive with USB management intact;
fixture ended08:10:17, host profile restored. This observed
GUI-action queue/lifecycle delay was the dependency subsequently investigated;
do not call it a spontaneous SAE failure or silently repair/relabel the cell.
See `docs/TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md`.
Repeated-matrix scratch:
`/dev/shm/aiam-gui-repeat-matrix-20260912.Z2kWqj`.
This cycle is terminal and immutable; all336 archived files verify in
`/home/dima/Projects/itlwm/aiam-gui-repeat-security-runtime-20260912.59gN8F`.
Manifest4100c668cf447955b424714a3a1666be23717f0d7892890412bb8bb896b92f38.
Start the next cycle in a fresh scratch root; do not replay fixture/UI scripts
without revalidating their guards and current screen.

**P0 — actual GUI connection/reconnection matrix (user priority reaffirmed
2026-09-12).** Start with
[`TAHOE_GUI_CONNECTION_MATRIX_20260912.md`](TAHOE_GUI_CONNECTION_MATRIX_20260912.md).
This takes precedence over the historical gate/checkpoint material below and
over independent protocol, capability, or static-parity work. A lower-level
fix runs first only to unblock an observed GUI cell or its safety prerequisite.
Mixed PSK/SAE AP is one such cell dependency, not a replacement for the matrix.

Latest GUI control (2026-09-12, after a9af94c0): direct saved WPA3↔WPA2
selection passes DHCP and20/20 each way. Real S3 on AF16 recovers Wi-Fi but
again stalls WindowServer's framebuffer wake acknowledgment. The guest is
gracefully rebooted to a separately labeled awake baseline. After GUI off/on,
saved WPA2 selection retains a19/20 reverse first test overlapping background
scan; an unchanged repeat is20/20. A later bounded continuous capture is now
terminal in `/dev/shm/aiam-gui-scan-roam-20260912.ll3xGl`:350/346 forward after
DHCP; host450/405 includes pre-join loss and must not all be called driver loss.
Packet-level attribution is pending, behind the repeated-matrix priority above.
This is a GUI-observed dependency,
not a return to independent roam research. See
`docs/TAHOE_GUI_STA_SLEEP_MATRIX_20260912.md`; current boot is
`7A5FAA36-5FAA-451F-A32B-70EB251B8660`, same AF16 binary. Older next-step
mixed-AP notes below are preserved but do not supersede this active GUI cell.

Date: 2026-07-21

## Exact repository state

- Workspace: `/home/dima/Projects/aiam/scratch/sae-runtime-stage-20260721`
- Code checkpoint before this documentation commit:
  `e909f3d06c4b100f6e314c884363b9f8617a1c11`
- The handoff documentation itself is a later docs-only commit.  Begin by
  checking the actual `git rev-parse HEAD` and `git status --short`.
- The worktree is intentionally detached.  The same commit is pushed to
  `itlwm/tahoe-ax211-sae-pmf-20260719`; push future commits explicitly with:

  ```sh
  git push itlwm HEAD:tahoe-ax211-sae-pmf-20260719
  ```

- Do not create a release for this checkpoint.  `v2.4.0-alpha` remains the
  sole mutable asset until a whole version-level feature layer has passed its
  exact-candidate runtime evidence.

## Persistent prioritization rule (user directive, 2026-07-22)

### Highest priority: GUI matrix (user directive, 2026-09-12)

The GUI connection/reconnection matrix is the first functional priority.
Exercise actual macOS UI selection of saved and newly presented networks,
repeated connections and changes between open/WPA2/WPA3, then the same paths
after real sleep/wake and Wi-Fi off/on. Include ad hoc and AP UI paths in the
coverage ledger; an untested or unavailable cell remains open. Native API or
command-line selection is diagnostic evidence, not a GUI cell pass.

Preserve and diagnose failed first attempts before any recovery toggle.
Reference/decomp work takes precedence only for a concrete GUI-observed fault
or a necessary safety prerequisite. Use the owned laboratory guest and protect
independent management connectivity; do not modify/reboot physical10.90.10.22.
The current matrix ledger is `docs/TAHOE_GUI_CONNECTION_MATRIX_20260912.md`.

GUI checkpoint2026-09-12:5e98d640 closes the live stale former-BSS ownership
fault after WPA2→SAE. Exact AF16 GUI WPA3 first join/reconnect/off-on plus
open/WPA2 regressions pass; the alpha is published with public readback
verification. See `docs/TAHOE_GUI_STALE_BSS_CACHE_20260912.md`. Continue with
the GUI-derived mixed PSK/SAE AP prerequisite: the awake GUI WPA2 AP cell
now passes external DHCP,20/20 both ways,cold ARP and NAT on AF16. The
reference security-popup capability also exposes a mixed-mode default that
our AP authenticator still rejects; do not publish that capability alone.
See `docs/TAHOE_GUI_AP_SECURITY_MATRIX_20260912.md`. Keep post-S3 GUI,
ad hoc and the remaining profile/security combinations explicitly open.

### General selection rule

When choosing the next layer that reduces surface mismatch with the reference,
first choose the eligible layer that unblocks the most frequently used
user-facing function. Do not prefer a lower-frequency evidence, diagnostic,
or convenience layer merely because it is smaller or easier to verify.

Safety prerequisites and explicit external blockers still apply. A lower-level
layer may precede the highest-frequency user path only when it is an actual
hard prerequisite for that path or prevents a false success claim about it;
the candidate report must state that dependency explicitly. For the current
project this means a real saved-profile association/WPA3 path and its
driver/Agent bridge outrank further standalone PMF diagnostic polish once the
in-flight bounded verification work is complete.

## Commits delivered in this checkpoint

1. `efd1c7a test(tahoe): build trace producers before auditing them`
2. `459ef19 net80211: harden BIP IGTK publication and PMF handoff`
3. `269096e test(sae): add dormant group19 relay FSM foundation`
4. `e909f3d test(runtime): record A2DF control and mbedTLS SAE KAT`

The BIP/PMF commit makes IGTK slots 4/5 safe to publish, preserves old-slot
RX lifetime during a rekey, restricts IGTK use to multicast management frames,
and fences HAL/user GTK paths away from those slots.  It is not yet a physical
PMF-required association claim.

The group-19 mbedTLS material is a pinned OpenWrt-compatible **test intake**.
Neither the AirportItlwm kext nor AirportItlwmAgent links mbedTLS yet.

## Verified evidence

### Exact clean source build

`bash scripts/run_tahoe_sae_quarantine_layer.sh` passed for source identity
`e909f3d` in the pinned Tahoe guest.  It covered static/model contracts,
AirportItlwm Tahoe build, all 959 BootKC symbol resolutions, trace-producer
audit, Agent build, and RegDiag build.

Its isolated guest directory was:

```text
/tmp/aiam-tahoe-sae-layer-gate.FnqOMP
```

This gate did **not** install, load, publish, release, or reboot a kext.

### Private AuxKC admission

The clean candidate passed a private-only AuxKC preflight:

```text
candidate UUID: 823A3EBC-C8B9-3327-945B-BA397B4208B8
candidate SHA-256: cb987e25f7bf5497fc9b4dae12878e50f2e690c46c85278ac3e84e4e77118b05
private admission: PASS
canonical mutation: none
exact AuxKC members: 5
```

Guest-private preflight material was placed below
`/private/tmp/aiam-bip-pmf-e909f3d-auxkc.FR7fQp/`.  It is disposable evidence,
not a release artifact.  The canonical kext and AuxKC were unchanged.

### Existing control and crypto evidence

- Four A2DF radio OFF/ON cycles of saved control candidate `034BABDD` passed
  with 20/20 bounded lab pings and preserved management/default-route
  invariants.  See `analysis/TAHOE_A2DF_CONTROL_034BABDD_2026-07-21.md`.
  This is a control data-plane baseline only, not PMF/BIP/SAE proof.
- The OpenWrt mbedTLS SAE group-19 KAT passed in the Tahoe guest.  See
  `analysis/TAHOE_OPENWRT_MBEDTLS_GROUP19_KAT_2026-07-21.md`.  It is not kext,
  Agent, PMF, association, or traffic evidence.

## Current hard boundary

Do **not** activate the new candidate merely to collect ordinary WPA2 traffic.
The project currently lacks an IWX PMF/BIP runtime evaluator capable of
attributing an initial IGTK install and a slot-4/5 rekey to the candidate.

Existing `run_tahoe_sae_lab_profiles.sh` is intentionally insufficient:

- it needs three pre-existing profiles;
- it treats IWX as ordered-trace unsupported;
- it observes only the default route;
- it has no IGTK publication/rekey witness.

`run_tahoe_lab_radio_gates_no_route_mutation.sh` is still required as a
recovery/data-plane gate after activation, but it is not PMF/BIP proof.

## Laboratory state and prohibitions

- Only the pinned QEMU Tahoe guest may be changed or rebooted.  Do not touch
  physical `10.90.10.22`.
- Do not reboot the host.  A guest reboot is permitted only after a clean
  candidate, private AuxKC preflight, an explicit transactional activation,
  and a disposable-overlay plan exist.
- Never direct-load or unload AirportItlwm.
- The live 5 GHz lab AP is 80 MHz on channel 153 and uses the optional-PMF
  transition configuration
  `/home/dima/Projects/ax211-5g-ap/hostapd-5g.conf`.
- A staged PMF-required PSK configuration exists at
  `/home/dima/Projects/ax211-5g-ap/hostapd-5g-wpa2-pmf.conf`, but it is not
  live.  It may be used only through a new one-at-a-time switchover helper
  with mandatory rollback.  Do not reuse helpers that mutate host IP, NAT, or
  routes.
- Before any join-capable test, use saved-profile/Keychain-only preflight.
  Do not put passphrases into scripts, logs, process arguments, commits, or
  evidence.
- Preserve the guest default route on `en0`; `en1` may have only the direct
  laboratory route.  Do not use route, address, DHCP, raw-scan, or arbitrary
  join helpers outside a bounded runner.

## Next implementation, in order

1. Read `analysis/TAHOE_PMF_RUNTIME_GATE_PLAN_2026-07-21.md`.
2. Implement the new safe categorical IWX PMF/BIP trace/evaluator and its
   complete deterministic fixture matrix before any candidate activation.
3. Add a credential-safe PMF-required PSK runner that enforces the A2DF
   route/address invariants and records only sanitized attestations.
4. Build and test the new layer in the isolated Tahoe guest; commit and push
   it.  Still do not release.
5. Create a fresh disposable QEMU overlay, then run private preflight,
   transactional AuxKC activation, guest-only reboot, and exact loaded-identity
   binding.
6. Run A2DF recovery first.  Only then use the controlled PMF-required AP
   switchover, initial PMF association/traffic, short group-rekey observation,
   rollback the AP, and run A2DF recovery again.

Any missing saved profile, disposable-overlay mechanism, PMF trace integrity,
or rollback witness is an explicit prerequisite failure—not a reason to infer
PMF/BIP success from a generic association.

## Continuation checkpoint — bounded real SAE peer RX (2026-07-22)

The newest implementation layer is a deliberately dormant but real
IWX/net80211/controller Algorithm-3 peer ingress spine.  It has passed the
pinned isolated Tahoe build gate with source identity `dirtyc3403b973130` in
`/tmp/aiam-tahoe-sae-layer-gate.yAVfzG`: kext, trace producer, Agent, and
RegDiag built; all 959 undefined symbols resolve against BootKC.  The gate
did not install, load, publish, release, reboot, join, or touch radio/AP
state.

What is now present:

- `ieee80211_recv_auth()` can copy only an exact current-selected-BSS,
  `S_AUTH`, group-19-HnP-admitted peer SAE Commit/Confirm into a bounded,
  credential-free event;
- net80211 epoch/replacement invalidation, exact controller generation, and a
  separate conflict-aware AirportItlwm peer mailbox prevent delayed or
  AP-flooded RX from reaching a later relay generation or starving IWX TX
  terminal completion;
- physical IWX TX terminal success remains the required fence before a peer
  value advances the relay FSM.

This is not WPA3 support.  It has no selected-BSS join owner, no enabled SAE
association ingress, no Agent cryptography, no PMK/AKM/PMF activation, and no
runtime association evidence.  Do not weaken the existing WPA3 quarantine or
represent the build as an on-air result.

Next autonomous layer, because it unblocks saved WPA3 profiles, is the real
selected-BSS join handoff.  It must pre-arm exact RX admission before the
transition to `S_AUTH`, wake the Agent only after that state is live, and add
a nonblocking epoch/state-cancellation callback that clears/wakes the
controller relay even if no later peer/TX event arrives.  Those three fences
are prerequisites to activating this bridge.
