# Deferred BSS origin identity — 2026-09-11

Latest: `52a0eeae` is pushed, built and loaded on real IWN/6235. The bounded
S3/SAE/WPA2/open runtime below preserves service, but contains packet loss and
latency spikes. It does not qualify full legacy roaming or a replacement release.

## Implemented checkpoint; full roaming remains open

The preceding user-status turn was read-only. This continuation changes the
common path used by IWN/IWM/IWX, following the exact 25C56 Core/WCL lifecycle
recovered in `TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md`. The reference keeps
an admitted source/target operation across preparation and actual terminal
events. Its internal implementation is not our node-reference scheme.

Both explicit WCL and legacy same-ESS background switches now capture source
and target MAC/BSSID, original association epoch, continuation epoch, fresh
join sequence, reassociation census sequence and admitted reassociation serial
before allocation or AMPDU/management callbacks. No deferred callback borrows
the current owner's serial. Captured identity is checked again after callbacks
and before arming or acting on the selected cache entry.

Source-leave epoch advancement tests that same identity inside the actual
selected-BSS leaf which advances the epoch. It returns its captured result,
not a new owner's epoch observed after revocation callbacks. The fresh-join
sequence detects replacements even when BSSID and association epoch coincide.
The serial-zero legacy path cannot acquire a subsequently admitted WCL roam.

The data gate is set before stopping AMPDU and submitting DEAUTH. Failure
before accepted source leave preserves the source epoch/keys and does not
request SCAN. A later missing-target failure retires only its captured serial.
The failure helper returns the retired epoch by value; after public failure
delivery, the old continuation must still match its sequences, epoch and
source before requesting SCAN. A reentrant successor's state is left alone.

This is a reentrancy/identity correction, **not** an always-asynchronous
main-workloop handoff or full physical source drain. The final check-to-action
boundaries still need that serialization. The generic selected-node join has
its own further callback/epoch transitions; full immutable ownership must be
carried through those, state workers, AUTH/ASSOC/key results and overall roam
completion. Pure-SAE's separate retarget path is not relabeled as this legacy
switch and its generation-zero failure path remains open.

## Executed verification before build/load

- Complete Linux payload aggregate: terminal exit 0, including adjacent
  physical TX retirement, station/BA and IWX TVQM suites.
- Linux and macOS ASan/UBSan execute the complete common capture/current,
  deferred producer, failure and switch methods together with the existing
  actual node-copy/ref/release and IWN TX/reset/free methods: 25 positive cases.
  The two former obsolete-owner/failure-reentry requirements now pass.
- Complete actual epoch implementation plus actual identity predicate:
  118 carrier/epoch cases pass on both operating systems. Fourteen additions
  include same-peer fresh-join replacement, serial/census/epoch/source/target
  mismatch, absent lock, legacy admission, and replacement during revocation.
- Actual reassociation admission/abort/retirement/controller-gate suite:
  22 cases pass on both systems. macOS also repeats full physical IWM/IWX
  TX/reset/free extraction and the adjacent common contracts successfully.

Hardware notification scheduling, inner node join, allocation and management
submission are explicit boundaries in the deferred fixture. The epoch fixture
executes the real guarded epoch implementation separately. These are not RF
claims. No IWM/IWX hardware qualification follows from this IWN laboratory.

The full deferred gate deliberately retains two red requirements (exit 134):

1. Historical pre-copy one-reference liveness, restricted to that boundary.
2. **New established-source terminal-before-arm reproduction.** Actual
   node_copy leaves zero references. The explicit management-submission
   boundary completes the frame using the real ref/release functions before
   returning to the complete producer. That producer later installs its
   callback at zero references: no join occurs. This is not fixed by changing
   a reference-count constant or by immutable identity alone.

`BSS_SWITCH_EXPECT_DEFECTS=1` verifies these failures; its wrapper exit zero
does not mean complete roaming passes. The full aggregate includes only the
positive subgroup, with this red gate separate and explicitly reported.

Evidence:

- `/tmp/aiam-bss-identity-linux-full-20260911-r2.log`, SHA-256
  `442f22879f722c572cb145ff6179e376849501b4e2b9838d8148e109bb93a002`.
- `/tmp/aiam-bss-identity-macos-selected-20260911-r2.log`, SHA-256
  `cbf0ec9a49b890b7a117082534a1bcb18865c8eb994f9290f5b0687049a656f3`.
- `/tmp/aiam-bss-identity-linux-red-audit-20260911.log`, SHA-256
  `1802550c91dcf72a27274487e2c9fcf7adc7288f20271b78547cfd119ae1fd06`.
- Production content-manifest digest:
  `cd1422640631f3ec06a9e897e9943a1d457925bcaf81db75731957493ad78178`.

At this checkpoint the source is not built/loaded. The lab still runs
`1896beb2`, boot `F31CDD13-6EBC-470A-A76E-9E231395AB84`, image UUID
`46BA6759-1E6A-35B3-A9B8-A90C66B09A69`. Public release remains `97fe747c`.
Build, recoverable activation and actual runtime are the next steps, not
optional substitutions for the outstanding end-to-end gates. Physical `.22`,
unrelated QEMU instances and the user-owned local `Build/` are untouched.

## Updated reference tooling supplied by the user

Read-only inspection of `10.7.6.112:/home/dima/Projects/ghidra` confirms clean
branch `feature/bounded-function-parallel-decompiler`, HEAD `5995e24caa`, with
the four supplied commits `69d6405859`, `eb9a8c5ff9`, `27fed11060`,
`5995e24caa`. They cover block-structure recovery, exact jump-table bounds,
partial-symbol alias recovery and artificial-PHI storage respectively.

The checkout's `src/decompile/cpp/decomp_opt` is present but dated May 18;
the usual `os/linux_x86_64/decompile` path is absent. Neither is attested as
the updated build. Before using these fixes on a disputed reference function,
build/identify an exact-HEAD executable in a separate scratch directory and
record its hash. Heavy decompilation must use **40 actual cores/interfaces**.
The user's agent's successful scratch build/replays are reported evidence,
not a claim that this session repeated them or installed a new binary.

## Exact build and loaded candidate

Production commit: `52a0eeaed2da7fbe74be470d76f97e0b8559caf1`. The guest's
source files pass SHA-256 verification against the complete local production
manifest (`cd1422640631...` above); the guest's older `.git` HEAD is not used
as source identity. Tahoe builds successfully, all 1085 external symbols
resolve against the target BootKC, and there is no `thread_call_cancel_wait`
dependency.

- Build log `/tmp/aiam-bss-identity-build-20260911-r1.log`, SHA-256
  `605fa1a991a9df972d9948d2406a31e5ac172097413ff581ea914351d3cbf8d9`.
- Mach-O UUID: `AA1DD77D-223B-330A-9AC4-1C79FD9A8B7B`.
- Mach-O SHA-256:
  `c0fc81edf4333cc1ec1f9591c53c7dbd1ab51292143d963da50856aa07aac9cb`.
- Current loaded boot: `9358FA34-B833-4F4E-9183-9B8D911F5419`.
- Owned QEMU PID `1252914`, name `aiam-iwn-after-scd-control`, SSH `3338`.
- Child/evidence root:
  `/home/dima/Projects/itlwm/aiam-iwn-bss-identity-runtime.mbFA1U`.
- Child `tahoe-bss-identity.qcow2`, separate `OVMF_VARS.fd`, serial
  `serial-bss-identity.log`; `start-owned-child.sh` retains exact launch args.

The preceding `1896beb2` guest shut down normally after the 06:18:07 UTC
request; serial reports power-off and its process terminated. The preserved
stopped parent is
`/home/dima/Projects/itlwm/aiam-iwn-tx-retire-runtime.nFXV01/tahoe-tx-retire.qcow2`,
1,092,288,512 bytes, SHA-256
`81986b20c8ef333e0158e886ab61cab592339d1f893dcac8d7fe724ce7585cc9`.
Its saved OVMF hash is
`755e9357a44ea8f91c990904deac9708b16755844b483c771977da53c795bc4e`.
Both hashes were rechecked after child startup and again after runtime;
lsof independently showed that parent read-only and only the child writable.
The working parents and base were not deleted or written.

Private five-member AuxKC admission passed with no canonical mutation.
Transactional activation preserved all four companions at
`/private/var/tmp/aiam-iwn-activation-bss-identity-cd1422640631-1/activation-20260911T062147Z`.
One normal guest reboot was requested at 06:22:29 UTC. The first SSH banner
timeout during that reboot was followed by successful readback of the same
QEMU; it was not treated as a crash or a reason to restart it. At 06:23:50 the
new boot loaded the exact candidate and had automatic WPA3/DHCP `.219`.

## Actual same-image STA runtime

All probes below use 1400-byte ICMP payloads. Forward is guest-to-AP/router;
reverse is the physical host AX211-to-guest. Native selections/off-on are
networksetup/system-framework operations, **not GUI interactions**.

| Case | Actual recovery | Forward / reverse | Maximum RTT, ms |
| --- | --- | --- | --- |
| Initial saved WPA3 | Automatic SAE and DHCP after loaded-candidate reboot | 20/20, 20/20 | 24.580 / 19.042 |
| Actual S3, saved WPA3 | Automatic DHCP without another join/toggle | 20/20, **19/20** | 27.161 / 32.614 |
| Controlled received SAE failure | Valid-BSS SAE and DHCP after owned failure cleanup | 20/20, **17/20** | 22.019 / 21.196 |
| Post-S3 WPA2 selection | Native selection, external four-way handshake and DHCPACK | 20/20, 20/20 | 155.736 / 10.416 |
| Same WPA2 after one off/on | Same profile/private MAC/address recovered automatically | **19/20**, 20/20 | 951.509 / 1027.808 |
| Post-S3 open selection | Native selection, NONE security and external DHCPACK | 20/20, 20/20 | 888.624 / 966.658 |
| Final saved WPA3 return | Automatic return after the open fixture stopped | 10/10, 10/10 | 16.944 / 20.129 |

Later successful cases do not erase earlier losses. Latency near one second
also occurs on open/WPA2, so it must not be attributed solely to SAE. Physical
scan scheduling, TX/receive queues and possible host-AP off-channel effects
need correlated observation before assigning the cause. No clean low-latency
or whole-matrix qualification is claimed.

### Real S3, no Ethernet substitute

Direct Wi-Fi SSH was verified first. Only the temporary management USB
Ethernet was removed while awake; QEMU and the guest both showed no Ethernet
or tablet, just the boot keyboard. Sleep was requested at 06:27:10 UTC.
Actual serial `ACPI SLEEP` and QEMU `paused (suspended)` establish S3, not
merely the return from pmset. One owned-monitor wake at 06:28:06 produced
`ACPI S3 WAKE`. DHCP publication followed at 06:28:16.722; BOUND at
06:28:17.728. Wi-Fi-only SSH confirmed the same boot and image.

The S3 row's independent forward/reverse probes completed while Ethernet
was still absent. Management USB returned only afterward. Native configd
records a Wi-Fi roam at 09:28:46.702 local; timing alone is not causal proof
for the missing reverse packet. No post-S3 GUI or guest-AP-mode test is claimed.

### Received SAE failures after that S3

The controlled wrong-password pure-SAE/required-PMF LabAP was ready before
one accepted native directed request at 06:30:22 UTC. The observer ended
normally at 09:31:35 local with errors=0.

At 09:30:38, fresh join generation 3 consumed the actual empty-body Confirm
status-1 rejection, acknowledged producer/lower/SAE cleanup (1/2/4), and
published one failure returning 0. The valid BSS then completed SAE at epoch
49; DHCP publication followed at 09:30:39.849. Reassociation generation-zero
rejections at 09:30:29 and 09:31:09 still use the separate generic path.
Another valid SAE completed at 09:31:21, followed by DHCP at 09:31:22.886.
Repeated bad-candidate selection remains open. The fixture stopped normally
at 06:31:39 and restored host managed Wi-Fi with its wired route unchanged.

### WPA2 and open are real STA joins, not legacy-roam proof

Native WPA2 selection began at 06:33:39. External hostapd confirms the RSN
four-way handshake, and dnsmasq ACKed `192.168.73.35` at 06:33:52. One native
Off at 06:35:25 reached inactive carrier/no IPv4 before one On at 06:35:27.
External DHCPACK followed at 06:35:28; the guest verified the same WPA2 profile
and address at 06:35:29. No extra selection or second toggle was used.

That fixture stopped normally at 06:37:24. Native open selection began at
06:38:19, with external DHCPACK for `192.168.73.26` at 06:38:30 and guest
security NONE. Its fixture stopped normally at 06:40:12. The guest later
automatically returned to saved WPA3, with DHCP publication at 06:40:54.208,
without another join/toggle. Final readback at 06:41:11 retained the exact
boot/image, active WPA3 `.219`, and management `10.0.6.15`.

These RF tests do not establish execution of the newly corrected legacy
deferred callback. Its actual new producer/epoch symbols are present in the
loaded image (`new-helper-probes.log`), but probe presence is not execution.
Successful/failed/missing/replaced-target legacy roaming and full source drain
remain required. Both host AP fixture controllers and all four hostapd/dnsmasq
children were checked terminated; no observer or build remained in the guest.

### Retained runtime evidence

Each table case has separate `*-forward.log` and `*-reverse.log` in the child
root. Readbacks, native selection/off-on logs, monitor logs and launch/freeze
scripts are retained there too. Selected SHA-256 records:

- `s3-native.log`:
  `5f0a3d8d19028b763955c576bfb48f060e23e41527e531bc0e11ffe955df95ef`.
- `sae-native.log`:
  `27bc2a49b720eb56695fa4cf6d4dc669f113bfa92230aa37457fe3f980eaf45a`.
- `wpa2-native.log`:
  `eb3d2b6dd0b26067a7239ea2ff5e60e35c98c376061f02996347fd72b456d309`.
- `open-through-recovery-native.log`:
  `56a7e4b44928666607394861b285c20569b48e184c64dcd06178e2c1d47396b7`.
- `serial-through-runtime.log`:
  `b17f4c44d903f3363c2c32a89c64401e7e137bebc628e3b7ecb495a9f3d790d3`.
  No matched panic, kernel-trap, firmware-fatal/error or device-timeout
  signature; ordinary boot/AMFI/Bluetooth diagnostics remain in the log.

The reused SAE wrapper retains
`/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU/bss52a0-posts3-auth1-observer.log`,
SHA-256 `8a60ab2a019cb95f5a6e1cee57f9e1686bf90108799e44e852ad107365424fe8`.
External hostapd/dnsmasq evidence is under
`/tmp/aiam-gui3e73-runtime.kiWiTP/bss52a0-posts3-{auth1,wpa2,open}-*`.

## Verified recoverable space reclamation

After activation, free space fell below the existing 1.5-GiB RF admission
guard. Only the old unused July-21 image below was removed locally, after a
successful rsync byte copy, independent remote/local SHA-256 and length
comparison, fresh owned-image backing census (no child reference), and final
fuser check (no open user). The later free-space readback rose to about 1.78 GB.

- Original:
  `/home/dima/Projects/itlwm/overlays/overlay-a2df-ba4a2f0-20260721T093006Z.qcow2`.
- Verified archive:
  `dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/bss-identity-space-20260911.236VRB/overlay-a2df-ba4a2f0-20260721T093006Z.qcow2`.
- Length 712,245,248 bytes; SHA-256
  `aedc8865433fe47b9326c90ca0d6dc9c30e596544a938e5574cc0eeabb433f4d`.
- Unchanged backing: `/home/dima/Projects/itlwm/tahoe.qcow2`.

Restore with rsync to the original absent/unused path and verify that hash
before reuse. This is a verified disk archive, not a claim of a newly assembled
full VM/OVMF recovery kit. The current VM, all working parents/base, `.22`,
other agents' QEMU processes and local user `Build/` were untouched by removal.

## Continuation and publication

Public release is still the qualified `97fe747c` artifact. This candidate is
not promoted on these mixed and incomplete results. Next preserve exact
ownership through an always-asynchronous main-workloop handoff and actual
source producer/management/data/BA/command/reset retirement in IWN/IWM/IWX;
the terminal-before-arm test must become live without an invented reference
threshold. Carry ownership through selected-node replacement and real
AUTH/ASSOC/key/completion, and correlate the observed traffic stalls with both
guest and host radio activity. Full GUI/security/sleep/AP/multi-AP gates and
IWM/IWX hardware qualification remain open. The original autonomous goal is
neither complete nor blocked.
