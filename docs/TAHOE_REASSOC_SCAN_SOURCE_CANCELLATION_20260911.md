# Reassociation scan stranded after source loss — 2026-09-11

## Real failure and reference

On bb142d93, UUID `7BE91101-7829-3D70-8BDC-BD39C8850E3A`, boot
`C17BA719-545D-4127-B44A-8632DF5C52E0`, removing the controlled WPA2 AP
after real S3 and an off/on check stranded automatic return to saved WPA3
LabAP. Physical host 10.90.10.22 was not touched. Evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-auth-beacon-runtime.yIJOEP`.

The existing serial log shows accepted WCL reassociation scan followed by
real firmware beacon loss. Subsequent joins repeatedly return
`REASSOC_SUPERSEDE_BUSY error=16`. Read-only 45-second DTrace runs establish:

- q1: owner serial 6, active 1, leaf 2 (SCAN_STARTED), common flags 0x5a0c0801.
- q2: actual `iwn_wnm_bgscan_abort` sees sc_flags 0x1cf, without its 0x200
  BGSCAN bit, and returns EBUSY before the abort reservation helper.
- q3: source association epoch 59, current epoch 220; a real physical
  terminal succeeds, then common WCL-foreground completion (mode 2, tag 0)
  is rejected while owner 6 remains active.

All three observers ended normally with zero DTrace errors. q1's lower-abort
wildcard did not match the actual `iwn_wnm_bgscan_abort` name; q2 corrects it.
q2 printed the bool physical-terminal return as a full integer, whose upper
bits are unspecified; q3 uses uint8_t. Only q3 is used for that bool result.
Offsets come from disassembly of the exact loaded UUID, not guessed layouts:
ic owner 0x628, source epoch 0x630, active 0x648, phase 0x64c, flags 0x14cc,
current epoch 0x14f0, softc pointer 0x28; softc flags 0x2530. No kernel writes,
forced state changes, manual reconnect or radio reset occurred during these
observations.

The updated Ghidra checkout on 10.7.6.112 was independently verified clean
at `5995e24caa83fa74d841c0cfc7c24f59773c3459`, branch
`feature/bounded-function-parallel-decompiler`. Its saved 25C56 export of
`WCLRoamManager::linkDown` at `ffffff8002105ae4` clears pending roam state
and invokes timer/policy cleanup. `WCLNetManager::linkDownInd` at
`ffffff80020edee8` handles the independent firmware beacon-loss indication.
These are logical lifecycle facts, not evidence that Intel firmware has
already completed a scan. No additional decompile was needed for this fix.

## Correction

Source-epoch cancellation now retires only that source's logical scan-phase
reassociation owner, under the existing selected-BSS leaf before any yielding
revocation callback. SETUP, SCAN_STARTED and SCAN_FAILED qualify; target
switching and on-air reassociation retain their own terminal lifetime. Exact
source epoch, active serial and monotonic sequence must match. The common
BGSCAN markers and request values are cleared, but the sequence and historical
accepted-scan receipt are preserved.

No physical scan lease, command, DMA ownership or completion is fabricated or
cleared. Existing lower abort/terminal handling remains authoritative. A late
tagged scan result cannot select for a new request. No additional 0xcf or
synthetic on-air result is published: the source cancellation/leave/link-loss
path already owns its notification. This common correction covers IWN, IWM
and IWX source cancellation, without claiming new MVM hardware evidence.

## Executable verification so far

The fixture compiles the complete actual epoch-cancellation function, scan
owner cleanup and scan-completion admission, alongside the existing real
carrier/BSS replacement functions. Kernel leaves, crypto, lower firmware and
controller transport remain explicit doubles. Linux ASan/UBSan passes 130
cases, including stale/missing identity, each pre-target phase, preservation
of post-scan phases, repeated cancellation, late physical tags, and a new
owner admitted by a revocation callback. The ordinary aggregate passes.

The unchanged bb142d93 epoch function, substituted at its original location,
fails the old-owner-retirement assertion (exit 134). Initial negative-harness
ordering and out-of-range byte fixture constants were corrected; their
failed logs remain, and are not counted as semantic regression evidence.

Logs (SHA-256):

- green q3: `06d26216ecf7d359495f84229b5d2c40ecba605bcdcbaad4d87c0649b9f9f26a`
- unchanged baseline q3: `b1dd8d5ceaa18b0d477d3e185b45705b31b904dc0d44d5a46164b187cf267928`
- aggregate q1: `e9de6ec1d3509fda8b9d517aaee57cdd69164e5c53f158341a2ccbddb916926d`
- live observer q1: `6eaec0b1a25440e477a939e95bebea722a66e25e5a55c428defe2520c585a68d`
- live observer q2: `cdb3edd2b41f1dc397a7b846d57e6e7176d102f65d9e54305127153e20eb0cc5`
- live observer q3: `dd80be10eecb87273d1c676b6091319a045ac7017c723c6feaeced131e04fb6d`

macOS ASan/UBSan also passes all 130 cases. The full 353-file predecessor
manifest was verified before the exact five changed source/test files were
copied to the build mirror, then the full candidate was verified. Ordinary
AP-capable Tahoe build succeeded; all 1085 undefined symbols resolve against
the guest BootKC and no `_thread_call_cancel_wait` dependency exists.

- source manifest SHA-256:
  `351889e0d098a0586b8423ece8ddd1206b9949e133422d1d6b51ac8aafb54520`
- embedded source ID: `351889e0d098`
- Mach-O UUID: `412FE602-291C-3683-877A-823470E766D1`
- Mach-O SHA-256:
  `883a37a204ec4039307d4f9a8a8e65da2d8d0fbc25ca5348e103e0f62ce7428c`
- build/macOS-test log SHA-256:
  `47f16bd99efbc3a7ae09fc70eedd924303a3a3e53952a0979f216544634fd7ad`

Real failure/recovery qualification remains required. Do not promote this
source/build correction to a public release before those checks.

## Exact-image runtime follow-up

Source commit `52951b81` was pushed before activation. The owned QEMU
PID/name and writable lab overlay are unchanged; the latest working 17b copy
and all backing images remain untouched. Runtime evidence for this candidate:
`/home/dima/Projects/itlwm/aiam-reassoc-source-runtime.sE0SPZ`.

Private preflight passed with five AuxKC members and no canonical mutation.
The first activation invocation incorrectly named the source snapshot instead
of the preflight snapshot; it failed its path check before mutation. The
corrected invocation returned READY at 11:31:19 UTC. Ordinary guest reboot
loaded UUID `412FE602-291C-3683-877A-823470E766D1`, boot
`D53B731D-A453-49CF-80A0-46E6DD3E0956`. Native boot auto-join reached
WPA3/SAE and DHCP. The unsigned private candidate is not a signing claim.

| 1400-byte ICMP observation | Forward | Reverse | Maximum RTT, ms |
| --- | ---: | ---: | ---: |
| Boot WPA3 | 20/20 | 19/20 | 59.412 / 34.271 |
| Native WPA2 selection | 20/20 | 20/20 | 64.946 / 118.914 |
| WPA2 after radio off/on | 20/20 | 20/20 | 114.588 / 18.309 |
| Automatic WPA3 return after WPA2 AP removal | 20/20 | 20/20 | 59.551 / 1077.238 |
| Native open-network selection | 20/20 | 20/20 | 119.742 / 172.847 |

Boot's lost reverse packet and the 1.077-second return-path outlier remain
recorded; this is not a claim that all data-path loss/latency is fixed.

The WPA2 fixture was removed and host restoration finished at 11:37:00.
At 1789126621503035113 ns the exact new helper saw scan owner 3,
SCAN_STARTED, source epoch 36 while cancellation advanced to 37. Its return
at 1789126621503040096 ns shows active=0, serial=0, BGSCAN cleared. The
following scans were admitted normally, and WPA3 DHCP's lease began at
11:37:08 UTC. No post-failure manual network selection or radio toggle was
issued. After the open fixture, WPA3 also returned automatically (DHCP lease
11:40:10). Fixture host restoration returned zero and the wired default route
was unchanged in both experiments.

The 300-second read-only observer ended with zero errors, SHA-256
`21407ecd788f567c585ba018ee75a02b8be0453a28e772228df7b754db6eb77b`.
No `SUPERSEDE_RESULT=16` was observed. Its field offsets were rechecked
against this exact image. One preliminary symbol spelling had length 44
instead of 46 and produced no helper disassembly; the corrected result is
retained separately as `cancel-layout-q2.log`.

The predecessor's frozen stuck-state evidence manifest is
`a2af19313a9e6d0cf2dfbb00dfc4a1fde42888699213b6ff1994f4196ddb2e6e`.
Its missing-permission disassembly attempt was empty; a separate sudo
read-only extraction was added afterward and is bound by this candidate's
`predecessor-evidence.sha256`. The original manifest was not overwritten.

## Same-image S3 and repeated source-loss recovery

The diagnostic USB Ethernet was removed while awake, and direct Wi-Fi SSH
independently verified its absence. A sleep request at 11:41:26 UTC reached
actual ACPI S3 at 11:41:56; the owned QEMU was observed `paused (suspended)`.
One monitor wake at 11:42:36 ended 40 seconds of actual sleep. The boot UUID
and loaded image were unchanged. Saved WPA3 DHCP started at 11:42:42, without
another network selection or radio toggle. Both 1400-byte traffic directions
completed 20/20 before diagnostic USB restoration; maximum RTTs were
68.856 / 32.120 ms. The first immediate post-wake SSH attempt timed out before
DHCP; the later successful readback is not substituted for that failed probe.

The same post-S3 boot then repeated native WPA2, off/on and controlled AP
removal. No reboot separates these observations:

| Post-S3 1400-byte ICMP observation | Forward | Reverse | Maximum RTT, ms |
| --- | ---: | ---: | ---: |
| Native WPA2 selection | 20/20 | 20/20 | 90.838 / 91.316 |
| WPA2 after radio off/on | 20/20 | 20/20 | 35.522 / 22.094 |
| Automatic WPA3 return after WPA2 AP removal | 20/20 | 20/20 | 82.224 / 57.723 |

At 1789127208437255861 ns the helper retired scan owner 9, SCAN_STARTED,
source epoch 132 after advancement to 133. Return at 1789127208437260053 ns
shows active=0, serial=0 and BGSCAN cleared. The host fixture finished restoring
at 11:46:48 UTC, and the guest's automatic WPA3 DHCP lease started at 11:46:54.
No post-failure manual join or radio toggle was issued. The 180-second observer
ended normally with zero errors and no `SUPERSEDE_RESULT=16`, SHA-256
`08528b2fc314a558bc818f73e8b69580538e21feb5e728e4c2016cb9494d40b9`.

This closes the reproduced stranded logical scan-owner failure on actual
IWN/6235 before and after S3. It does not close all candidate policy, timeout,
MVM physical-retirement or data-path loss/latency issues. The read-only VNC
capture after S3 reports that the guest has not initialized the display;
pmset also records WindowServer's 30-second sleep acknowledgement timeout.
Thus the Wi-Fi results do not qualify post-S3 GUI availability. No graphics
or daemon reset was used to relabel this limitation.

The terminal STA evidence, scripts and a frozen serial checkpoint are bound by
`evidence-sta-posts3.sha256` in the runtime root, manifest SHA-256
`298eccf4a8307ae21a85906cf050c2aa57ea672907a7a4d86bd3d665d6187f57`.
GUI and AP-role regression of this candidate remain required before public
release promotion. Physical host .22 and unrelated VMs remain untouched.

## Same post-S3 image: native AP regression

Without another guest reboot, native Internet Sharing configured WPA3, WPA2
and open APs using the existing CoreWLAN/SystemConfiguration helper. The
restored independent en2 was upstream; AX211 on the host was the external
client. No bridge membership rewrite, daemon replacement or kernel-state
setter was used. Each mode negotiated the expected security and received
192.168.2.2 by DHCP. WPA3 readback explicitly shows SAE, pmf=2, BIP, group 19.

| AP mode | Host-to-guest, 1400 bytes | Cold-ARP guest-to-host | Maximum RTTs, ms | Native disable, UTC |
| --- | ---: | ---: | ---: | --- |
| WPA3 | 20/20 | 10/10 | 45.532 / 116.732 | 11:58:22 |
| WPA2 | 20/20 | 10/10 | 41.072 / 100.512 | 12:00:29 |
| Open | 20/20 | 10/10 | 32.310 / 102.571 | 12:02:44 |

All modes also completed HTTP 200 through the guest's upstream with exact
payload comparison. After each ordinary disable and a 15-second dwell,
bridge100 was absent, its retired object had ioref=0, and automatic STA
restoration reached 172.16.66.219 with 10/10 current-address traffic. Maximum
STA RTTs were 148.333, 5.178 and 7.360 ms respectively. All three controllers
and host-profile restorations returned zero; the wired default route did not
change. Observer stderr was empty and the bounded data observers ended.

These qualify AP operation after S3 and sequential stop/start for this exact
IWN image, not client continuity with an active AP across S3, concurrent STA
data service or new IWM/IWX RF behavior. The new serial interval has no matched
panic, firmware fatal/error, device timeout, unset-key or AP TX-gate report;
three AMFI messages explicitly marked non-fatal are retained.

The external evidence prefixes are
`/tmp/aiam-roam-policy.Kj1vgE/scan-source-posts3-ap-{wpa3,wpa2,open}-q1`.
Their manifest `ap-external-evidence.sha256` has SHA-256
`8e7db62a3c149f7f12de353730c484b9812fe2df86e6139a637167e65620c9c8`.
The terminal AP checkpoint in the runtime root is
`evidence-ap-posts3.sha256`, SHA-256
`635ca4840934fe38184cc8a14fb33ef977e077d2cc3fb8dc244e6d4d8ac070fb`.
The still-bounded HTTP server log is excluded from that frozen checkpoint.

A three-second read-only WindowServer sample independently places all 555
main-thread samples in displayDidWake -> IOFBAcknowledgeNotification ->
IOConnectCallMethod -> mach_msg2_trap. This agrees with the prior qualified
image's display wait, but is not a proof of its kernel-side cause. Sample
SHA-256: `94e6d2b0d4cb742e5aa39412a293fbf593d254b42cb47804e46aa9f7b98c4dba`.
Post-S3 GUI remains unqualified. Next is cold-boot GUI regression of this same
image before public release promotion; no physical-host operation is needed.

## Cold GUI follow-up and host storage interruption

Ordinary lab guest reboot at 12:04:49 UTC loaded the same image into boot
`DFF0EF81-9D9E-4C28-9D37-8A8259BB1B7B`. WPA3 auto-join obtained a new DHCP
lease at 12:05:57. After normal console login the Wi-Fi Settings page was
visibly usable. A temporary USB tablet was added at xhci.0 port3 only for
these GUI actions; it was absent during the preceding S3 qualification.

The actual GUI Connect button for saved OpenWrt was clicked at
12:08:56.405 UTC. WPA2-PSK DHCP began at 12:09:03 with address172.16.66.212.
Read-only state checks and separate 1400-byte traffic probes passed20/20 in
both directions, with maximum RTT257.705 /219.291ms. The settled screenshot
shows OpenWrt Connected. No command-line join or radio toggle initiated this
selection. Evidence prefix: `gui-wpa2-q1`, plus `gui-wpa2-action.log` and
the before/clicked/settled screenshots in the same runtime root.

The GUI LabAP Connect button was then clicked at 12:10:15.258 UTC. Host storage
became full during that trial. QEMU independently reports `paused (io-error)`
and MacHDD `I/O status: nospace`. This is an interrupted test, not a passed
WPA3 GUI transition or evidence of a Wi-Fi kernel panic. The owned VM remains
paused until adequate capacity is restored; physical .22 and other VMs are
untouched. The public artifact remains unchanged.

### Recoverable storage reclamation

Archive directory on `dima@10.7.6.112`:
`/home/dima/Projects/itlwm-runtime-archive/gui-source-space-20260911.aHWtXZ`.
Only the following old local artifacts have been removed so far, after exact
remote length/SHA-256 comparison, repeated unchanged local SHA-256, and an
empty final fuser check:

- `/home/dima/Projects/itlwm/aiam-runtime-recover-58063b5/AirportItlwm-iwn-sta-dvm-ampdu-wip27.zip`,
  length15552985, SHA-256
  `3dcb43a168d17b2629d615b92cc8f433f49b1b55a10085b934cf0292cefa6630`.
- `/home/dima/Projects/itlwm/overlays/overlay-live-bb7366b-20260721T012732Z.qcow2`,
  length787218432, SHA-256
  `5cc7c8eb0633b8465c66ac4ed26c6a34536a1190828833f3576b96781f3b43fc`.
  The clean offline image has no child in a fresh373-image census, including
  canonicalization of backing paths. Final census SHA-256:
  `360e2ba35c111de063740a943c77afe7d8ecf87f403d05acb1cec3718d3c022c`.

Both exact basenames are retained in the archive. Restore with an ordinary
file copy to the original absent/unused path and recheck the listed hash.
The qcow2's original backing `/home/dima/Projects/itlwm/tahoe.qcow2` was not
changed. ZFS eventually reported about427MB available; this is enough for
evidence but not a safe VM qualification margin.

The larger July26a archive transfer remains in progress; its source has NOT
been removed. Its verified process was temporarily SIGSTOPped while the small
overlay copied, then SIGCONT resumed the same process without restarting or
discarding partial work. The retained working17b copy and all backing disks
remain local and unchanged. A separate recovery note was saved in the remote
archive as well as the runtime root, so the full-filesystem interval does not
leave the procedure dependent on the live conversation alone.

### Storage recovery completed

The original large transfer returned zero. Its remote full length11876827136
and SHA-256 `7e9b841dfd6b1c9fa9945beff6746b3ede9c3236b00e102360a3cd3b95e62eec`
match the independently repeated local hash. A fresh372-image canonicalized
backing census found no child; final fuser was empty and qemu-img reported a
clean, non-corrupt image. Census SHA-256:
`bf18c3cb7f5cde7e588397d0f726c4eb0a12bbc2c1522efa307c057d54bdc91c`.
Only `/home/dima/Projects/itlwm/iwn-sae-pmf-lab-20260726a/tahoe-pmf-runtime.qcow2`
was removed at12:57 UTC. Its exact basename and three independently verified
companion files remain in the same remote archive; local companions, working17b
and every backing disk remain untouched. Host available space became7.27GB.

The exact owned QEMU resumed and cleared its block I/O error. After the long
pause, Wi-Fi was inactive and the GUI was gray. The preserved serial tail
includes firmware missed-beacon/reset recovery and association status40;
this interrupted interval is not counted as a clean reconnect experiment.
A normal guest reboot then loaded the unchanged candidate in boot
`1FDDC5CE-5121-473D-85C7-40660E9336DD`. WPA3 auto-join obtained DHCP at13:00:15.
Initial1400-byte traffic was19/20 in both directions, maximum549.761/683.251ms;
the losses remain recorded and are not a passed zero-loss gate.

Actual GUI selection of saved WPA2 at13:04:04.757 obtained DHCP at13:04:11,
172.16.66.212, with20/20 in both directions (maximum347.921/239.908ms).
Actual GUI return to saved WPA3 at13:05:27.336 obtained DHCP at13:05:36,
172.16.66.219, with20/20 both ways (maximum276.447/220.925ms). No CLI join or
radio reset initiated those selections. GUI off at13:06:13.542 was separately
read back as Off/inactive. GUI on at13:06:59.417 automatically restored WPA3
DHCP at13:07:07, but traffic was19/20 forward and20/20 reverse; retain that
failed zero-loss gate separately from restored service.

The controlled open AP appeared in Known Networks and was selected by its
actual Connect button. DHCP at13:09:30 assigned192.168.73.26; security readback
was NONE and1400-byte traffic passed20/20 in both directions, maximum
151.706/132.765ms. Separate GUI off at13:10:25.847 was read back as Off/inactive.
GUI on at13:11:09.531 automatically restored this same open profile with DHCP
at13:11:11; traffic passed20/20 both ways, maximum171.185/79.962ms. No explicit
network reselection followed either on action. The open fixture received an
exact-controller TERM at13:12:27; its trap completed normal restoration with
result0 at13:12:32. The controller's130 exit records that intentional TERM,
not a cleanup failure. Host AX211 returned to172.16.66.226 and the wired
default route remained unchanged. No post-S3 GUI claim is added.

Without another join or radio toggle, WPA3 DHCP returned at13:12:40. The first
post-removal traffic check was18/20 forward and20/20 reverse, maximum
333.201/192.845ms. This proves restored service but is another failed zero-loss
gate, not a seamless failover result. A frozen6702-line interval of the new
boot has no matched driver panic, firmware fatal/error, device/watchdog timeout,
unset-key, TX-gate or supersede-busy diagnostic. Its SHA-256 is
`e37b1c5661f0a4aef7ab42c5941a3a1b0ac11db9f4a9d987b0659409b034d6c1`.
The full interrupted-boot serial checkpoint remains separately preserved.
After an ordinary quit of System Settings, a separate settled check passed
20/20 both ways, maximum117.513/31.101ms, without another join, toggle or reboot.
This single comparison does not establish why the preceding packets were lost.

All353 production-manifest files were reverified in both canonical source
and the macOS build mirror before packaging. The complete private preflight
bundle byte-compares equal to the installed bundle; a normalized ZIP extracted
into a separate stage also compares equal. Its Mach-O remains UUID412FE602
and SHA-256883a37a2 above. Prepared ZIP SHA-256:
`0978d13439d8907614a1ea2af2baa79ebfeee808d12ab9f71efdb6573163b93a`.
The previous public ZIP was independently downloaded and matches its recorded
`c2dd6a08...` hash. Preparation is not publication; the public asset is still
unchanged at this checkpoint.

The140 terminal cold-GUI, storage-recovery, readback and packaging evidence
files are bound by `evidence-gui-cold.sha256`, SHA-256
`709289ed0d89814c8c43f8f33e7636a46babd1a8979e85a0513599f5cbc5d692`.
Its entries were checked again after sealing. This does not replace the
separate pre/post-S3 STA and AP manifests or erase any earlier failed probe.

## Published exact-image release

After the qualification documentation was pushed as31b0b97f, the existing
`v2.4.0-alpha` Tahoe ZIP was replaced at2026-09-11 13:17:07 UTC. Its size is
15,681,473 bytes, and its release title identifies production52951b81. The
published ZIP was independently downloaded into `release-readback-source529`
and byte-compares equal to the prepared ZIP. ZIP SHA-2560978d134 and extracted
Mach-O SHA-256883a37a2 match the full identities above. The previous public ZIP
remains recoverable in `release-before-source529`, SHA-256c2dd6a08 above.

Release notes explicitly retain all observed loss, incomplete post-S3 GUI and
IWM/IWX RF qualification limits. The new comeback-deadline prototype is not
in this published bundle. No physical-host installation, physical-host reboot,
tag relocation or unrelated-VM mutation accompanied publication.
