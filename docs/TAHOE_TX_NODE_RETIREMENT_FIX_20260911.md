# Physical TX/node retirement correction — 2026-09-11

Latest: production `1896beb2` is pushed, built and loaded on real IWN/6235.
The runtime below executes nonempty reset retirement, real S3 and received
SAE failures. Reverse loss during ordinary roaming remains; this is not a
fully qualified replacement release or complete accepted-roam closure.

## Scope and implementation

This corrects the executed physical failures in
[the all-family investigation](TAHOE_REASSOC_TX_RETIREMENT_20260911.md),
not the complete accepted-roam lifecycle. The preceding user-status turn was
read-only. Production changes now cover IWN, IWM and IWX.

Completed mbufs retain their existing node reference in a temporary packet
list, without allocating under a TX leaf or adding a large kernel-stack
pointer array. The descriptor packet/node pointers and metadata detach first;
the actual reclaim loop finishes its counters/cursors before releasing node
references. IWX releases them after unlocking the TX leaf. IWN aggregate-stop
and failed-start unwind pass the batch through the hardware stop, release NIC
access and finish old BA/node updates before delivering the last release.
IWN single completion still releases the old reference before the independent
WNM callback, which can copy the next BSS and its reference counter.

Reset/free now release ordinary STA references too, not just SAE-marked ones.
Ring reset and counters precede release; orphan references with no remaining
packet are included. IWX partial allocation/invalid queue reset cannot return
before software references are retired. Free clears the old descriptor/command
aliases before callbacks can see already-freed DMA memory.

This does **not** make a zero node-reference count a whole-hardware drain
proof. SAE terminal reporting retains its existing position. Full source TX
admission closure, command/BA/reset ownership, immutable deferred attempt
identity and always-asynchronous main-workloop handoff remain required.

## Executed verification before build

- Complete Linux payload aggregate passes, including the newly enabled full
  physical-retirement fixture and adjacent primary-station/BA/TVQM suites.
- macOS ASan/UBSan executes the same complete retirement/reset/free methods:
  ordinary controls and all eight previously failing physical requirements
  pass. Additional orphan/partial reset/free cases check callback-visible
  state too. Minimal packet/DMA/lock/scheduler substitutions are explicit
  fixture boundaries, not hardware layouts or on-air qualification.
- Actual IWN node-copy/release/TX/reset/free execution passes ten positive
  scenarios. The three remaining deferred-BSS identity/liveness requirements
  still independently fail with exit 134 on both operating systems. The
  pre-copy one-reference case remains limited to that boundary, not a claim
  about established STA reference counts.
- Adjacent IWN queue topology, STA aggregate stop, AP stop and all three SAE
  transport contracts pass on macOS. Historical-source fixture signatures are
  retained through explicit baseline/current API selection.

Linux log `/tmp/aiam-tx-retire-linux-full-20260911-r2.log`, SHA-256
`f804f959e10e90fc9c8538b901c934c5e22cf25e144c4b269b66c53eeda82c44`.
macOS log `/tmp/aiam-tx-retire-macos-selected-20260911-r1.log`, SHA-256
`181894b4de15932caeb8f7ea01a34dea91746ce177d4b684e52f61ade7417d11`.

Production content digest:
`563246c2a3c2642f2094ff1403758b766e9d4ba597e55b10738b74655ba09598`.
At this checkpoint the correction is not built or loaded. The live lab still
runs `5aad4f68` / UUID `42D77FC7-BF18-3C78-A999-9276B718BF31`.
Public release remains the qualified `97fe747c` image. User host `.22` and
unrelated VMs are untouched.

## Required continuation

Build the matched source, activate in a fresh owned child with the working
parent preserved, then exercise ordinary STA queue retirement via on/off and
real sleep/wake, plus SAE recovery and open/WPA2 legacy BSS switching. Follow
with GUI/AP/security regressions before qualified publication. Continue the
full reference-owned roam start/preparation/AUTH/ASSOC/completion integration;
do not redefine it as only this packet-retirement correction.

## Build, admission and preserved working parent

Local and guest production content digests match the value above. The Tahoe
build succeeds, with all 1085 external symbols resolved against BootKC and no
`thread_call_cancel_wait` import. Build log
`/tmp/aiam-tx-retire-build-20260911-r1.log`, SHA-256
`97de7d9143ab0271f3e866cf9757f16dd3baedabeaae03540b974c50056b0d20`.

- Mach-O UUID: `46BA6759-1E6A-35B3-A9B8-A90C66B09A69`.
- Mach-O SHA-256:
  `a0726d60e312c718ee60459f4ad0b3b5488238451bd18d66fe6a2ecbb6eab2c4`.
- Loaded boot: `F31CDD13-6EBC-470A-A76E-9E231395AB84`.
- Owned QEMU: PID `1225303`, `aiam-iwn-after-scd-control`, SSH `3338`.
- Child/evidence root:
  `/home/dima/Projects/itlwm/aiam-iwn-tx-retire-runtime.nFXV01`.
- Child disk: `tahoe-tx-retire.qcow2`; separate `OVMF_VARS.fd`.

The preceding guest shut down normally. Its stopped parent is
`/home/dima/Projects/itlwm/aiam-iwn-node-callback-runtime.fZXeYZ/tahoe-node-callback.qcow2`,
1,051,459,584 bytes, SHA-256
`aaeebb02cf6e9fa8b2a81eccead44a9c9bfd548b1efcb136ad4961c3ced42bab`.
Its saved variables hash is
`1ec71438ea56fcc160e36c87046ca33ea1d65e0ecdeb42fe00c675f730ce4b2e`.
Both hashes were rechecked unchanged after the new child started; lsof showed
the parent read-only. No working disk/base or unrelated VM was deleted.

Private five-member AuxKC admission passed without canonical mutation.
Transactional activation at
`/private/var/tmp/aiam-iwn-activation-tx-retire-563246c2a3c2-1/activation-20260911T053220Z`
preserved all four companions. Normal guest reboot at 05:33:08 UTC loaded the
exact image above. No direct kext unload or physical `.22` operation occurred.

## Actual off/on and nonempty TX reset

Initial automatic WPA3/DHCP completed at 05:33:46 UTC. Independent 1400-byte
checks passed 20/20 forward and 20/20 reverse (maximum RTT 19.889/17.681 ms).

One native networksetup Off was submitted at 05:36:47. The first wrapper
incorrectly required immediate IP removal after command return and stopped
before On. Readback confirmed Off, inactive carrier and no IPv4; the existing
cycle continued with one On at 05:37:53, without repeating Off. Automatic WPA3
IP publication followed at 05:38:01.317, with a real DHCP lease/BOUND afterward.
The wrapper now waits for the asynchronous Off edge. This was not a GUI test.

The 150-second observer completed normally with errors=0: 20 ring resets,
131 node-owned packets appended/released, 143 drain calls, **one actual
node-owned packet/reference retired inside reset**, two node copies and zero
legacy BSS-switch callbacks. The first observer failed D compilation before
any radio action; explicit accumulator initialization fixed that setup.

Forward traffic passed 20/20, but reverse was **18/20**. Native logs record
another same-SSID Wi-Fi roam at 08:38:36 local time. Temporal proximity is not
proof that roaming alone caused the losses; the loss is retained, not replaced
by a later successful test.

## Actual S3 and reference observation

Direct Wi-Fi SSH was verified before removing only the temporary USB Ethernet
while awake. The guest guard and QEMU inventory showed no Ethernet or tablet,
only the boot keyboard. Sleep was requested at 05:40:38 UTC; subsequent ACPI
SLEEP and QEMU `paused (suspended)` prove actual S3. One monitor wake at
05:41:36 produced ACPI S3 WAKE. No join or radio toggle followed wake.

Automatic saved WPA3 obtained a fresh DHCPACK/lease at 05:41:47 and native
IPv4 publication by 05:41:49.121. Direct Wi-Fi readback retained the exact
boot/image. While Ethernet was still absent, 1400-byte traffic was **20/20
forward, 19/20 reverse**. Another native Wi-Fi roam occurred at 08:42:15 local.
USB management returned only after both checks completed.

The exact built node object's DWARF independently locates ni_refcnt at 0x28.
A separate bounded observer across S3 records 3311 release entries, **zero
zero-reference releases**, 20 resets and two actual node copies, both with
destination refcount zero. It ends normally at 08:42:49 with zero errors.
This does not prove all concurrent/legacy source owners are drained. An
earlier DWARF diagnostic was interrupted by the planned candidate reboot;
the subsequent complete read, not that attempt, supplies the offset evidence.

No new-image GUI or active-AP-through-sleep qualification is claimed.

## Same post-S3 image: actual SAE failure/recovery

The controlled wrong-password LabAP was admitted after the host's unchanged
space/route/ownership checks. The host AX211 supplied pure SAE/required PMF;
one native directed request was accepted at 05:45:08 UTC after 15 seconds of
beaconing. The observer covers 08:44:50–08:46:21 local, ending with errors=0.

- At 08:45:24, fresh generation 4 receives Confirm status 1; cleanup edges
  1/2/4 and one successful failure publication precede valid-BSS SAE epoch 67.
  Native DHCP publication follows at 08:45:25.624.
- At 08:46:03, fresh generation 6 follows the same failure/cleanup/publication
  sequence; valid-BSS SAE completes at 08:46:07, epoch 90; DHCP publication
  follows at 08:46:08.697.
- Reassociation's generation-zero failures at 08:45:14 and 08:45:55 still
  use generic scan. External hostapd records four Confirm mismatches, so bad
  candidate reselection remains, not a closed policy layer.

Fixture cleanup completed normally at 05:46:24 and restored host managed
Wi-Fi with the wired route unchanged. Independent post-recovery traffic passed
20/20 each way. This later success does not erase the earlier reverse losses.

An ensuing native OpenWrt selection without a supplied password returned
`Failed to join ... -3900` despite process exit zero; the guest automatically
returned to saved WPA3. A read of the saved system credential was denied
(exit 36) before another join was issued; no secret was printed or permissions
changed. These are not a passed WPA2/legacy roaming test or a proven driver
regression. Final readback at 05:49:51 has the same image, WPA3/DHCP `.219`,
management `10.0.6.15`, with no active observer/build/fixture process.

## Runtime evidence and next implementation

Selected SHA-256 records in the child root:

- `off-on-retire-observer-r2.log`:
  `4b057d126b425e05154f6ef0bf3c57567602f0e7aa5f8796bcd08dbfd44e2aa4`.
- `s3-refcount-observer.log`:
  `97caf7df95a54375a68f36e4ecbf27f42572532b2c19310a5ee70b3b18371868`.
- Complete `native-through-off-on.log` (2,617,835 lines):
  `05ab0c84807169095ef6cbeec70d03d73262f2ae0112b3afea7577cc7cbd8260`.
- `s3-native-dhcp.log`:
  `5acd7468947213680430279d0dde89399d0b9a324aa7964b4e181c839fdd9fc6`.
- `sae-posts3-native-dhcp.log`:
  `c639b52c15ad0d417847aac8cddacb7364f1dd2779657eab1a7525521520cda4`.
- `sae-posts3-forward.log` / `sae-posts3-reverse.log`:
  `3f853eb0a324b1a5576b013504f5d0d5dd3fa1fdc939e2494bf511870de28eeb` /
  `2ad799d1c94861c8dc5d5bf5faaa4d7efc55d1ca0f198ffb2e93a39faaf7a771`.
- `serial-through-recovery.log`:
  `38bc812e04359ba0488480d93171ac616308941d18d2a7dab8cdb78f271b89a5`.
  No matching kernel-panic, firmware-fatal/error or device-timeout signature;
  ordinary AMFI/other boot diagnostic text is not represented as an empty log.

The SAE wrapper's observer remains in the preceding night root as
`tx1896-posts3-auth1-observer.log`, SHA-256
`f56380ed25014916bb6ad7a830bdc482f8c7421fe4cf3a2c03721aa6c110a28b`.

The reference's actual Core/WCL roam-preparation and reassociation handlers
were re-read in this cycle. Source preparation is a real lifecycle edge,
separate from reassociation status and overall completion. Next implement
immutable deferred attempt identity and asynchronous full-source retirement
across IWN/IWM/IWX, including terminal-before-arm and successor admission.
The three red common scenarios remain enabled. Obtain real open/WPA2 legacy
callback evidence and successful/failed/missing/replaced target roaming, then
the full GUI/security/S3/AP gates. Public release remains `97fe747c`; this
candidate is not promoted while qualification is incomplete. The full active
functional goal is neither complete nor blocked.
