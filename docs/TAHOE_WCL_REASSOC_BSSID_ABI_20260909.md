# WCL reassociation BSSID carrier — 2026-09-09

## Evidence and correction

The complete 25C56 NetAdapter `sendReassocCommandLegacy`, `V1` and `V3`
decompilations in `aiam_applebcm_reassoc_25C56_20260801/reassoc.txt` copy six
bytes per entry from public offset `0x64` into the firmware BSSID field/list.
They use the first BSSID even when the public count at `0x90` is zero; counts
of two or more select the bounded list. Channels are a separate array at
offset zero with their count at `0x94`. These are not score/channel tuples.
The older incomplete BootKC exports are not evidence for this conclusion.

On loaded `35589ce0`, a real `WCLNetManager::setROAMWithBssid` request supplied
one broadcast BSSID and no channel restriction. The old driver interpreted
the address as a score and channel 255, filtered it against an idle AP owner's
primary channel, armed an AP handoff and acknowledged a no-op. No AP operation
was active. The raw observation is recorded in the AP-intent note.

The public and common carriers now preserve BSSID bytes. Exact BSSIDs form an
allowlist; broadcast and unspecified addresses admit a wildcard within the
associated ESS, subject independently to channels and prune RSSI. Actual
target admission still validates rates, security and required PMF even when
the discovery-side desired SSID is empty. A valid WCL target may replace the
old desired BSSID/channel pin. Eligible targets use observed RSSI, never MAC
address bytes as a fabricated score.

The AP-intent gate from `3b14777c` is retained. A real single-channel AP
lifecycle constrains only the channel array, without rewriting BSSID entries.
An incompatible explicit channel request returns busy before disarming the
initial BSSID pin. Wildcard and addressed requests take the real bounded
background-scan path; the old empty-request/no-op successes and their false
AP-handoff producer are removed. A no-target result belongs to the existing
asynchronous failure path, not a synthetic successful reassociation.

## Verification boundary

The UBSan test compiles production carrier declarations, the full WCL
producer, AP intent/controller queries, candidate disposition and BSS matcher.
It covers zero/one/oversized counts, the observed broadcast carrier, explicit
and multiple BSSIDs, independent channel filtering, source/other-ESS exclusion,
RSSI pruning, lifecycle constraints, and real admission rejection of open,
wrong-AKM, missing-PMF, wrong-group-management-cipher and invalid-rate targets.
The ordinary unfiltered scan-export behavior remains separate.

The common internal request layout changed; diagnostics must use matching
build DWARF, not offsets from an earlier kext. The public size remains `0x9c`.

This fix does not itself prove seamless roaming, forced same-BSSID
reassociation, every reference feature flag, reliable discovery of the
missing 5-GHz BSS, or the full repeated-public-join/GUI matrix.

## Runtime qualification of `9040aa2b`

The full Tahoe build resolved all 1083 BootKC symbols. Private AuxKC admission
passed without canonical mutation, followed by transactional activation with
the same four companion members. The disposable IWN/6235 guest rebooted and
loaded UUID `53207465-749C-3229-82A6-3E9EB798433F`, Mach-O SHA-256
`05ccada93fd70030f007ac6539253718060b42c63bc18a25f36766f359ba25a5`.
Saved WPA3 association and DHCP recovered automatically; traffic passed 5/5.

A normal radio OFF/ON was followed by an unmodified userspace wildcard WCL
request at 14:25:25 UTC. FBT captured the public broadcast BSSID, the identical
common BSSID and zero channel constraint. The request started actual 13- and
24-channel firmware scans. The real candidate matcher admitted two calls and
rejected 23, a target was chosen at 14:25:30, and the serial log recorded
driver-resident SAE retarget and `TARGET_RUNNING`. This is actual reassociation,
not just scan acceptance. A subsequent ten-packet check passed 9/10; it is not
claimed lossless. An immediate probe at radio-ON had failed to bind before
the DHCP address returned and is not counted as a successful connectivity test.

The first credentialed public selection completed in 19 seconds. Three
consecutive subsequent selections completed in 10, 9 and 10 seconds, with no
printed join error and 5/5 source-bound packets after each. These are ordinary
`networksetup` operations, not proof of every GUI interaction. The first trace
also observed foreground selection superseding a just-started WCL scan; the
later uninterrupted wildcard observation above is the evidence for completion.

A role-7 pure-SAE/required-PMF AP then ran alongside the primary WPA3 STA.
An external AX211 completed SAE group 19 with BIP on the required shared
channel. Static-address traffic passed 20/20 client-to-AP, 5/5 AP-to-client
and 5/5 primary-to-gateway. `pmset sleepnow` at 14:27:45 reached actual S3:
the owned QEMU was suspended and the serial console recorded `ACPI SLEEP`.
The owned monitor received wake at 14:28:53, followed by `ACPI S3 WAKE`.

After wake, boot epoch and loaded UUID were unchanged, both roles were active,
and explicit client reselection completed SAE/PMF again. The same three traffic
checks passed 20/20, 5/5 and 5/5. AP stop retained primary traffic at 10/10.
A further credentialed selection after sleep and AP stop completed in 13
seconds and passed 10/10 packets. This qualifies service recovery, not seamless
or automatic AP-client continuity, and does not replace the older DHCP AP
matrix. The temporary AP address and client profile were removed and the
host's normal Wi-Fi profile restored; wired management was unchanged.

Release archive SHA-256:
`90365ab1867ebcad8af14e7a17600b6eb2bf8ae8d6b765eb62a33aa1f406165b`.
The archived Mach-O matches the frozen, loaded candidate. Missing-BSS scanning,
all reference roam flags, same-BSSID behavior and equivalent IWM/IWX hardware
qualification remain separate work. The physical user machine was not touched.
