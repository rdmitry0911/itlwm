# Node callback candidate: loaded SAE recovery — 2026-09-11

## Exact image and scope

Production `5aad4f68` is loaded on real passed-through IWN/6235. The earlier
built-only statements in the deferred-BSS report are historical checkpoints.
This run qualifies preservation of fresh-SAE failure/recovery on that image,
not the still-unimplemented complete deferred BSS/TX/roam lifetime.

- Boot: `D18B9FE6-E326-4562-921F-8BD541EB0F27`.
- Loaded UUID: `42D77FC7-BF18-3C78-A999-9276B718BF31`.
- Mach-O SHA-256:
  `5c1671cc3f1c2b2b18e89bba559ff5cd33e4f8fa985eef3357df25b7c233bf29`.
- Production content digest, rechecked after runtime:
  `99c9481b6c92bff2604747de2a81f4ad7518920627c6e9275c8e1dadaf9f65d7`.

The former `52dd156d` VM shut down normally. Its stopped image and UEFI
variables were hashed before creating a new child. After child start, the
parent hash remained unchanged and open-file inspection showed it read-only.
Private five-member AuxKC admission and transactional activation passed;
an ordinary guest reboot loaded the exact candidate. No kext unload, forced
reset, physical-host `.22` operation or unrelated QEMU change was used.

Current owned QEMU: PID `1193162`, `aiam-iwn-after-scd-control`, SSH `3338`.
Child/evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-node-callback-runtime.fZXeYZ`.
Disk: `tahoe-node-callback.qcow2`; variables: `OVMF_VARS.fd`.
Preserved parent:
`/home/dima/Projects/itlwm/aiam-iwn-roam-scan-runtime.pBJX7E/tahoe-roam-scan.qcow2`,
length 1,495,597,056, SHA-256
`707074f39f4e974b98cad09ac8583de6595431063a42d415e858046d981495d2`.
Its saved variables SHA-256 is
`b4bfe094b58c51d90137fea22c37633e9e16c7ca51f7e9a2f2c137b7f1cfcf77`.
Rollback must use another child of that preserved parent, not boot/write the
parent itself. The older qualified `97fe747c` parent is preserved too.

## Real failure/recovery and traffic

At 04:34:05 UTC, without console login or a Wi-Fi command, the candidate had
WPA3_SAE and DHCP `172.16.66.219`. Initial 1400-byte tests passed independent
20/20 forward and 20/20 reverse checks, maximum RTT 22.197/33.072 ms.
The initial forward result is retained as an explicitly labelled transcription
of terminal tool output; the later forward/reverse results are raw logs.

The host AX211 then provided the existing controlled wrong-password LabAP,
channel 9, BSSID `80:e4:ba:20:ef:fa`, pure SAE and required PMF. After 15 seconds
of actual beaconing, one native directed request was accepted at 04:49:31 UTC.
The unchanged framework helper did not set kernel state or force a new join
after failure. The 90-second read-only SAE observer ended normally with zero
errors, covering 07:49:13–07:50:44 local time (UTC+3).

| Local time | Actual result |
| --- | --- |
| 07:49:37 | Reassociation Confirm status-1 rejection, no fresh JoinAdapter generation; generic scan path remains |
| 07:49:50 | Fresh generation 2 receives the rejection; producer/lower/SAE acknowledgements 1/2/4; one failure publication; valid same-SSID SAE completes at epoch 37 |
| 07:50:20 | Another ownerless-for-fresh-join reassociation rejection |
| 07:50:28–29 | Fresh generation 4 receives rejection; acknowledgements 1/2/4 and one publication; valid same-SSID SAE completes at epoch 58 |

External hostapd records four Confirm mismatches. Native configd publishes
DHCP after both recoveries, including 07:49:50.702 and 07:50:30.453; the final
lease/readback records a fresh ACK/lease start at 07:50:32. The bad BSS is still
reselected: this is not candidate-policy closure or complete roam recovery.

The separate 115-second node observer records six actual `node_copy` calls
and **zero** `node_switch_bss` calls, with zero errors and normal termination.
This matches the source's pure-SAE bypass and prevents mislabelling this run
as an on-air exercise of the modified legacy callback. Those callback changes
have executed production regression coverage, not a completed legacy-roam RF
qualification on this candidate.

The fixture normally restored host managed Wi-Fi at 04:50:48 UTC. Its exact
controller/hostapd/dnsmasq processes are terminal; the wired default route is
unchanged. On the same boot/image, post-recovery 1400-byte traffic passed
20/20 forward and 20/20 reverse, maximum RTT 7.362/285.285 ms. The reverse
outlier means this is not a low-latency qualification.

The 7,019-line serial snapshot has no matched kernel panic, firmware
fatal/error or device-timeout signature. It does contain the existing AMFI
`[non-fatal]` boot diagnostics; a broad search for `fatal` is not a clean log.
All observers, traffic checks and the native log collection reached terminal
completion. No new-image GUI/S3/AP pass is claimed. Public `v2.4.0-alpha`
remains qualified production `97fe747c`, ZIP SHA-256
`c2dd6a085d4929d733fa77f385bc0a460160caaba6f885b5405f916b368ba622`.

## Evidence hashes

In the child/evidence root above:

- `callback5aad-auth1-native.log`, complete 19,434 lines:
  `3da69724526a99742ad4bd1a1c2763c9d610ef74d61d48024348a08c416b0900`.
- `callback5aad-auth1-node-observer.log`:
  `840da750ead024eb7be08bd4bf5334fffa51bcce415f18d4c5614681d5dc1dd2`.
- `callback5aad-post-recovery-forward.log`:
  `ed646af347fc563541f16485016c132ff9bbf563a4a00c7a3699830467ab91fa`.
- `callback5aad-post-recovery-reverse.log`:
  `5b929cb244521184d9e776bbade9f31fd285533f4efd921546aa37312891f28e`.
- `callback5aad-serial-after-auth1.log`:
  `1f63ef50f7acdd1ccfb20b68c817a9b7f630c0d3371f9caf515c4c108280976d`.

The SAE wrapper retains its unique-label outputs in the older night root:
`/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU/callback5aad-auth1-observer.log`,
SHA-256 `d7436a365c01f706d4ab61dd22c4eaba38ad6634db74d39c1423c775aca24133`.
The fixture/request logs there and hostapd log in
`/tmp/aiam-gui3e73-runtime.kiWiTP` retain the same `callback5aad-auth1` label.
The peer-event layout checker completed on Linux and macOS before the run.

## Verified recoverable space reclamation

Only this old offline owned overlay was removed locally:
`/home/dima/Projects/itlwm/overlays/overlay-live-286fe6f-runtime-20260721T052144Z.qcow2`.
Byte-copy rsync completed to
`dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/node-callback-space-20260911.82rqFE/`.
Independent local/remote SHA-256 and lengths matched:
`ca7235de72dd4cee9576a4517779ed6003114187b28aca16370d97f22eb62b5b`,
1,739,194,368 bytes. A fresh backing census found zero children, and final
fuser found no open user. Its unchanged backing file is the ordinary
`/home/dima/Projects/itlwm/tahoe.qcow2`; no working parent/base was removed.
The original path is absent. Restore with `rsync -aS` to that absent/unused
path, then verify SHA-256 before use. Filesystem reclamation was asynchronous;
the RF fixture's unchanged 1.5-GiB admission guard passed before any radio
mutation. Subsequent free space was about 2.4 GB.

## Continuation

The [new IWM/IWX physical-retirement execution](TAHOE_REASSOC_TX_RETIREMENT_20260911.md)
extends the actual IWN finding, including reset/free of ordinary STA frames.
Implement the full immutable accepted-roam/source-TX/lower lifetime across
all three families, plus the reference's real progress/AUTH/ASSOC/completion
edges. Preserve the published fresh-SAE recovery. Then test successful,
failed, missing-target and superseded roaming on air, including the legacy
callback path, followed by GUI/security/S3/AP and qualified release. The
functional objective is unchanged, neither complete nor blocked.
