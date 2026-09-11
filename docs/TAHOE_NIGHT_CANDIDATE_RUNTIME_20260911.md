# Night candidate: real failure/recovery and S3 — 2026-09-11

## Scope and exact loaded image

The previous status turn was read-only. This continuation loaded and exercised
the accumulated common/IWN changes in source `4d979f3b`, rather than leaving
the nightly build indefinitely untested behind unfinished IWM/IWX work.
This is controlled WIP qualification, not all-family closure or publication.

The built production fingerprint is
`81687f45ebd7c0b86708046af2d0b72e8f44b405b04ff0fc0bc17b2dd83b5ce5`.
Private AuxKC admission passed without canonical mutation; the four companion
kexts were preserved. Transactional activation completed at
`/private/var/tmp/aiam-iwn-activation-night-4d979f3b-1/activation-20260911T001904Z`.
The guest's subsequent normal reboot loaded:

- Boot UUID: `6CCFA730-B7E2-4E23-A4B5-CCA40BA6B447`.
- Kext UUID: `F23158D2-32F8-39A1-87CC-83E310634012`.
- Mach-O SHA-256: `94cfee4fd9d7742156722154c346da4d635773861ca979244ad0a510a1a36c59`.

The physical device is still passed-through IWN/6235. No IWM/IWX firmware
behavior is qualified by it. The physical user host `10.90.10.22`, unrelated
QEMU instances, base disk and public release were not changed.

## Recoverable working disk

The former working VM (PID 361779) was shut down normally. Serial confirms
APFS unmount and power-off; the QEMU process terminated. Its final overlay is
`/home/dima/Projects/itlwm/aiam-iwn-profile-reset.bhi2aO/tahoe-s3.qcow2`,
size 17,618,173,952 bytes, SHA-256
`ea72134127e8cabb6058564691e4daed9b8c6e93759cbafc73537786f3e5b1f9`.
The separate saved OVMF variables hash is
`44ae26e121a1e09644e2a2a871459041951abff6d1eded2bfbd973ca2e416796`.

Local free space did not permit a full copy. ZFS block cloning was disabled;
the shared host's filesystem parameters were not changed. A remote rsync to
`10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/last-working-iwn-20260911.LF3hrU/`
ran at approximately 5 MB/s and was explicitly stopped, with terminal exit 20.
That directory is NOT a complete or verified disk backup.

Instead, a new small qcow2 child was created above the stopped working image:
`/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU/tahoe-night-candidate.qcow2`.
The candidate also uses a separate copy of OVMF variables. The owned QEMU is
now PID 1047654, still named `aiam-iwn-after-scd-control`, with SSH port 3338.
`lsof` independently confirms the working parent is open read-only and the
new child read/write. The parent's SHA-256 was rechecked after child startup
and is unchanged. Its backing base remains `/home/dima/Projects/itlwm/tahoe.qcow2`.

Exact launch arguments, the pre-install command-line snapshot and old OVMF
variables are retained in `aiam-iwn-night-runtime.wCIvBU`. On a failed candidate,
stop only this VM and make another child of the preserved working parent with
the saved OVMF variables. Do not boot/write the preserved parent or delete the
base/other agents' disks. Local space remains constrained and must be checked
before another build, installation or long radio test.

## Initial WPA3 and controlled failed target

Without a GUI login or Wi-Fi toggle, the new image selected the ordinary
saved pure-WPA3 profile, completed SAE and obtained DHCP on channel 13.
IPConfiguration identifies `Security: WPA3_SAE` and a real DHCPACK. The initial
10-packet check received 8 replies; a background transition to the other BSS
also occurred in this period. This is not a clean loss-free initial gate or
proof that roaming caused both missing packets.

The owned host AX211 AP became ready at 00:21:24 UTC: same laboratory SSID,
channel 9, pure SAE/required PMF, but a deliberately different test password.
The host's wired management route was preserved. At 00:21:43 the existing
Apple80211 framework tool submitted its directed BSSID request successfully.
This is a real WCL request, not a GUI selection or a direct kernel-state write.

External hostapd observed two Commit/Confirm exchanges and two Confirm
mismatches. Both 30-byte authentication rejection replies (algorithm 3,
transaction 2, status 1, no variable body) received MAC ACKs, cookies `0x5d1`
and `0x5d3`. An ACK alone does not prove delivery to the host SAE engine.

Native logs record link-off at 00:21:53.799, WCL's associated network becoming
null at .813 and IPv4 withdrawal at .919. Thus the prior stale-WCL correction
survives this candidate: about 14 ms, not the old roughly 54-second stale state.
The subsequent saved-WPA3 attempt still selects the failing target and ends
with airportd's `-3905` error at 00:22:08.051. It does not demonstrate correct
progression to another valid BSS of that same profile.

Without a further join command, GUI action or radio toggle, the system then
selected the other saved WPA2 profile: association at 00:22:14.534 and IPv4
publication at 00:22:18.683. Recovery is about 24.9 seconds after link-off,
not a new fast-recovery improvement. A later source-bound 1400-byte check
passed 20/20 packets. The complete 70-sample watcher terminated normally.
The negative fixture was normally stopped and the host's managed Wi-Fi
connection restored at 00:23:52. Its expected signaled exit is 130.

## Actual S3 and automatic network recovery

Before sleep, the guest used WPA2 with working traffic. Hibernation mode 0,
the exact loaded UUID and boot epoch were checked. Temporary USB Ethernet was
removed while awake; the VNC tablet was not present. Both QEMU USB inventory
and the guest-side sleep guard confirmed their absence. The guard requested
sleep at 00:25:06; the VM did not suspend immediately. Subsequent serial
`ACPI SLEEP` and QEMU `paused (suspended)` establish actual S3. A transient
SSH timeout was not treated as a crash or grounds for restarting the VM.

Only the owned monitor received `system_wakeup` at 00:26:43. Serial reported
`ACPI S3 WAKE`. USB management was added only after wake, without changing
Wi-Fi state. The same boot UUID and kext UUID remained loaded.

The system automatically selected saved WPA3 instead of its pre-sleep WPA2
profile. DHCPACK/lease start is 00:26:56 and native IPv4 publication is
00:26:58.079. Independently awaited 1400-byte tests passed 20/20 guest-to-router
and 20/20 host-AX211-to-guest. This establishes tested network-service recovery,
not last-selected-profile restoration, GUI availability, AP recovery or
lossless continuity through sleep. No matching panic, firmware fatal or device
timeout appears in the captured candidate serial interval through these tests.

## Evidence and remaining next boundary

Private evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU`.

- `failed-sae-native1.log`: `f92157953a6917c31dbb37292b2c1428c5dd1269b4d45d4b39a747f46ee6133f`.
- `failed-sae-watch1.log`: `504de3453df1dcf5c879978f9f032769ff7fe1eed91829ba41d092c72b81ef35`.
- `failed-sae-hostapd1.log`: `29523a617b031529276437b9854daaf1720b3a1975e86454efa99ecd71e07b9c`.
- `serial-through-s3.log`: `7e87c2bbd81f1ea14569380620a09c7eda7e1954a999238c92046987b5d0551a`.
- `sleep-native2.log`: `b676ae34878e771e26954b5fda76873eca10eec28db3a7768e370dc8c564f5d7`.

The first post-sleep native-log capture was interrupted by a downstream
output limit and is explicitly retained as `sleep-native1.partial.log`, not
the complete capture. The second capture reached terminal success.

The next high-frequency functional boundary is actual AUTH failure and native
candidate progression. Current `ieee80211_wcl_join_fail` production ingress
is connected only to discovery/NO_NETWORKS. The IWN worker reduces both
`PEER_ABORT` and `PEER_AP_REJECT` to `fail=true`, then retires the engine and
requests SCAN; it does not preserve a candidate-scoped peer status for the
new failure ledger. This source gap does not alone prove where either real
rejection was lost.

The repeated controlled AP run started at 00:33:33. Another directed request
at 00:35:08 produced two externally confirmed SAE rejections, both MAC-ACKed
(cookies `0x5de` and `0x5e3`), followed by automatic WPA2 recovery. However,
the first read-only FBT observer had already ended at 00:35:01, before that
request. Its zero calls are not evidence of a missing software delivery.
The next observer ran 00:35:39–00:37:10, ending with zero diagnostic errors
but no engine calls. A `networksetup` join at 00:36:01 printed `Could not find
network LabAP` despite process exit zero and did not create an observed new
SAE exchange. It is explicitly not a successful selection or an on-air
negative control. Both observers are terminal, not still running. The second
fixture was signaled for ordinary cleanup at 00:37:08.

The observer and a C++ offset-check fixture are retained in the private
evidence root (`sae-failure-delivery.d`, `check-peer-offsets.cpp`). Exact peer
event offsets compiled against the production header on Linux and macOS;
the probes compiled against the actually loaded image. A correctly timed
repeat still needs to distinguish delivery/engine result from missing
terminal publication; do not infer it from these empty observation windows.

The exact 25C56 `handleAuth` and `sendConnectComplete` decompilations were
re-read: failure updates the matched BSSID/peer result and publishes 0xd3;
the separately owned completed join emits the exact 164-byte 0xd5 before
retiring its active ledger. Do not substitute a shorter timeout, invented
peer response, forced next BSSID or completion before real lower/SAE cleanup.
Full GUI/security/AP qualification and outstanding IWM/IWX TX-BA, reset and
AUTH lifetimes remain required. Public `v2.4.0-alpha` remains source `b5c6cfd8`.
