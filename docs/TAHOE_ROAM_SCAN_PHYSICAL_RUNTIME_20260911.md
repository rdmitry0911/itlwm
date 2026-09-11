# Physical roam-scan candidate: loaded runtime — 2026-09-11

## Result and exact image

Production `52dd156d` is now loaded on the real passed-through IWN/6235.
The earlier built-only statements describe historical checkpoints. This is
bounded runtime qualification of the physical scan changes, not completion
of the accepted-roam lifecycle or a replacement public release.

- Boot: `19C91793-8DB8-4D91-A99F-91ACE16D902B`.
- Loaded UUID: `D63CF6F8-85F5-3C98-B3E6-7289BEFD5A19`.
- Mach-O SHA-256:
  `9c4b3c105a3b42eda305aa780323a412a30a84bdc88578e715f423546ab9faf8`.
- Production content fingerprint:
  `518de2096e613c03556ae86c7507f057dda89d65317285607cdabb10b2660767`.

Private AuxKC admission passed without canonical mutation. Activation
`/private/var/tmp/aiam-iwn-activation-roam-scan-52dd156d-1/activation-20260911T034756Z`
preserved the four companion kexts; an ordinary guest reboot loaded the exact
candidate. The first SSH banner timeout during boot was followed by a successful
read-only retry, not a forced reset. No kext unload was used.

## Real SAE failure, recovery and traffic

Without console login, GUI join or radio toggle, the candidate automatically
connected with WPA3_SAE and received DHCP `172.16.66.219` at 03:49:33 UTC.
Independent initial 1400-byte forward and reverse tests each passed 20/20.
The initial reverse maximum RTT was 1057.592 ms, so this is not a latency pass.

The owned AX211 AP used LabAP, channel 9, BSSID `80:e4:ba:20:ef:fa`, pure SAE
with required PMF and a deliberately different password. The native directed
request was accepted once at 03:53:57 UTC. The read-only observer covered
06:53:38–06:55:09 local time (UTC+3), terminating normally with zero errors.
Its fixed peer-event offsets were independently compiled against the current
production header on Linux and macOS before admission.

Actual Confirm status-1, empty-body rejections reached the in-kext engine:

| Local time | Owner/result |
| --- | --- |
| 06:54:02 | Reassociation, no fresh JoinAdapter generation; generic scan recovery remains |
| 06:54:15 | Fresh join generation 3; cleanup acknowledgements 1/2/4; one failure publication; successful SAE on valid same-SSID BSS `9a:fb:5d:97:a9:02`, epoch 38 |
| 06:54:45 | Another reassociation, again no fabricated fresh-join generation |
| 06:54:57 | Fresh join generation 5; acknowledgements 1/2/4; one publication; successful SAE on the valid BSS, epoch 61 |

External hostapd independently records four Confirm mismatches. Native DHCP
publications follow both recoveries, including 06:54:16.924 and 06:55:01.113.
The normal fixture cleanup restored host managed Wi-Fi at 03:55:16 UTC;
the wired management route remained unchanged. After revalidating the boot,
loaded UUID and real lease, independent 1400-byte tests passed forward 20/20
and reverse 20/20 (maximum RTTs 21.498 and 30.440 ms respectively).

This preserves the published fresh-AUTH SAE fix but still reproduces repeated
selection of the bad BSS. It does not establish that every adversarial
SCAN_ABORT race covered by executable tests occurred on air. The frozen
5,423-line serial interval has no matched panic, firmware fatal/error or device
timeout text; this is bounded evidence, not absence of all latent defects.
All observers, traffic checks and the RF fixture reached terminal completion.

No new-image GUI/security-switch/S3/AP qualification is claimed. The responsive
login screen was inspected, but not used as a GUI connection test. Public
`v2.4.0-alpha` remains qualified `97fe747c`, ZIP SHA-256
`c2dd6a085d4929d733fa77f385bc0a460160caaba6f885b5405f916b368ba622`.
IWM/IWX retain source/executable-test/build coverage, not new hardware evidence.

## Evidence

Root: `/home/dima/Projects/itlwm/aiam-iwn-roam-scan-runtime.pBJX7E`.

- `loaded-verified.log`:
  `44f33e7a3b17942e18a760b3cdd66a88c2f2b5a8dc47788870382ca9b26ec052`.
- `recovered-forward.log`:
  `ce95dce245c5ed2a022535a6b6ffa7aa8ef0941f11e8dfc6a27acfeff4102b8d`.
- `recovered-reverse.log`:
  `a63d334cbd6958b58b8dafbd685f06cfc2d67bd88c8d8e940fc4d248d7e2fcac`.
- Complete info/debug `rejection-native.log`:
  `00b25ed3f709a58ca591ad97063d7a38b6f4c7e0b51bb1cf8c9e837dc1b1f0d8`.
- `serial-through-rejection.log`:
  `6d1ce9e5902c57ddbab9c3f4f341b578909d2be7fde16e1dfea60fdeea056652`.
- Observer is in the preceding evidence directory
  `../aiam-iwn-night-runtime.wCIvBU/scan52-auth1-observer.log`:
  `b5cb45a3314f8b3bea3c5505503f704dd103943f56ec7b26c311a16be9d953c4`.

## Preserved rollback and recoverable space reclamation

The previous owned VM shut down normally before child creation. Its stopped
working `97fe747c` disk is now the read-only parent:
`/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU/tahoe-night-candidate.qcow2`,
length 4,210,360,320, SHA-256
`47ec38edc291967e35f0070a21d7302b1999767ae5a20848fd4d4051ae051f4b`.
Saved `OVMF_VARS.candidate.fd` SHA-256 is
`248d90935b86a2a38a7866d90f5671f31a0972f43277bae7ed8c62c5f3c5b9de`.
Both hashes were rechecked unchanged after startup. lsof proves parent read-only
and child read/write. New child is `tahoe-roam-scan.qcow2` in the evidence root;
only owned QEMU PID 1165548, name `aiam-iwn-after-scd-control`, SSH 3338 uses it.
On failure, stop only that VM and create another child of the preserved working
parent with copied saved variables. Do not launch the old script against the
now-preserved parent. Older parents, base disks and unrelated QEMU are untouched.

Two obsolete offline overlays were byte-copied to verified archive
`10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/roam-scan-space-20260911.gc3jTx/`
before their exact local copies under `/home/dima/Projects/itlwm/overlays/`
were removed. Each passed independent source/remote hash and length comparison,
a fresh backing census with no children, and final no-open-user checks:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `overlay-runtime-ba4a2f0-20260721T092212Z.qcow2` | 1,021,378,560 | `b4814a5928c282ac6a131055bfac5802f09f0377131899d8dc1a3be19ef8aba3` |
| `overlay-live-linkctx-20260721T003339Z.qcow2` | 1,325,137,920 | `174f5d60cbe65c4b3106bbd2c4c590381ee92fdd4d9c5b5044d0f88df3d23eb6` |

Restore by rsync -aS to the original absent/unused path, then verify SHA-256;
their backing remains unchanged `/home/dima/Projects/itlwm/tahoe.qcow2`.
No physical host 10.90.10.22, user-owned Build directory, filesystem settings
or unrelated agent resources were changed.

## Next boundary

The [deferred BSS lifetime defects](TAHOE_REASSOC_DEFERRED_BSS_20260911.md)
remain unfixed. IWN releases the node inside TX-data cleanup before clearing
the descriptor and before its caller decrements ring occupancy. Therefore an
immediate switch at refcount one could reenter physical cleanup too early;
changing only the reference threshold is not the implementation. Complete
owned lower retirement, deferred source-to-target handoff and the real
reference AUTH/ASSOC/key/progress/terminal lifecycle remain the same active
functional layer, followed by loaded roaming and GUI/security/S3/AP gates.
