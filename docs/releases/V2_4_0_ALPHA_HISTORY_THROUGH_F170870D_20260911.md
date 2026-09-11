# Historical v2.4.0-alpha release notes through f170870d

This is an archive of the public release body captured before the SAE
peer-response retry update. Statements of "current" and artifact identities
below are historical, not assertions about a later release.

Source: https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha
Release ID:357137705. Captured title:AirportItlwm Tahoe v2.4.0-alpha (f170870d).
Last update:2026-09-11T21:29:48Z. Prior asset ID:558047145,
ZIP SHA256:187a1a65cdc1e0fa5ce94c8ba067094c6e9ce057a94559c9cb0ae6d4845779cb.
The captured metadata and prior archive are also retained locally for rollback.
The body is moved here because accumulated history approached GitHub's release
notes size limit; old results and limitations must not be silently discarded.

---

## Current alpha: owned STA AUTH RXON and PMF liveness recovery (2026-09-11)

**General WPA3/SAE roaming and zero-loss data service remain unqualified.** One post-sleep native roam failed to discover its requested BSS and stayed on its source, despite preserved data service. Four other transitions completed their first AUTH attempt but retained 1–3 lost packets per 250 in a direction. The final restored-link check failed its strict zero-loss gate (20/20 forward,18/20 reverse); later diagnostics also retained19/20 and249/250 forward results. A separate250/250 bidirectional control does not erase those failures. The old persistent post-RUN AP/STA state divergence is not claimed eliminated: the new SA Query path provides authenticated liveness checking and bounded recovery. This is a limited alpha checkpoint, not seamless-roaming or full reference-parity certification.

Production changes: `f170870d` and `a40adf3d`. [AUTH/RF qualification](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_IWN_SINGLE_AUTH_RXON_20260911.md), [STA SA Query design and prior controls](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_STA_SA_QUERY_RECOVERY_20260911.md).

IWN now uses one identity-owned unassociated **target** RXON for ordinary STA AUTH/RUN-to-ASSOC, followed by its existing station restoration and real command/fresh-beacon fence. It no longer sends an unowned old-channel reset immediately beforehand. INIT/SCAN cleanup is preserved. Shared STA PMF handling challenges unprotected reason6/7 only for the current protected association; queries are protected, bound to actual IWN/IWM/IWX descriptor submission, and responses are accepted only after CCMP/replay verification. An unanswered submitted challenge produces one ordinary native WCL leave/reconnect, not a fabricated received-deauthentication event.

Exact installed/tested archive, packaged without rebuilding:

- Mach-O UUID: `68F5B0D9-4863-3627-B30C-CC0CC111BB58`.
- Mach-O SHA-256: `7e368e43f9d2e968cf41f7512a57b0813d5d4443d10ea7244469a78604615f4d`.
- ZIP SHA-256: `187a1a65cdc1e0fa5ce94c8ba067094c6e9ce057a94559c9cb0ae6d4845779cb`, 15,688,990 bytes.
- 356-file production manifest: `26cd66f64bc8430161f52a041adf0de601589c53375d28b78e1d37dec57f778e`.

The complete payload regression passes; the40 actual AUTH-owner/lower-function scenarios pass on Linux and Tahoe. Hardware/IOKit boundary doubles are explicit. Full Tahoe build resolves all1088 imports; private admission and transactional installation preserve the exact five-member AuxKC set. Frozen preflight, installed, packaged and extracted bundles compare equal.

Actual IWN/6235 controls on this image:

- Native ca/channel9 to02/channel13 and reverse each issue one target RXON, accept all three command replies and a fresh beacon, then SAE/RUN on the first AUTH attempt. Readiness takes41.9/80.2ms; traffic is249/249 and249/248 of250. The return also has one duplicate.
- Real S3 is independently observed with diagnostic USB Ethernet/tablet absent. Same-boot WPA3/DHCP and20/20 bidirectional1400-byte traffic return before USB restoration. A later post-S3 roam first fails discovery and preserves250/250 source traffic. A separately recorded additional pair passes target AUTH in284.1/50.1ms with247/247 and249/249 traffic. The earlier discovery failure is retained.
- Post-S3 guest AP WPA3, WPA2 and open each pass external AX211 security negotiation and DHCP,20/20 forward,10/10 cold-neighbor reverse, exact118-byte HTTP routing through guest USB/NAT backhaul, normal stop/bridge disappearance and10/10 restored STA traffic. This is not simultaneous STA-radio backhaul or active-AP continuity through sleep; no private ifnet reference-count claim is made.
- Native WPA3 off/on, STA WPA2/open/WPA3 selections pass DHCP and20/20 traffic both ways. An earlier newly compiled CoreWLAN helper received privacy-redacted SSID/BSSID and requested network(null); airportd rejected it with-3900. The system's ordinary signed networksetup client was used instead, without changing privacy grants. WPA2's later wrapper stopped after successful RF/DHCP/traffic while a live script was being edited; the original wrapper failure and AP-side key/DHCP receipts remain recorded. These are not mouse-driven GUI tests.

Exact-image SA Query: the healthy AP returned a CCMP/PN-verified response43.8ms after the real query TX commit, with no reconnect and20/20 traffic each way. Removing only the fixture's station state produced five protected queries, one timeout after1.025s and native WCL leave; fresh SAE/RUN followed3.773s later without off/on. Native policy selected saved LabAP, not the original fixture. The original stream lost all30 packets across the profile/address change; recovered gateway traffic was20/20. This is recovery, not continuity or guaranteed same-profile selection.

Packet diagnostics retained the failed checks. A fixed later control passed250/250 each way; both endpoint captures contained1000 identical ICMP packet identities, no duplicates or capture drops. Its250 forward requests each matched one real firmware TX success. No loss occurred in that window, so it does not identify the cause of earlier losses. An earlier observer with zero packet-identity coverage is explicitly not counted as a valid firmware result.

The evidence identity is recorded in the linked qualification report. Remaining work includes intermittent target discovery and SAE timeouts, residual loss/duplicates/latency, repeated bad-BSS selection, full GUI/profile/security/sleep combinations, active-AP sleep continuity, concurrent STA/AP backhaul, ad-hoc, throughput/interoperability and physical IWM/IWX qualification. Source/test coverage of those HALs is not IWM/IWX radio proof. Physical host10.90.10.22, other QEMU instances and shared base disks were not modified.

---

### Earlier checkpoint history

The archive identity above supersedes all older artifact identities below. Older failure observations are retained as history, not attributed automatically to this new image.
### Additional failure observed after publication —18:40UTC

**Persistent loss of the data path can also follow a successful SAE/association.** In the next bounded diagnostic series, the second02->ca transition completed both SAE peer phases and reached target RUN, but received only68/250 forward and70/250 reverse packets. Native DHCP subsequently remained in INIT/no-server and assigned a169.254 link-local address while WiFi still reported WPA3/active. The next planned roam was cancelled to preserve the failure for diagnosis; no reboot/off-on was used. This is a failed functional result on this exact image, not a passed roam, and its cause is not yet established. The archive below is unchanged.

## Current alpha: bounded passive roam discovery (2026-09-11)

**Known serious limitation: general WPA3/SAE roaming is not closed.** One planned transition on this image failed its first SAE authentication after about4.51seconds, temporarily lost IPv4 and recovered only through subsequent macOS reconnection/roaming. It received203/250 forward and208/250 reverse packets. The final connected state is not counted as a successful original request. A separate final post-AP STA check received20/20 forward but19/20 reverse and failed its strict zero-loss gate. Do not treat this alpha as reliable seamless roaming or full reference parity.

Production correction: `ee501d78`; Darwin fixture portability: `cdb2850c`. [Implementation and exact-image radio qualification](https://github.com/rdmitry0911/itlwm/blob/a7750d4b/docs/TAHOE_PASSIVE_REASSOC_DISCOVERY_20260911.md).

For tagged explicit IWN reassociation, the driver now permits one additional passive visit per originally admitted non-DFS2GHz channel without a successful CRC receipt, under the same scan lease. It preserves the original dwell/home budget, forbids active probing on passive channels, then resumes normal5GHz scanning and delivers one upper completion. Cancel/reset/source-owner checks reject stale work. Ordinary scans, AP/PAN/BTM and IWM/IWX scan policy are unchanged. This adapts the measured Intel DVM fragmented-visit limitation; it does not claim to duplicate Broadcom firmware internals.

Exact tested archive:

- Mach-O UUID: `DCCB5E44-25AE-30BE-930F-8975BFC10A6D`.
- Mach-O SHA-256: `976c2addfe9dcc9d161f83502d999350da218f8f5ed699ba1fa4a0ed88d94f16`.
- ZIP SHA-256: `0f17c3bf2d7a54942fa8677b9a46cc18c3a6e48a9792a601c75e77720f002040`,15,685,533bytes.
-354-file production manifest SHA-256: `7c82cbd8d19c16c2e74784360bc74b07d6a51f5f69ac748ddcf4815fc2fbbe33`.

65,563 extracted-production retry/ownership scenarios,19 reused abort cases, Linux/macOS fixtures and the final full payload aggregate pass. The full Tahoe build resolves all1085 imports. Private AuxKC admission, transactional activation and normal lab-guest reboot passed; sealed, installed and extracted complete bundles compare equal. This archive packages the already tested image without a rebuild.

Real IWN/6235 observations:

- Three planned ca->02 returns completed. Two independently observed an initial channel13 visit with no fresh target frame, then actual fresh target reception only during the new standalone channel13 command. The same serial continued through5GHz and one upper terminal, then protected source departure, SAE/association and target RUN. Those returns received249/248 and248/248 of250 packets, with1 and2 encapsulation drops: not lossless roaming.
- The failing reverse transition noted above issued no passive retry. The shared SAE core is unchanged from the prior image and has no sent-Commit/Confirm peer-response retry timer; that is a plausible recovery gap, not yet proof of the exact lost peer frame. Exact phase observation/retransmission is the next layer.
- Native WPA3 off/on recovered DHCP and passed20/20 packets each direction. Real98-second S3 was witnessed as QEMU suspended with diagnostic USB Ethernet/tablet absent. Same-boot WiFi DHCP returned6seconds after wake; a later WiFi-only check passed20/20 each direction without off/on or reboot. The premature first SSH probe timed out before traffic and remains documented.
- After sleep, native Internet Sharing open, WPA2 and WPA3 each passed external AX211 negotiation, DHCP,20/20 forward,10/10 cold-neighbor reverse,118-byte HTTP routing, normal sharing stop/bridge retirement and10/10 restored STA gateway traffic. WPA3 negotiated SAE with mandatory PMF. HTTP uses guest USB/NAT backhaul; this is not concurrent STA-radio upstream or continuity of an already-running AP through sleep.

Still open: intermittent SAE response-timeout recovery; transition/post-AP loss and latency; complete mouse-driven UI/saved-profile/security combinations; AP-through-sleep continuity; duplicate delivery and broader interoperability/throughput; physical IWM/IWX qualification. The refreshed Ghidra5995e24caa produced a read-only40-function scan export using40 workers. Frozen573-file evidence manifest: `ccf4652625c5d2b608d3292590547f9193a81fab9c1995f961865f8365aedc40`. Physical host10.90.10.22, unrelated QEMU instances and shared base disks were not touched.

---

### Earlier checkpoint history

The current archive identity above supersedes all historical artifact identities below. Earlier observations and limitations are retained as history.

## Latest qualified alpha: protected SAE source departure (2026-09-11)

Production correction: `986e030b`. [Implementation and exact-image qualification](https://github.com/rdmitry0911/itlwm/blob/718a868f/docs/TAHOE_SAE_ROAM_SOURCE_DEPARTURE_20260911.md).

Non-BTM IWN same-ESS SAE roaming now submits a protected deauthentication on the source BSS and waits for that exact management descriptor's terminal before changing the association/key context and starting target SAE. A pointer-free, monotonic identity rejects stale/duplicate completion and survives radio-stop ticket history; credential, source epoch and upper roam owner are revalidated at the actual doorbell. The already-fenced BTM path is unchanged. This is not a userspace SAE replacement or a claim about Broadcom firmware's hidden on-air sequence.

Exact tested image:

- Source manifest SHA-256: `65b657d797b571541bc1445039b67a2df111183a7a7f8cf879e9a8b7ca9ce6ff`.
- Mach-O UUID: `ADABFCB9-0EE6-3FE0-AD81-01CDF616226B`.
- Mach-O SHA-256: `043c6e748cebe4ce2390df82e3428cb48121add50b4eddc0cf7d866640a21768`.
- ZIP SHA-256: `fddbebf1e662ffea7f98bf27e08c481648f7109f681034d27681ed5bf1109fac`, 15,684,441 bytes.

The Linux/macOS actual-production-function tests, full aggregate, complete AP-capable Tahoe build, 354-file source-manifest checks, all 1085 external symbol resolutions, private AuxKC preflight and transactional lab activation passed. Frozen, installed and extracted full bundles compare equal; this archive was packaged without rebuilding.

Real IWN/6235 evidence:

- Successful transitions between the two WPA3 BSSs show protected source-deauth firmware TX status, exact descriptor retirement, then target SAE/association/RUN. No status-30 comeback was seen in those samples. A 250-packet transition received 249 forward / 248 reverse, with one encap drop: this is not seamless roaming.
- A clean post-S3 transition used monotonic departure ticket 13 and reached the requested BSS without comeback, with 249/250 forward, 247/250 reverse and two encap drops. Native WPA3 off/on recovered DHCP and passed 20/20 1400-byte traffic in both directions.
- Real S3 lasted 172 seconds with diagnostic USB Ethernet/tablet absent. Same-boot WPA3 DHCP returned eight seconds after wake; direct Wi-Fi SSH worked before management Ethernet returned. Both strict zero-loss runs failed: 20/19 and 19/20 out of 20. The loss observations are retained, not explained away by recovery.
- After that sleep, native Internet Sharing open, WPA2 and WPA3 each completed external-client DHCP, 20/20 forward, cold-ARP 10/10 reverse, exact HTTP payload routing, normal sharing stop/bridge retirement and 10/10 restored STA traffic. WPA3 independently negotiated SAE and mandatory PMF/BIP. HTTP uses the guest USB/NAT upstream, not concurrent STA Wi-Fi backhaul.
- First WPA3 AP HTTP failed because the bounded host fixture server was absent. First open AP received all 20 replies plus one duplicate; its parser stopped before HTTP. Both original failures remain documented; the full repeats completed, with no duplicate in the open repeat.

Still open: intermittent discovery of channel-13 target BSS (one failed fresh census had 23 nodes, all ni_fails=0, but no target that the host could see), transition/post-sleep loss and latency, the complete mouse-driven UI/saved-profile/security matrix, WindowServer sleep acknowledgement timeout, already-active AP client continuity through real sleep, duplicate delivery, broader interoperability/throughput and physical IWM/IWX qualification. This update qualifies the narrow IWN source-departure correction, not complete WPA3/AP/reference parity. Physical host 10.90.10.22 was not touched.

The 280-file verified evidence manifest is `f3106c820bffd438911c0d0db25b192f31dc8bda5a4eec7b881923a942e04d8d`. Later bounded command tracing also reproduced a 27-node no-target census with channel 13 explicitly present as passive / 85 ms; that failed roam preserved 250/250 traffic both ways. Invalid-alignment faults from an earlier packed-field observer are retained separately and are not counted as a clean observation.

---

### Earlier checkpoint history

The artifact above supersedes all older artifact identities below. Their historical evidence and limitations are preserved.
## Latest qualified alpha: AP comeback minimum deadline (2026-09-11)

Production correction: `c9715b63`; pointer-free contract/comment repair: `1f14e709`.
Full [qualification report](https://github.com/rdmitry0911/itlwm/blob/8d5221d9/docs/TAHOE_ASSOC_COMEBACK_MINIMUM_DEADLINE_20260911.md).

An AP status-30 comeback interval is a minimum elapsed delay, not just a number of shared one-second watchdog ticks. The driver now arms a bounded monotonic deadline, preserves the exact association identity across deferral, and revalidates the deadline before retry submission. IWM/IWX also check admission before firmware protection work. This does not replace the existing one-second timer with a precision timer or close every deferred MVM lifecycle race.

The unchanged production watchdog fails both early-phase direct/deferred negative tests; the corrected actual production functions pass 120 cases under Linux and canonical macOS ASan/UBSan. The normal aggregate, full Tahoe AP-capable kext build, all 1085 external-symbol resolutions, full 353-file source-manifest checks, private AuxKC preflight, and transactional lab activation passed.

Exact image:
- Source manifest SHA-256: `4ec402769fb5dec3146a0b26f838d0307fa0a24724179502c29878ba56fe2646`
- Loaded Mach-O UUID: `BE0C8CE1-4924-39F2-BE2B-5929C1039113`
- Mach-O SHA-256: `e3d8139f121fb138ad0a71091a4c0e3b06ec6404dde24d3ee0dc6de0e5551091`
- ZIP SHA-256: `feb561fc09360d9ddbea0d513f1d24b32f6108f9589b8e0a5253f0799a2f35ec`
- ZIP size: 15,681,564 bytes. The frozen preflight, installed, and extracted full bundles were compared; the tested image was packaged without rebuilding.

Real IWN/6235 observations on this exact image:
- Two actual WPA3 BSS transitions received status30/1000TU. Production retry submission occurred after 1.075541811 s and 1.103180254 s, both beyond the AP's 1.024 s minimum. This is a real driver-submission measurement, not a monitor-captured OTA timestamp. Traffic was 242/250 and 241/250, then 243/250 both ways; losses remain.
- Real Normal Sleep lasted 112 s, independently witnessed as QEMU suspended. Diagnostic USB Ethernet and tablet were removed. Same-boot WPA3 DHCP returned 7 s after wake and Wi-Fi-only 1400-byte traffic passed 20/20 both ways. The initial immediate SSH probe was unavailable and is retained.
- Starting native Internet Sharing AP after that sleep passed open, WPA2, and WPA3. External AX211 obtained DHCP; WPA3 independently negotiated SAE and mandatory PMF/BIP. Each mode passed 20/20 forward, 10/10 reverse after cold ARP, exact HTTP payload routing, normal sharing stop, bridge retirement, and 10/10 restored STA traffic.
- Same post-S3 image passed native WPA2 selection and same-profile off/on with 20/20 both ways. Removing that AP exercised the actual prior scan-owner retirement and restored WPA3 with 20/20 both ways. Open selection passed 20/20; its subsequent automatic WPA3 recovery retained a failed zero-loss gate: 19/20 forward, 20/20 reverse.
- A separate cold boot passed actual System Settings mouse selection of WPA2, WPA3, and open, each with DHCP and 20/20 both ways. Open GUI off/on also passed. WPA3 GUI off/on restored the link but retained 20/20 forward, 19/20 reverse: the early probe saw a cached lease and began before the later independently confirmed fresh DHCP ACK. This is not a zero-loss pass or proof that the driver caused the missing packet.
- Removing the cold-GUI open fixture restored WPA3 automatically with fresh DHCP and 20/20 both ways. Host management stayed on wired Ethernet. No physical-host .22 operation, broad QEMU kill, or kext unload was performed.

Limits retained: non-seamless roaming and transition losses, two-hop RTTs up to about 590 ms (host AX211 power saving was on), post-S3 WindowServer/framebuffer acknowledgement wait, already-active AP continuity through sleep, broader security/interoperability variants, throughput parity, and physical IWM/IWX coverage. The new deadline's phase-specific early-rearm branch is established by actual-function tests, not claimed to have executed in the two sampled RF phases. This alpha is not a claim of complete WPA3/AP/reference parity.

Evidence manifests: 145-file pre-cold `45888272923e3d98e65f54e686ea74b4026e17c8fcd13834c1d0c0d21798616d`; 127-file cold GUI `cb47359b625cd880d94328e31dfcc7540c11c91e1cb4b6d4a5b1a5033907e384`; 45-file external AP `de708a96cecf5ca6cef6ecafd2486bcd7e765dcd6d91763ff4b523ff6ad80c76`. Failed initial harness/manifest attempts and negative traffic results were retained, not replaced by successful repeats.

---

### Earlier checkpoint history

The artifact identity above supersedes the older image identities below; their history and limitations are preserved.

## 2026-09-11 source-loss scan recovery and nonblocking IWN AUTH

The current Tahoe asset contains production `52951b81`, including `bb142d93` and the intervening common/IWN/IWM/IWX ownership corrections. IWN AUTH now continues from owned firmware-command and beacon receipts instead of blocking the RX work path while waiting for a beacon. Source cancellation now retires the exact logical pre-target roam-scan owner before yielding callbacks. It does not fabricate physical scan completion, retire another request, or erase post-scan/on-air owners.

Loaded Mach-O UUID: `412FE602-291C-3683-877A-823470E766D1`.
Mach-O SHA-256: `883a37a204ec4039307d4f9a8a8e65da2d8d0fbc25ca5348e103e0f62ce7428c`.
ZIP SHA-256: `0978d13439d8907614a1ea2af2baa79ebfeee808d12ab9f71efdb6573163b93a`.
Production source manifest SHA-256: `351889e0d098a0586b8423ece8ddd1206b9949e133422d1d6b51ac8aafb54520`.

On the predecessor, removal of a WPA2 AP after S3/off-on left an obsolete logical scan owner active, and subsequent joins repeatedly failed with EBUSY despite the physical scan having ended. Read-only traces of this actually loaded IWN/6235 image show exact source-owner retirement and automatic WPA3/DHCP recovery both before and after real S3, without a manual join or radio toggle. Both repeated recovery checks completed independent 20/20 traffic in each direction. The updated 25C56 Ghidra reference exports were used to separate logical link-loss cleanup from firmware scan completion.

Actual S3 lasted 40 seconds with diagnostic USB Ethernet absent. The same boot/image automatically restored WPA3 DHCP and passed 20/20 both ways before USB management returned. Native WPA3, WPA2 and open Internet Sharing then ran sequentially on that post-S3 image: each negotiated the expected external-client security, obtained DHCP, passed 20/20 forward and cold-ARP 10/10 reverse checks, and routed an exact HTTP payload. Every normal disable retired the bridge and restored STA traffic without an intervening reboot.

Cold-boot System Settings selections of saved WPA2, saved WPA3 and open networks each obtained DHCP and passed independent 20/20 traffic checks. GUI off/on restored the same open network and passed 20/20 both ways. WPA3 off/on and automatic WPA3 return after removal of the open AP restored DHCP but had one and two lost forward packets respectively; a later settled check passed 20/20 both ways. Boot checks also retain isolated loss and an earlier recovery RTT outlier of 1.077 seconds. These are functional recovery checks, not lossless or low-latency qualification. One earlier GUI trial was interrupted by host ENOSPC; it was preserved as incomplete and repeated after verified storage recovery and a normal lab reboot.

Production-function regressions, macOS/Linux sanitizer tests, the full Tahoe build and symbol resolution passed. The packaged bundle byte-compares equal to the installed and tested bundle. Hardware qualification here is IWN/6235; executable/build coverage of shared and IWM/IWX paths is not new IWM/IWX RF evidence. The unfinished comeback-minimum-deadline prototype is not included.

Remaining work includes packet loss/latency and failed-candidate policy, other AUTH/ASSOC/reassociation failure producers, deferred BSS and MVM physical-retirement gaps, the full GUI/profile/security/sleep matrix, automatic active-AP client continuity, concurrent STA/AP policy, ad-hoc and recent IWM/IWX hardware qualification. Post-S3 GUI still has the separately observed framebuffer-acknowledgement wait; Wi-Fi recovery does not qualify that GUI path. Physical host 10.90.10.22 was not modified or rebooted for this release.

[Implementation, reference reconstruction, exact-image STA/S3/AP/GUI evidence and qualification limits](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_REASSOC_SCAN_SOURCE_CANCELLATION_20260911.md).

All sections and “current asset” descriptions below are historical. The current download is the `52951b81` production image identified above.

## 2026-09-11 candidate-owned SAE rejection and reconnect recovery

The current Tahoe asset is built from `5e7a6735`, including the production SAE correction in `97fe747c` and the accumulated common/IWM/IWX ownership corrections. A real received SAE failure now belongs to the exact fresh AUTH attempt admitted before crypto work. It preserves the peer result, retires producer/lower/SAE work, and only then publishes the one-shot failure to WCL. Stale or replaced attempts cannot complete a newer request. This does not manufacture a fresh JoinAdapter request for reassociation's separately owned failure path.

Loaded Mach-O UUID: `69D766FF-86F9-3337-ADE0-48361B68A59C`.
Mach-O SHA-256: `560a3f5bb365a7f5c9a5e6f8d7418b0ff0db0931e392901f38aa658a4e8f5a63`.
ZIP SHA-256: `c2dd6a085d4929d733fa77f385bc0a460160caaba6f885b5405f916b368ba622`.

On the actually loaded IWN/6235 image, a controlled wrong-password SAE AP produced real empty-body Confirm status-1 rejections. Two fresh join generations completed all three retirement edges and exactly one failure publication each, followed by successful SAE on another BSS of the same SSID. Native DHCP publication followed each fresh-attempt failure by roughly two seconds; this is not the total directed-roam outage. Subsequent independent 1400-byte checks passed 20/20 in both directions without another join command, off/on or reboot. The same observation also revisited the bad BSS, so failed-candidate policy and the separate reassociation failure path are not claimed closed.

On this same image, real System Settings selection of saved WPA2, saved WPA3 and an open profile, plus one actual GUI off/on cycle, each completed DHCP and independent 20/20 forward/reverse traffic. Off correctly withdrew carrier and IPv4. Actual S3 with USB Ethernet/tablet absent restored the same open network automatically; external DHCPACK and native IPv4 publication occurred after wake, and 20/20 in both directions passed before USB management was restored. These are selected saved-profile/service gates, not the complete GUI matrix or a latency qualification.

Native WPA3, WPA2 and open Internet Sharing then ran sequentially on that post-S3 boot. Every mode obtained real external-client DHCP, independently confirmed security, passed 20/20 forward and bridge-scoped cold-ARP 10/10 reverse packets, and routed an exact HTTP payload through the guest upstream. Each ordinary disable plus a 15-second dwell left bridge100 absent with zero retired-bridge I/O references and restored actual STA traffic at 10/10. No radio toggle, daemon restart, bridge rewrite or guest reboot separated the modes. This is native AP operation after S3, not automatic AP-client continuity through S3 or concurrent STA data service via Internet Sharing.

The complete 13,858-line candidate serial interval has no matched panic, firmware fatal/error, device timeout, unset-key or AP TX-gate text. Production-function ASan/UBSan regressions pass on Linux and macOS; the Tahoe kext build and external-symbol resolution pass. Included IWM/IWX station/RX-BA and IWX TX-queue ownership changes have executable test/build coverage; this update's physical runtime qualification is IWN/6235 and must not be extrapolated to every Intel family.

Remaining work includes repeated selection of failed BSSs, reassociation and other AUTH/ASSOC/key failure/timeout producers, roaming loss/delay and the previously observed ARP/DHCP interruption, the full new/saved/security/off-on/sleep GUI matrix, active-AP off-channel scanning, automatic AP-client continuity, ad-hoc, real CCA and equivalent recent IWM/IWX hardware qualification. Post-S3 GUI remains stalled: a bounded WindowServer sample locates its main thread in `displayDidWake -> IOFBAcknowledgeNotification -> IOConnectCallMethod`; the kernel-side cause is not yet established, and this is not advertised as fixed.

[Exact-image implementation, negative controls, GUI/S3/AP evidence and archive identities](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_SAE_PEER_FAILURE_20260911.md).

All artifact identities and “current asset” descriptions below are historical. The current download is the `5e7a6735` artifact identified above.

## 2026-09-10 roaming address continuity and failed-target recovery

The current Tahoe asset is built from `b5c6cfd8`, including `c19ab0db` and all previously qualified AP and PMF corrections below. Admitted same-ESS roaming now preserves the logical carrier while the old association is being replaced. A genuinely failed replacement retires that reservation once and independently notifies WCL of link loss, with the selected BSSID and association epoch revalidated inside the controller gate. Source-retaining/superseded scans are not converted into disconnects; stale failures cannot retire a newer join. Explicit leave/off/deauthentication paths retain their existing terminal owners.

Loaded Mach-O UUID: `9D5A9332-924A-3D77-8615-7E1CF1C73CB1`.
Mach-O SHA-256: `4037898627c66b449c6c6688378910954c65839b62a0bdf5c702792e356cb7ec`.
ZIP SHA-256: `c2940702f7de42e797659bc59a36a0908756c35c219b9196eea19bd123815b44`.

On the loaded IWN/6235 image, an external same-SSID wrong-password AP received SAE Commit/Confirm and rejected the Confirm mismatch. After actual carrier loss, airportd cleared its associated-network state in about 15 ms instead of the previous approximately 54 seconds; stale Already-associated refusals no longer blocked the immediate recovery interval. Automatic WPA3 retry began about 3.8 seconds after loss, but still reached the failing AP. The system then automatically joined a different saved WPA2 profile and obtained DHCP about 22.1 seconds after loss. Recovered traffic passed 20/20 in both directions without guest radio toggling, manual selection or reboot. This closes stale WCL ownership, not full target-selection/failover policy.

A separate 150-second test held one TCP and one UDP socket open across three actual WPA3 BSS transitions. Native events showed no carrier withdrawal, IPv4 removal or repeated DHCP acquisition. TCP sent and received exactly 6,301,000 bytes with one peer connection and no pending echo bytes. UDP returned 6,012,000 of 6,340,000 bytes, about 5.17% missing; EAGAIN/ENOBUFS send pressure remains recorded. This is established-TCP and address continuity, not lossless or low-latency roaming. An earlier foreground-GUI scan run superseded four roam requests and is not counted as four successful transitions.

Real GUI selection of saved WPA3 and open profiles, and a single GUI off/on cycle, restored DHCP and independently checked traffic. Actual Ethernet-free S3 retained the boot and kext UUID and automatically restored the same open network's carrier and DHCP before USB management was reattached; both post-wake directions passed 20/20. The guest framebuffer remained stale, so this is network-service recovery, not post-S3 GUI usability.

On that same post-S3 image, native WPA3, WPA2 and open Internet Sharing each obtained real external-client DHCP, independently verified security, passed 20/20 forward and bridge-scoped cold-neighbor 10/10 reverse packets, and routed HTTP through the guest upstream. Each normal disable, followed by a separate 15-second dwell, left bridge100 absent with zero retired-bridge I/O references and returned actual STA traffic at 10/10. No bridge rewrite, daemon restart, Wi-Fi toggle or guest reboot separated these modes. The complete 13,684-line candidate serial interval contains no matched driver panic, firmware fatal/assert, device timeout, unset-key or TX-gate diagnostic.

[Detailed reference reconstruction, production-function regressions and exact-image runtime evidence](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_FAILED_ROAM_WCL_TEARDOWN_20260910.md).

Remaining work includes candidate reordering/selection after failure, competing foreground scans, measured roaming loss/delay, the previously observed post-roam ARP/DHCP blackhole, the complete saved-profile/security/off-on/sleep GUI matrix, post-S3 framebuffer stalls, automatic external AP-client continuity, active-AP off-channel scanning, ad-hoc, real CCA measurement and equivalent recent IWM/IWX hardware qualification. These new hardware results are IWN/6235, not all Intel models. Native Internet Sharing uses an independent upstream and is not represented as concurrent STA traffic.

All artifact identities and “current asset” descriptions below are historical. The current download is the `b5c6cfd8` artifact identified above.

## 2026-09-10 protected WPA3 leave and post-sleep security regressions

The current Tahoe asset is built from `964a90b3`. It retains the reset-free AP pressure/restart corrections in `3e73f218` and fixes TX key selection for already-protected unicast management frames after WCL cancels an association's credential policy. The installed peer PTK remains the owner of the old connection's protected deauthentication, disassociation and action frames until actual key retirement. PMK scrubbing, association epochs, retired-key rejection and multicast GTK/BIP selection are unchanged.

Loaded Mach-O UUID: `EDEEE35F-92DE-3EE3-8F25-3EBA042D1275`.
Mach-O SHA-256: `a5a808d8b7e644401b56a44554f2c52ca2232c5de33fbd5c4b2c1c2b4a685752`.
ZIP SHA-256: `eaa6ef1c077dfb7fc309f0781722f8ed3aa6594ad3aa01b127192a021974f9ab`.

On the loaded IWN/6235 candidate, real GUI saved-WPA3 transitions produced three successful protected leaves with zero encryption failures in the completed observer. An independent monitor captured protected deauthentication, and the external hostapd accepted its reason code and removed the old STA. This is accepted on-air protection, not merely a local cipher-dispatch test. Two earlier monitored traffic runs each lost one of 20 forward packets; later GUI reselection without the monitor passed separate 20/20 forward and reverse checks. These observations do not prove the monitor caused the earlier losses.

Real System Settings selection of a new WPA2 profile on the external OpenWrt control network and a saved open profile each completed actual DHCP and separate 1400-byte 20/20 checks in both directions without Wi-Fi off/on or command-line association. Actual S3/wake retained the boot and kext UUID. The system automatically selected another saved WPA3/required-PMF network and restored DHCP and 20/20 traffic in both directions. This is service recovery, not a passed return to the last selected open profile or a post-wake GUI-selection test.

On that same post-S3 candidate, native system Internet Sharing ran WPA3, WPA2 and open sequentially. In every mode the external client obtained real DHCP, independently confirmed security, passed 20/20 forward and cold-neighbor 10/10 reverse packets, and routed HTTP through the guest upstream. WPA3 negotiated SAE group 19, required PMF and BIP. No manual bridge rewrite, daemon restart, radio toggle or reboot separated the modes. WPA3/WPA2 stop each passed a 15-second dwell, detached bridge with zero I/O references and primary STA 10/10.

After the final open stop, the fixed-old-address probe failed to bind; it is not recorded as a passed old-profile return. Exact current-boot logs show automatic selection of a different saved WPA2 profile and publication of its new DHCP address about 14 seconds after stop, before that probe. Independent checks on the actual address then passed 20/20 in each direction. The complete 13,177-line candidate serial interval contains no matched driver panic, firmware fatal, device timeout, unset-software-key diagnostic or AP TX gate failure.

Remaining work is explicit: a best-connected roam from a strong to a weaker BSS withdrew a working DHCP address and then failed ARP/DHCP; passing GUI rejoins do not close that defect. The full saved-profile/off-on/security/sleep GUI matrix, automatic external AP-client continuity, active-AP off-channel scanning, ad-hoc, real CCA measurement and equivalent recent IWM/IWX hardware qualification remain incomplete. The guest framebuffer stalls after S3, so native sharing/service recovery is not relabeled as post-sleep GUI qualification. These packet checks are not throughput, low-latency or lossless-roaming gates.

All artifact identities and “current asset” descriptions below are historical. The current download is the `964a90b3` artifact identified above.

## 2026-09-10 reset-free AP pressure/restart and post-S3 sharing

The current Tahoe asset is built from `3e73f218`. It includes the AP discovery, DTIM delivery and BSS teardown fixes from `893a3114`, plus:

- Use the matching 25C56 Skywalk queue's native asynchronous TX dequeue option. Client-association and low-water notifications no longer synchronously drain the AP TX backlog inside RX processing.
- Preserve ordinary packet rejection/backpressure accounting without printing a synchronous console line for each ordinary rejected packet.
- Flush and retire actual IWN AP TX ownership before PAN stop completion; clear retired scheduler state before queue reuse.
- Keep the fixed PAN AUX queue separate from primary aggregation, and retain the correct non-data firmware station for AP management TX.
- Fence AP start/stop across yielding lower calls so a superseded request cannot republish an obsolete owner or profile.
- Stop publishing RSSI as a valid CCA occupancy sample. Real RSSI/noise/counters remain; actual CCA measurement is still outstanding.

Loaded Mach-O UUID: `946F461F-3B19-38AA-8923-7AEF04263ED8`. Mach-O SHA-256: `94172899f0a0e9c522a54f05d8585303be826f2dca71136174a60c46cdd11f6f`. ZIP SHA-256: `c0b26819fbe45072584cdbd5adf461d624813b7bbdacb9ca208897b771af95e8`.

On IWN/6235, two separate AP stops during UDP pressure retired 193 and 225 pending aggregate descriptors to zero. Each subsequent ordinary AP start and external SAE/required-PMF client join passed 20/20 forward, isolated cold-neighbor 10/10 reverse and concurrent STA 5/5. The complete pressure observer recorded zero firmware fatals/hardware stops and no AP data call nested inside a wake request. This closes the reproduced two-cycle reset failure, not a lossless or maximum-throughput qualification.

Actual S3 retained the boot epoch and loaded image. The driver restored the AP without a new AP-start command; explicit external-client reselection completed SAE/required PMF and repeated 20/20, cold-neighbor 10/10 and concurrent STA 5/5. No emulated Ethernet device was present during sleep; a fresh independent management upstream was attached only after wake.

Native system WPA3, WPA2 and open Internet Sharing then ran sequentially on the same post-S3 boot. Every mode obtained real DHCP, passed 20/20 client-to-gateway packets, a separate bridge-scoped cold-neighbor 10/10 check and routed HTTP. There was no manual bridge rewrite, daemon restart, radio toggle or reboot between modes. All three retired bridges reached I/O count zero. Complete serial and bounded observers found no driver panic, firmware fatal, device timeout or AP TX gate-error storm.

Known limitation observed in this exact qualification: after the final open-sharing disable, the 15-second STA check failed because its address was absent. Airportd first selected a weak 5-GHz BSS and timed out with `-3905`, then automatically selected a stronger 2.4-GHz BSS and restored DHCP about 28 seconds after disable. A later independent check passed 10/10 without explicit selection or off/on. This is not a passed immediate-return or seamless-reconnect test.

[Detailed reference, production-method regression and loaded-image evidence](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_AP_TX_DEQUEUE_RX_STARVATION_20260910.md).

Remaining work includes the full GUI/saved-profile/security/on-off/sleep matrix, occasional join errors and candidate-selection delays, lossless/full-policy roaming, automatic external-client continuity after sleep, active-AP off-channel scanning, intermittent weak 5-GHz reception, ad-hoc, real CCA measurement and equivalent recent IWM/IWX hardware qualification. These runtime results are IWN/6235, not all Intel models.

All artifact identities and “current asset” descriptions below are historical. Today's download is the `3e73f218` artifact identified above.

## 2026-09-10 AP discovery, DTIM delivery and BSS teardown

The current Tahoe asset is built from `893a3114`. It includes these runtime-qualified fixes since the previous `150d7e73` asset:

- Publish the firmware-admitted AP BSD interface before the primary is first discovered. This prevents the system sharing plugin from permanently caching the primary as its AP fallback and bridging the wrong interface. IWN previews real embedded firmware metadata without starting the radio; IWM/IWX keep their existing firmware/NVM capability gates.
- Publish only observations from the current physical scan, without recreating disappeared APs as fresh results.
- Deliver IWN AP broadcast/multicast through the firmware DTIM queue, including clients with power save enabled.
- Restore confirmed AP carrier after BSD protocol re-enable so the real bridge can forward broadcast traffic.
- Retire old STA TX aggregation before direct BSS replacement, and drain submitted IWN descriptors before queue cursor reuse.

Loaded Mach-O UUID: `5455B34A-AA04-34EE-8CC5-82B373798841`. Mach-O SHA-256: `c308f9b5dbe8e3e52f88643aee35441a3384ed7c62da1d404c47f8db77f4d2b1`. ZIP SHA-256: `f15e21c9d29c185c021f4c962512fada3f1dd9d05c66c85e0ecdb4a05ccc42d8`.

Two cold boots on IWN/6235 confirmed actual AP attachment before primary discovery and distinct names in the live system sharing cache. Native open/WPA2/WPA3 Internet Sharing obtained real DHCP, passed 20/20 client-to-gateway packets, separate bridge-scoped cold-neighbor 10/10 tests with client power save enabled, and routed HTTP through an independent upstream. No manual bridge rewrite or fake/static DHCP result was used.

Concurrent WPA3 STA plus SAE/required-PMF AP passed 20/20 client traffic, isolated cold-neighbor 10/10 and primary 5/5 before and after actual S3. The driver restored the AP without another start command; the external test client was explicitly reselected. Normal AP stop retained primary traffic at 10/10.

The full native WPA3 -> WPA2 -> open sequence also passed DHCP, 20/20, cold-neighbor 10/10 and routed HTTP on one post-S3 boot, with no reboot between modes. The old bridge retired to zero I/O references before reuse; final sharing disable preserved primary traffic at 10/10. An earlier bridge `EBUSY` was traced to two queued multicast ioctls behind a blocked AppleVirtIO network-work-queue callback. The successful control omitted emulated Ethernet during S3 and attached a fresh upstream after wake. This is not a claimed fix to Apple's unrelated Ethernet transport.

[Detailed loaded-image and reference evidence](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_APSTA_INITIAL_DISCOVERY_ORDER_20260910.md).

Remaining work includes intermittent missing 5-GHz candidates, public/UI saved-profile reconnect combinations and occasional join errors, lossless/full-policy roaming, automatic profile and external-client continuity after sleep, active-AP off-channel scanning, ad-hoc, and equivalent recent IWM/IWX hardware qualification. A brief background-roam address gap also reproduced during this control and recovered without off/on; it is not counted as seamless roaming. The recent hardware qualification above is IWN/6235, not all Intel models.

All artifact identities and “current asset” descriptions below are historical. Today's download is the `893a3114` artifact identified above.

## 2026-09-09 measured RSSI publication

The current Tahoe asset is built from `150d7e73`. Fresh beacon/probe RSSI now carries Apple's measured-value and on-channel metadata flags. The shared IWN/IWM/IWX producer keeps actual RX measurements separate from the historic scan-selection peak. A value-only publication witness prevents cancelled, superseded or repeated snapshots from consuming another sample or refreshing the same cached RSSI timestamp.

Loaded Mach-O UUID: `431DFEA8-4B59-3A52-B4B9-91677A7619B1`. Mach-O SHA-256: `7bb7dbd33dadd02303891a7a11e3791670851a020c292bf4e86d45bf232f3c67`. ZIP SHA-256: `a8cf54d2499cd6f423876b3f8f3195466f5c8a3b1a3315d96bbea27017fb2443`.

On the loaded IWN/6235 candidate, the real Apple scan consumer and final CoreWLAN results retained measured negative RSSI instead of discarding it. A matching-25C56 observer verified fresh RSSI/timestamp writes and unchanged timestamps on cached replay. Saved WPA3/DHCP recovered after activation. Three ordinary credentialed selections completed in 10, 9 and 9 seconds without a printed join error; each immediate five-packet test lost its first packet, then passed four. This does not prove the historical reconnect error is eliminated.

Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. Actual S3/wake retained boot epoch and kext UUID. Explicit client reselection completed SAE group 19/PMF/BIP and repeated all three traffic checks. AP stop reached the lower zero-result terminal and retained primary traffic at 10/10. These AP checks use static addresses and prove service recovery, not automatic client continuity or a new DHCP matrix.

Remaining work includes old cache-only identities with no RSSI in a newly created Apple BSS object, intermittent missing 5-GHz candidates, first discovery of new open networks, public/UI saved-profile reconnect combinations, full reference roam policy, active-AP off-channel scanning, ad-hoc, and equivalent qualification of recent changes on IWM/IWX hardware. The measured-RSSI fix does not label stale cache data as a fresh observation or claim those surfaces closed.

All artifact identities and “current asset” descriptions below are historical. Today's download is the `150d7e73` artifact identified above.

## 2026-09-09 queued 5-GHz scan handoff

The current Tahoe asset is built from `549114c2`. A 5-GHz-only WCL scan queued behind an existing foreground scan now preserves its admitted band when the physical radio is handed over. The old queued path unconditionally attempted an empty 2.4-GHz command. Regulatory restrictions, the requested channel list and scan timing are unchanged.

Loaded Mach-O UUID: `13B4F696-F1E4-3FCC-82E4-3834DE4B4E76`. Mach-O SHA-256: `351b094168d86f2c922cf59841f722ce9837f9f7309c35738fd837a0b9d0226c`. ZIP SHA-256: `d9c6e543c08e93ef20610a66dccbd6013184a8169fb246faa98e1481ec3e34db`.

On the loaded IWN/6235 candidate, the previously failing queued request submitted a real one-channel 5-GHz firmware scan, completed successfully and returned in 3 seconds instead of the prior 20-second timeout. The result set was still empty: this fixes scan admission/handoff, not all missing 5-GHz BSS candidates. A subsequent public join still reported `-3912`; a retry also reported that error before DHCP/data recovered and passed 10/10 packets without off/on.

Saved WPA3 STA/DHCP recovered after loading. Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. Actual S3/wake retained the boot epoch and kext UUID. Explicit client reselection completed SAE group 19/PMF/BIP, and the three traffic checks passed again. Normal AP stop reached the lower terminal and retained primary traffic at 10/10. These AP checks use static addressing and demonstrate service recovery, not automatic client continuity or a new DHCP AP matrix.

Remaining work includes intermittent missing 5-GHz candidates and first discovery of new open networks, public selection/reconnect errors, the full GUI/saved-profile/security/sleep matrix, full reference roaming policy, active-AP off-channel scanning, ad-hoc, and equivalent qualification of recent changes on IWM/IWX hardware. This IWN-specific scan fix does not imply those hardware matrices passed.

All artifact identities and “current asset” descriptions below are historical. Today's download is the `549114c2` artifact identified above.

## 2026-09-09 reconfigured-BSS discovery and reconnect

The current Tahoe asset is built from `96eaf2e9`. Fresh beacon/probe-response SSIDs now update cached BSS identities instead of keeping the first name forever. A renamed AP with the same BSSID is discoverable again. Hidden advertisements preserve learned names, and active association/AP peer owners are not rewritten. The shared net80211 change covers IWN/IWM/IWX source paths; this artifact was hardware-qualified on IWN/6235.

Loaded Mach-O UUID: `0FE73C63-480B-39C3-A43E-53DB3D225366`. Mach-O SHA-256: `ee4da909d6d57b75b6e56f5801424ea106a5021f9a7392885b1f303120fba0c2`. ZIP SHA-256: `c76806872f0ec00dd4f6c335cd381e27c19d956b08aeb3da172ccee39bd9ed1e`.

On the loaded candidate, the same BSSID was first cached as an open network, then renamed and changed to WPA2. A live probe observed the old cached SSID being replaced by the incoming SSID. The first ordinary public WPA2 selection completed in 16 seconds, followed by the four-way handshake, DHCP and 20/20 packets in both directions, without radio off/on or another reboot.

With the test WPA2 profile first in the preferred list, actual S3/wake automatically rejoined that WPA2 AP; traffic returned on the fourth one-second probe, then passed 10/10. An earlier run with another saved WPA3 profile first selected that other network: airportd discovered both and requested WPA3. The profile-order comparison is not a claim that the full GUI last-selected-network policy is fixed. A subsequent ordinary WPA2-to-WPA3 selection and 5/5 traffic also passed.

Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. Another actual S3/wake retained the boot epoch and kext UUID; explicit client reselection completed SAE group 19/PMF/BIP and repeated those checks. Normal AP stop retained primary traffic at 10/10. AP regressions use static addressing and prove service recovery, not automatic client continuity or a fresh DHCP AP matrix.

Known limitations remain: missing particular 5-GHz scan candidates, intermittent initial discovery of a new open network, full GUI/saved-profile/security-mode coverage, complete roam-policy/same-BSSID behavior, active-AP off-channel scans, and equivalent IWM/IWX hardware qualification. An emulated USB Ethernet management interface disappeared after repeated S3 while physical Wi-Fi SSH worked; that separate VM transport is not counted as a Wi-Fi failure or as fixed.

All artifact identities and “current asset” descriptions below are historical; the current download is the `96eaf2e9` artifact identified above.

## 2026-09-09 WCL BSSID reassociation correction

The Tahoe asset is now built from `9040aa2b`. The driver decodes the reference WCL reassociation carrier as six-byte BSSIDs, not score/channel tuples. Broadcast and unspecified targets perform a real bounded scan within the associated ESS, with independent channel, security and PMF admission. Merely publishing an idle AP interface no longer restricts ordinary STA roaming to its current channel. Real single-channel AP lifecycles retain their hardware constraint.

Loaded Mach-O UUID: `53207465-749C-3229-82A6-3E9EB798433F`. Mach-O SHA-256: `05ccada93fd70030f007ac6539253718060b42c63bc18a25f36766f359ba25a5`. ZIP SHA-256: `90365ab1867ebcad8af14e7a17600b6eb2bf8ae8d6b765eb62a33aa1f406165b`.

On the IWN/6235 Tahoe guest, a real userspace broadcast-BSSID request reached physical 13/24-channel scanning, selected another BSS and completed driver-resident SAE retarget. Saved WPA3/DHCP returned after loading. Three repeated credentialed public selections completed in 10, 9 and 10 seconds with 5/5 packets after each. A further selection after actual S3 and AP stop completed in 13 seconds with 10/10 packets. This is not a claim that every GUI/reconnect combination is fixed.

Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. Actual S3/wake retained the boot epoch and kext UUID; explicit client reselection completed SAE/PMF and repeated those checks. AP stop retained primary traffic at 10/10. These AP regressions use static addressing and prove service recovery, not uninterrupted or automatic client continuity or a fresh DHCP AP matrix.

Remaining work includes missing particular 5-GHz scan candidates, the full GUI/saved-profile/security-mode matrix, all reference roam-policy flags and same-BSSID behavior, active-AP off-channel scanning, and equivalent IWM/IWX hardware coverage. Historical tests below retain their original artifact scopes.

## 2026-09-09 IWN firmware scan timing budget

The current Tahoe asset is built from `35589ce0`. IWN now bounds the final per-channel scan dwell by the live STA/PAN beacon budget and the strict firmware max-out deadline, after applying the upper WCL timing request. It retains the channel plan and passive/DFS restrictions. The APSTA association-epoch and retained-IGTK PMF fixes below remain included.

Loaded Mach-O UUID: `990CB31C-373C-3CAB-B86F-F8C852342D58`. Mach-O SHA-256: `66cbd52411a11ecd9c0284a64892c2674e3268eb20704ce435850890138d6024`. ZIP SHA-256: `42d65d772c248d82c1e2154aecedbb1857afb2f66446ea0382b4e8aa7e1c1365`.

On the physical IWN/6235 Tahoe guest, saved WPA3/DHCP recovered after loading the candidate. FBT verified the corrected passive dwell in the actual firmware command and successful physical terminals for the requested 13-channel and 24-channel scans. Concurrent WPA3 STA plus role-7 WPA3/required-PMF AP passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. True S3 and ACPI S3 wake restored both roles; explicit client reselection completed fresh SAE/PMF and the same traffic checks passed. Stopping AP retained primary traffic at 10/10. These AP checks used static addressing and do not replace the historical DHCP matrix below.

Known limitations remain: directed scans can omit a particular 5-GHz BSS even when firmware reports completing the channel plan, and repeated public network selection can report an error before background recovery. The scan timing fix does not claim to solve either issue. During active role-7 AP, the tested public scan calls returned an empty set without a physical scan command; only traffic continuity, not PAN off-channel scan behavior, was qualified in that interval. Full GUI and equivalent IWM/IWX hardware matrices remain incomplete.

Earlier qualification history follows; all older "current asset" descriptions below refer to their historical updates, not today's download.

## 2026-09-09 APSTA retained-association epoch fix

The current Tahoe asset is built from `98dc62ee`. APSTA handoff and post-stop reservations are now bound to the original association epoch. A real network leave or credential replacement can no longer carry a stale reservation into the next connection and acknowledge its first WCL association without executing it. The retained-IGTK PMF fix and AP-capable default remain included.

Loaded Mach-O UUID: `6E20810C-101D-313E-AD15-56ED0BD8E709`. Mach-O SHA-256: `948d79ae2f053b2d4315257d50f37d935f46577462fc40659f40f855ec28a159`. ZIP SHA-256: `5691d848ca125fba4c32b864f458347293777d8f23523d2da2fa9201cfb71e30`.

On the physical IWN/6235 Tahoe guest, FBT verified that an epoch-changing public leave no longer suppresses the next real SCAN/association. The same loaded candidate passed concurrent WPA3 STA plus role-7 WPA3/required-PMF AP traffic: 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway. Actual S3 (QEMU suspended and ACPI S3 wake) restored both roles; after client reselection, the same traffic checks passed. A normal AP stop retained the primary path with 10/10 packets. The AP regression used static addresses; prior Internet Sharing DHCP matrix results below describe their stated older artifacts.

Known limitation: repeated public selection is not fully fixed. One selection completed in 10 seconds, but the next still reported `-3912` after 47 seconds before background recovery. Its remaining failure occurred after real association admission, including a 5-GHz authentication timeout. Idle-STA shared-channel filtering and reliable directed-roam candidate discovery remain under investigation. This update does not claim the full GUI matrix or equivalent IWM/IWX hardware qualification.

Earlier qualification history follows; old artifact identifiers below are historical, not the identity of the current download.


## 2026-09-09 retained-IGTK PMF reconnect fix

The current asset is built from `5b4929e3`. It restores management-frame protection after a saved WPA3 reconnect when the AP supplies the same live IGTK. The driver validates the retained key at the association commit and rearms PMF without reinstalling the key or resetting replay counters. The shared net80211 correction applies to IWN/IWM/IWX; this update was runtime-qualified on IWN.

Loaded Mach-O UUID: `90A9F9AA-2E9A-302F-A4C7-4FA29A14730C`. Mach-O SHA-256: `0602876224834e19ce16c6a220b6ab2fa47a9ad034ad3477714af1992ad4bca0`. ZIP SHA-256: `2659b255ca40407d7622566ccde701a92184cfcd11ab659e70c95b3694eb6d9f`.

On the actual Tahoe guest, protected 802.11v steering, SAE/DHCP, saved-profile return to the source AP, SA Query response and a second protected BTM transition passed. The recovered path passed 8/8 source-bound packets. A real S3/wake recovered Wi-Fi on the seventh one-second probe and then passed 8/8 packets in each direction. The prior AP-capable product configuration is retained.

Remaining observed issues include the public network-selection command reporting an error before eventual connection, and an exact 5-GHz BTM scan sometimes missing a target seen by normal scanning. These are not claimed fixed by this release.

## 2026-09-09 product-default AP qualification

The release asset is the AP-capable default built from `6eb51401` (UUID `F9E599B0-3FA4-3A9E-886C-BC3A31294DE3`, Mach-O SHA-256 `91afd774a0fb51aeba21757423661e3147466cdce674ebc90dd7bbb2c9dcb11b`, ZIP SHA-256 `8467743b2867b9a79c146ad374d042b6f0d83fe9e882eb622f5bdd13391d8da1`).

Using the real Tahoe `configd -> airportd -> InternetSharing` path and an external AX211, default Open, WPA2-PSK and WPA3-SAE/required-PMF APs each completed discovery, association, DHCP and 20-packet gateway traffic. The product-default WPA3 AP was also discovered and joined at 5745 MHz on 5-GHz channel 149, with a DHCP lease and 20/20 gateway probes. An Open AP was disabled and enabled again through the same standard producer; the external client automatically rejoined and passed a fresh 20-packet transfer.

A forced sleep/wake replay under active WPA3 sharing republished `ap1`; the client retained SAE, required PMF, BIP and its DHCP lease. Three packets were lost during the wake transition, then 22 consecutive gateway probes passed. This records recovery rather than claiming a zero-loss forced-sleep handoff.

A separate saved WPA3/SAE STA profile also completed normal radio off/on (DHCP plus 20/20 traffic in both directions) and a true S3/wake. After the owned QEMU wake, physical Wi-Fi SSH and 12/12 traffic in each direction worked; only the independent virtio user-NAT host-forward remained unavailable, which is a VM management-transport limitation.

## Tahoe current-network copyout — `ff0f3f8b`

This prerelease replaces the Tahoe kext asset with commit
`ff0f3f8bba690253fb0ebeb31cd54bf47b209104`.

- Fixes the Tahoe BSD `APPLE80211_IOC_CURRENT_NETWORK` route: it now builds
  the `0x8d8` scan-result carrier in kernel memory and uses
  `IO80211Controller::copyOut`, rather than passing a caller-owned nested
  pointer to the serializer.
- Mach-O UUID: `0F4EC337-E357-399E-B62C-871CAF896E0D`.
- Mach-O SHA-256:
  `ad35f0e9e639a0d3ef8a37ba520b2f0284e9e433512b66d73b3443b78eec9c8c`.
- ZIP SHA-256:
  `0f6bb5cbc81ded0ab28d99045499a6f2624deb13c3529fe82aba34fe8b8a93fb`.

Runtime-qualified on Tahoe 26.2 / 25C56 with passed-through AX211 and the
real WPA3/SAE+PMF laboratory network: raw `CURRENT_NETWORK` returned the active
SSID/BSSID/channel; DHCP and 1400-byte traffic passed; saved-profile radio
off/on rejoined; and real S3 (`ACPI SLEEP` / `ACPI S3 WAKE`) recovered direct
Wi-Fi SSH, the current network carrier, and 20/20 host-to-guest plus
guest-to-router 1400-byte ICMP.

## WCL reassociation terminal ordering — `29817285`

This prerelease now carries commit `2981728587cba659a220070b2dd7959dc574f19f`.

- Publishes the genuine WCL reassociation result (selector `0x49`) on the accepted target `S_RUN` edge, before the RSN four-way completion; kernel PAE key-done remains tied to the real port-valid edge.
- Mach-O UUID: `8DBC93B1-C2FE-3258-BBDC-BB2841846D82`.
- Mach-O SHA-256: `4853e46cb9b23ddfd4d277acc5ad88e21214d259464b956ffc6d17fe37b4f7c7`.
- ZIP SHA-256: `6da8ca87901e41436003c19e8eb4a0224f0e855dc2c53cd0c4213c975c814899`.

Runtime-qualified on Tahoe 25C56 with the passed-through IWN 6235 and the two-BSS pure-SAE laboratory network: a native Wi-Fi off/on selected the other BSS, reached `TARGET_RUNNING`, retained DHCP, and after recovery passed 20/20 1400-byte guest-to-gateway plus host-to-guest traffic.

## IWN APSTA primary-channel handoff — `822af1b1`

This prerelease now carries commits `457b69c0f06d5fda16bcaf00d91b8a6043d1680d`
and `822af1b135b61310f63f43fda2e14466c902b53d`.

- Preserves the associated BSS channel through Tahoe's transient CoreWLAN
  HostAP role handoff and supplies it to the IWN APSTA owner before it builds
  the PAN beacon/RXON configuration. This matches Intel DVM's
  single-different-channel contract instead of inferring the channel from a
  post-handoff net80211 opmode snapshot.
- Mach-O UUID: `A34C4286-8063-30AF-B0FB-C79141E4E0AE`.
- Mach-O SHA-256:
  `1bebbaccde464e9ff44e1d882becaaed811180d777e0b0a3bd747851bdd539a5`.
- ZIP SHA-256:
  `3ea1d639711e9edf4d34325c4c121019556b1980897dd693c26acca2987ce05a`.

Tahoe 25C56 build resolved all 1083 BootKC symbols. On a passed-through IWN
6235, a standard CoreWLAN open HostAP BSS was visible to the external AX211
and completed real 802.11 association. The direct lab invocation of
`InternetSharing` lacks the system UI producer's XPC entitlement, so it did
not create `ap1`/bootpd and is not counted as a successful standard-DHCP test.
The same loaded candidate retained the real two-BSS WPA3/SAE+PMF laboratory network STA
path with 20/20 1400-byte ICMP in both guest-to-gateway and host-to-guest
directions.


## WPA3/SAE+PMF HostAP on-air qualification — `822af1b1`

The already-published `822af1b1` Tahoe kext was subsequently qualified through the native CoreWiFi/NetworkRelay producer, rather than a synthetic lower-driver call. A passed-through IWN 6235 advertised an SAE-only RSN BSS with PMF required; an external AX211 joined it, received an address by DHCP, and completed 20/20 1400-byte ICMP in both directions, guest HTTP (200, 8651 bytes), and routed HTTPS (200, 559 bytes). The host management default route remained on Ethernet.

This additionally confirms that WPA3 HostAP admission, the lower IWN SAE authenticator, PMF beacon advertisement, association, DHCP, and data plane are present in this artifact.

## IWN 5 GHz WCL exact-plan scan — `a654fa30`

This prerelease now carries commit `a654fa3078c69b77063d6422964e15cf0dc89392`.

- Makes the IWN DVM WCL scan entry honor a 5-GHz-only CoreWLAN carrier plan. It now starts its first physical scan command in the eligible band instead of rejecting the request while assembling an empty 2.4-GHz command.
- Mach-O UUID: `4527D090-4342-3EA9-8F19-043ED3F87A51`.
- Mach-O SHA-256: `8feca69c7cbb3175aed7eac24c177b5113a198ed23d2234308941d7cb73509e4`.
- ZIP SHA-256: `a0ef1586d1a1ebb181b63227d669c300a9a3d01b4ef492d295cf063cd2c5614f`.

Runtime-qualified on Tahoe 26.2 / 25C56 with a passed-through IWN 6235: an ordinary CoreWLAN 5-GHz channel request returned scan results without an error, and the driver trace showed the physical IWN scan starting with the 5-GHz flag. This closes the prior public scan admission failure; it does not claim full multi-AP coverage on every laboratory channel.

Runtime qualification added 2026-09-09:
- controlled same-ESS WPA3 BSS withdrawal through the normal Network Settings path: a saved profile recovered automatically after its joined BSS vanished, then retained DHCP and traffic;
- role-7 APSTA on current IWN hardware: a WPA3 AP client and a WPA3 STA remained active on the required shared channel, with bidirectional AP traffic and post-sleep replay.

The ordinary primary Internet Sharing route remains a working DHCP AP path, but it intentionally changes the primary interface role and is not represented as STA+AP concurrency.
