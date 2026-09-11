# Real SAE peer failure and candidate retirement — 2026-09-11

## Proven pre-fix boundary

On real IWN/6235 with source `4d979f3b`, boot
`D946CBDB-621A-4524-9D0C-4E3FE52C1E88`, the controlled wrong-password SAE AP
produced real status-1 Confirm rejections. The read-only observer covered
03:54:16–03:55:47 local time (UTC+3), terminating normally with zero errors.
The in-kext crypto engine consumed empty-body Confirm rejections at association
epochs 32, 54 and 69 and returned PEER_AP_REJECT (4) each time. Retirement
requested generic SCAN and no AUTH failure entered the common join ledger.
A separate NO_NETWORKS producer and successful SAE epoch 53 were also observed.
Thus the observed rejection was not lost at 6235 firmware RX or short-frame
parsing. This does not prove every PMF or rejection path.

Evidence root: `/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU`.

- `night4d-auth5-observer.log`, SHA-256
  `f54a54eadcf8e431535e477c12ffca665ac54bc930135f19b73c7baa2d3c01eb`.
- `night4d-auth5-native.log`, SHA-256
  `ec0ec4af130f75c296058d493a9a15b65aa86f1ede362a98df55c6ac63582522`.
- External hostapd log:
  `/tmp/aiam-gui3e73-runtime.kiWiTP/night4d-auth5-hostapd.log`.

The auth3 request returned EBUSY and its fixture stopped before authentication;
auth4 was admitted but its real scan ended NO_ELIGIBLE_TARGET. Their empty
failure observations are not RX-negative controls. Auth5 kept the real AP
beaconing for 15 seconds before the accepted request. The fixture cleaned up
normally and restored host managed Wi-Fi without changing its wired route.

## Implementation and executable regressions

Production commit `97fe747c` and fixture portability commit `5e7a6735` are pushed.
The common JoinAdapter generation is captured at SAE producer admission and
kept separate from the credential request/relay generation. A consumed peer
failure claims only that exact current AUTH request under selected-BSS then
engine leaves. Peer statuses remain peer statuses; local crypto/method failures
use a local error with no invented received status. The cancelled owner retains
the generation and requests the existing owned lower cleanup instead of an
autonomous next scan. Producer acknowledgement follows local value scrubbing;
lower and SAE acknowledgements remain separate actual retirement edges.

The full production worker, claim and retirement methods pass 19 ASan/UBSan
scenarios on Linux and macOS, including same-BSS same-epoch replacement and
delayed TX terminal retirement. The pre-fix complete worker independently
compiles and fails the missing-producer assertion (exit 134). Crypto outcome,
task scheduling and lower firmware cleanup are explicit fixture boundaries.
Existing common join/physical scan/controller payload tests pass on both OSes;
the complete Linux payload aggregate passes. The first macOS fixture compile
failed on its SDK's absent explicit_bzero; the existing portable test support
fixed that test-only issue before the successful run.

Source and guest production fingerprint:
`a9b779d9482a218b801a40253e16ebf39fc17fa67659b458de1f9cf7adb87c10`.
Built UUID: `69D766FF-86F9-3337-ADE0-48361B68A59C`.
Built Mach-O SHA-256:
`560a3f5bb365a7f5c9a5e6f8d7418b0ff0db0931e392901f38aa658a4e8f5a63`.
All 1085 external symbols resolve against the running Tahoe BootKC; there is
no thread_call_cancel_wait dependency.

- Linux aggregate log: `/tmp/aiam-sae-peer-failure-linux-20260911.log`, SHA-256
  `7f9e497b8b852c182ec5e31ce82a4338d180d5c4eaf5c062ea921d5592a6d8c9`.
- macOS test/build log: `/tmp/aiam-sae-peer-failure-macos-20260911-r2.log`, SHA-256
  `b954b445fae179740ebae520b9ddfd6199acbccf11e449a9220e4aaa5f67ade7`.

## Loaded-image rejection recovery

Private AuxKC admission and transactional activation passed. Activation root:
`/private/var/tmp/aiam-iwn-activation-sae-peer-97fe747c-1`, transaction
`activation-20260911T012053Z`. After a normal guest reboot, boot UUID
`02DED0EA-E840-4DAE-BED2-1067761106C1` loaded the exact `69D766FF` image above
on the passed-through IWN/6235. Four companion kexts were preserved; no unload
was attempted. The first admission-helper invocation stopped before staging
because a pipefail/grep-early-exit check returned 141; the corrected read-to-EOF
check passed. The first completion-observer compilation rejected a signed
format argument; an explicit uint64_t cast passed compilation before any RF
fixture was admitted. Neither failed setup is counted as a radio test.

The `peer97-auth2` observer covered 04:23:43–04:25:14 local time (UTC+3), with
zero diagnostic errors and normal terminal exit. One native directed request
was accepted at 01:24:02 UTC, after 15 seconds of real fixture beaconing.
Its first reassociation rejection has no fresh JoinAdapter generation, as
expected: this patch does not invent an owner for the separate 0x49 path.
Subsequent fresh saved-profile attempts exercised the corrected producer:

| Local time | Fresh join generation | Actual observed sequence |
| --- | --- | --- |
| 04:24:20 | 2 | SAE Confirm status 1, empty body; engine rejects; producer/lower/SAE cleanup acknowledgements 1/2/4; one failure publication returning 0; successful SAE with the valid same-SSID BSS at epoch 33 |
| 04:25:04 | 4 | Same real rejection and three cleanup edges; one publication returning 0; successful SAE with the valid BSS at epoch 56 |

Native configd logs publish IPv4/DHCP at 04:24:22.286 and 04:25:06.501.
The independent watcher confirms restored active en1 with 172.16.66.219.
No extra join command, radio toggle or reboot caused either recovery. The
fixture then terminated normally and restored host managed Wi-Fi at 01:25:30
UTC while retaining its wired default route. Independent subsequent 1400-byte
checks passed 20/20 guest-to-router and 20/20 host-AX211-to-guest; the same boot
and loaded UUID and a real DHCPACK were revalidated. The captured new serial
interval has no matching panic, firmware fatal/error or device-timeout text.

Evidence in the private root:

- `peer97-auth2-observer.log`, SHA-256
  `1def008d253c44b777cc212316f6054f262aa4b0199c92336ee1830a8f44331c`.
- `peer97-auth2-watch.log`, SHA-256
  `4e9a94398b0a4189be9272291c637c5666dd8744aa77ebbf75612d10f324f51e`.
- Complete info/debug `peer97-auth2-native.log`, SHA-256
  `ca5b6506058936f6963aa1811a48835bf64d3d852e9c04fdbb9630f76bdc38d9`.

This confirms the fresh-AUTH received-SAE-rejection/cleanup/publication path,
not complete candidate policy. The system revisited the bad BSS during the
same observation. Reassociation's separate 0x49 completion, real timeouts and
remaining AUTH/ASSOC/key producers remain open. The selected exact-image
GUI/security/S3/AP regressions below now pass, with their explicit limits;
the wider GUI matrix and IWM/IWX hardware qualification remain incomplete.
Release preparation is recorded below; the full functional goal is unchanged.

## Same-image GUI security regression

After an ordinary console login, System Settings was visibly usable. Selection
was by actual GUI Connect buttons, not a command-line join or kernel-state
setter. Each completed case below retained the same boot and loaded UUID,
validated IPConfiguration security and a real DHCPACK, then independently
awaited 20/20 1400-byte forward and reverse probes:

| GUI action, UTC | Actual result |
| --- | --- |
| 01:36:27, saved OpenWrt | WPA3 to WPA2-PSK; new private MAC/address 172.16.66.212; native DHCP publication 01:36:39.478 |
| 01:38:58, saved LabAP | WPA2 to WPA3-SAE; address 172.16.66.219; native DHCP publication 01:39:06.999 |
| 01:40:33 off, 01:40:54 on | Off readback confirms power Off, inactive carrier and no IPv4; automatic saved WPA3 DHCP publication 01:41:03.566; no second toggle or manual selection |
| 01:44:35, saved AIAM-UIF3-OPEN | Security NONE, external hostapd AUTH/ASSOC/AUTHORIZED without PMF, external DHCPACK 01:44:44 for 192.168.73.26 |

The open BSS appeared after normal navigation away from and back to the Wi-Fi
page; it was absent in the earlier visible list. The host fixture remained
beaconing throughout. No radio toggle was used to discover or join it.
These tests do not qualify all new/saved profiles or post-sleep GUI behavior.
Open-network forward RTT reached 971.686 ms, despite all 20 replies; this is
service recovery rather than a low-latency pass.

Evidence prefixes in the private root are `peer97-gui-wpa2`,
`peer97-gui-wpa3`, `peer97-gui-off-on` and `peer97-gui-open`, each with separate
forward/reverse logs and the corresponding GUI screenshots/watchers.
Complete security/off-on native log `peer97-gui-security-native.log` SHA-256:
`f1177b202c13468b050cf8cd5937bb1878b689d9a5513ed38fb7865aed6e81cc`.

## Same-image real S3 and Ethernet-free open-network recovery

The working open-network case then underwent actual S3. Direct Wi-Fi SSH was
verified before removing the exact temporary USB Ethernet and tablet while
awake. QEMU inventory then contained only the boot keyboard; the guest guard
independently rejected any remaining en0/en2 or QEMU tablet before requesting
sleep at 01:46:36 UTC. The request returning was not counted as sleep: serial
ACPI SLEEP and QEMU `paused (suspended)` observed at 01:47:15 prove the edge.

One owned-monitor wake at 01:47:46 produced ACPI S3 WAKE. Without another join,
toggle, or USB Ethernet, direct Wi-Fi SSH returned and showed the same boot,
same loaded image and same open profile. External DHCPACK at 01:47:48 and
native publication at 01:47:48.538 confirm a real lease after wake. Separate
20/20 forward/reverse 1400-byte tests completed over Wi-Fi while QEMU still
contained only its boot keyboard. USB management was restored only afterward.

The framebuffer continued to display a stale 04:47 clock, so this is network
service recovery, not a passed post-S3 GUI interaction. No display/daemon reset
was used to relabel that limitation. The open fixture's normal stop restored
host managed Wi-Fi at 01:49:50; the guest automatically returned to saved WPA3,
with native DHCP publication at 01:50:08.750, without another selection.

Private evidence: `peer97-s3-state-watch.log`, `peer97-wake-wifi-watch.log`,
`peer97-s3-open-forward.log`, `peer97-s3-open-reverse.log`, external
`peer97-open-dnsmasq.log`, and complete `peer97-open-s3-native.log` SHA-256
`903ff7e0e2474a288a43b024952fb6241e4806b2049e3538bb559535e66872c6`.
AP operation after this wake is a separate regression gate below; these STA
results do not establish automatic AP-client continuity through sleep.

## Same post-S3 image: native AP security and stop regressions

Native Internet Sharing used the restored independent en2 upstream, service
`44D1179D-0ED9-4D93-88B6-BC8163F2E0A7`. The existing system-framework helper
configured and enabled WPA3, WPA2 and open sharing sequentially through the
ordinary configd/airportd/InternetSharing path. It did not rewrite bridge
membership or replace those daemons. The host AX211 was the real client.

All three modes independently completed external-client DHCP (192.168.2.2),
20/20 1400-byte client-to-guest probes, bridge-scoped cold-ARP reverse 10/10,
and an HTTP 200 transfer routed through the guest upstream with exact payload
comparison. Client supplicant state proved SAE plus required PMF for WPA3,
WPA2-PSK for WPA2, and NONE for open. Each normal disable at 01:52:23,
01:54:56 and 01:57:28 UTC was followed by a 15-second dwell, absent bridge100,
zero retired-bridge I/O references and independently checked current STA
traffic at 10/10. The actual restored STA address was 172.16.66.212, not the
old WPA3 profile address. No guest reboot, radio toggle or manual STA selection
separated the modes. The wired host management route was preserved throughout.

Evidence prefixes under `/tmp/aiam-roam-policy.Kj1vgE`:
`peer97-posts3-ap-wpa3`, `peer97-posts3-ap-wpa2`, `peer97-posts3-ap-open`.
The per-file SHA-256 manifest in the private evidence root is
`peer97-ap-evidence-sha256.txt`, hash
`ab682715881b90cee9f9df305734156f2d8ec430576b9afc41117f4191117d3d`.
The complete 13,858-line `peer97-serial-through-regressions.log`, hash
`40eb701e275fe11c912c0ed4f7888cdffc7a767d98e8a4f013fd7399b247bc92`, has no
matched panic, firmware fatal/error, device timeout, unset-key or AP TX-gate
text. This is bounded runtime evidence, not proof of absent latent defects.
The isolated HTTP server served all three exact-payload requests and reached
its normal 420-second bounded lifetime (timeout exit 124) after the transfers.

These are AP operation-after-S3 and stop regressions, not active-AP client
continuity through S3 or simultaneous STA data service via Internet Sharing.
All new hardware qualification is IWN/6235; the IWM/IWX firmware paths retain
source/executable-test/build coverage, not equivalent new hardware evidence.

### Post-S3 GUI wait localized, not fixed

WindowServer's existing log enters displayDidWake at 01:47:46.022 but does not
show its main display notification returning. A bounded three-second sample,
after reading those logs, places all 2,272 main-thread observations at
`displayDidWake -> IOFBAcknowledgeNotification -> IOConnectCallMethod ->
mach_msg2_trap`. The request to open another Settings pane did not update the
framebuffer. Background WindowServer threads and networking remained active.
This localizes the visible wait to the framebuffer acknowledgement path; it
does not identify the kernel-side wait owner or exclude every driver/power
dependency. No graphics-driver change, daemon kill or reset was performed.

- `peer97-windowserver-native.log` SHA-256:
  `b6fed0c3f990c9cae3875e47f3007f11b0744efc9b333ab1341d0f8fa1c1b2e9`.
- `peer97-windowserver-posts3.sample.txt` SHA-256:
  `1560890bc39cf19cdaba0d21e70cf9d00a31f664c7f122fb3f85ba81e3f0a3e6`.

## Prepared release artifact

The exact frozen/installed bundle was copied, normalized and zipped in an
isolated stage; extracted Mach-O and Info.plist match their installed sources.
The prepared ZIP SHA-256 is
`c2dd6a085d4929d733fa77f385bc0a460160caaba6f885b5405f916b368ba622`;
its Mach-O retains SHA-256 `560a3f5b...` and UUID `69D766FF...` above.
The previous public b5 artifact was independently downloaded and verified at
`release-before-peer97/AirportItlwm-Tahoe-v2.4.0-alpha.kext.zip`, SHA-256
`c2940702f7de42e797659bc59a36a0908756c35c219b9196eea19bd123815b44`.
Publication and readback remain pending at this source checkpoint.

## Verified space reclamation and recovery copy

Only the old offline July-24-d qcow2 was removed locally, after successful
byte-copy rsync, independent source/remote hash and length comparison, a
read-only owned-image backing census with no child reference, and final fuser
check with no open user. Its small companion files remain local.

- Original: `/home/dima/Projects/itlwm/pmf-live-20260724d/tahoe-pmf-runtime.qcow2`.
- Archive: `dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/sae-peer-space-20260911.58o3vp/`.
- Length: 1,186,856,960 bytes; SHA-256
  `964b02e833e6c2e2a0f1f75166c372218528f5468680527392a56e2bf011a41c`.
- Unchanged backing file: `/home/dima/Projects/itlwm/tahoe.qcow2`.
- All four archive files, including OVMF variables, attestation and serial log,
  independently match their source hashes. Restore the qcow2 to the original
  absent/unused path with rsync -aS, then recheck the hash before use.

The current guest was subsequently shut down normally. Serial confirms APFS
unmount and power-off; QEMU PID 1047654 is gone and its disk has no open user.
Its now-stopped current image still contains loaded/installed `4d979f3b`:

- Disk: `aiam-iwn-night-runtime.wCIvBU/tahoe-night-candidate.qcow2`, length
  1,580,531,712 bytes; SHA-256
  `b447ba844b37db50972a62582135884ea4353ced91465fb858284a9cb2b1e046`.
- OVMF variables: `aiam-iwn-night-runtime.wCIvBU/OVMF_VARS.candidate.fd`, SHA-256
  `492e589198a6fcf3d5b6cc1d56c96bfda31c4b152a121cd80508bce2ae1f8e8a`.
- Verified working-image archive:
  `10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/working-4d979f3b-20260911.dycr30/`.

The byte-copy rsync completed successfully. Independent remote disk/OVMF hashes
and disk length match; local hashes were checked again while stopped and still
match. Only then was the same owned guest started with a new serial log,
`serial-sae-peer-candidate.log`, to stage the new candidate. Restore both disk
and variables from this archive only while this exact VM is stopped, preserving
the existing backing chain. The older local read-only parent and base are
unchanged. No physical user host,
unrelated VM, user-owned Build directory or shared filesystem setting changed.

### Additional verified offline archive before GUI/S3/AP regression

GUI startup grew the candidate child and reduced local free space below the
fixture's 1.5-GiB admission threshold. Only the old offline July-24-e qcow2
was removed locally after a complete rsync byte copy, independent remote and
local SHA-256/length comparison, a fresh owned-image backing census with no
child referencing it, and final fuser verification with no open user.
The current working image, read-only parent and base were not changed by this
space operation. Small July-24-e companion files remain locally.

- Removed local file: `pmf-live-20260724e/tahoe-pmf-runtime.qcow2` under the
  `/home/dima/Projects/itlwm` control root; length 1,254,490,112 bytes.
- Verified archive:
  `10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/sae-peer-regression-space-20260911.3OKFrk/`.
- Disk SHA-256:
  `e094ce46731bd8a10efd1c685ab1c161c26b3ef1d1b73a4096a556689d30cee1`.
- OVMF SHA-256:
  `807ab84edb1514729990671d6d37fdde216712e1261dfb6eda86730aa1f8d63e`.
- Serial SHA-256:
  `aaf0d3d29fe97a6e0d879542308f1b0dc84c40df2f0488878f93c0422a8523bf`.
- Attestation SHA-256:
  `6ba51c78fec8a720590077d4401494c3c0782acf69dc3d83e87c8de00eb1b133`.

All four remote files match. Restore the qcow2 by rsync -aS to its original
absent/unused path and recheck its hash before use; its backing remains the
unchanged `/home/dima/Projects/itlwm/tahoe.qcow2`. ZFS accounted the reclaimed
space asynchronously; subsequent free space was 2,082,078,720 bytes.
