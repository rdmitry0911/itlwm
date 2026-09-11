# One owned target RXON for STA authentication

Status: production candidate loaded and published as a limited alpha; native two-direction RF pairs before
and after S3 pass target admission, with retained packet loss and one failed
post-S3 discovery. Post-S3 AP, native STA and exact-image SA Query controls
have completed. The final restored-link check failed its zero-loss gate
(20/20 forward,18/20 reverse). This does not close all SAE/reconnect failures.

## Measured failure before the change

On loaded `59448436-3947-3A8D-8738-402DF8FED85C`, boot
`6886D4CD-BC3A-45BE-8D73-4ACC65414EF7`, native LabAP roaming from
`9a:fb:5d:97:a9:02`/13 to `82:c3:97:84:51:ca`/9 failed before SAE.
In `ready-depart-q2`, AUTH serial23/epoch151 programmed at
1789157770097889127ns. RXON/index70, ADD_NODE/index72 and LINK_QUALITY/index73
all received their actual firmware replies within19ms. At
1789157775097957874ns the owner terminated with ETIMEDOUT, stage4,
rxon_submitted=1, rxon_accepted=1, prep_submitted=3, prep_accepted=3,
beacon_seen=0. No scoped beacon was delivered through guest RX during that
five-second interval. This is not an independent over-the-air beacon census.

The lower transition issued two full unassociated RXON commands:
old BSSID/channel13 at1789157770097873359ns, then target/channel9 at
1789157770097878071ns, only4.712us later. Both had AID0 and filter0x4;
stale association/decrypt flags are therefore not this observed discrepancy.
The preceding successful reverse control also had the duplicate,4.851us
apart, so coalescing it is a justified candidate, not yet a proven universal
cause of missing beacons.

The failed control retained only206/250 guest-to-gateway and207/250
host-to-guest packets. Native recovery first rejoined the source BSS; the
80s control ended there and failed its requested-target gate. A later native
transition reached channel9, outside that control. It does not erase the
first failure or prove traffic continuity. DTrace ended with errors0.

## Reference and implementation

[Pinned Linux v6.18 Intel DVM RXON implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/rxon.c)
passes candidate `ctx->staging` into its BSS disconnect RXON, then restores
station/PAN state. Local source SHA256:
`c37ec7322e59349cd168d2622a74841fd711401f2cc815811a11094677323233`.
The updated-Ghidra `5995e24caa` export of Apple core `handleAuthEvent`
(`ffffff800159f7f0`) forwards actual auth status/reason to JoinAdapter; it
does not permit synthetic success in place of radio readiness. Export SHA256:
`b3b3dd2f513b4f74324fa8ae690247205e1904f4ae3765d648903c691e76f5af`.

The existing APSTA coalescing now applies to every identity-owned STA AUTH
or RUN-to-ASSOC continuation. Logical old-association cleanup is unchanged;
only `iwn_auth()` submits the candidate RXON and its existing broadcast
station/link-quality/PAN restoration. INIT/SCAN/non-owned reset behavior is
preserved. The command acknowledgments and fresh target-beacon gate remain
mandatory; no new sleep, fabricated receipt or timeout-as-readiness is added.

## Executable checks and evidence

The test extracts the real lower state function plus AUTH owner/helper.
Four new cases cover plain STA/live APSTA crossed with AUTH/RUN-to-ASSOC;
one checks that RUN-to-INIT still sends its reset. They require exactly one
owned RXON, zero unowned RXON, logical cleanup and one generic commit only
after receipts and beacon. Hardware/IOKit/generic calls remain boundary
doubles; these tests are not RF evidence. The old code fails the new command
count assertion. The candidate passes all40 AUTH cases, and the complete
payload aggregate including all five real nonblocking AUTH-body modes.

Frozen evidence (534 files, including all failed trials and diagnostics):
`/home/dima/Projects/itlwm/aiam-auth-readiness-runtime.ndbJOE`.
All entries of EVIDENCE.sha256 verify. Manifest SHA256:
`f16d00a9ccbb51e6e3ef8e66d8a300df199ce7d9ab4b08f88cffb02865376160`.
Do not append later publication receipts or diagnostics to this root.
Aggregate SHA256:
`9a8c6843170f092b501b945d22efa9ec32fa1352f25f13037da23cfe0c904e7e`.
Failed baseline trace SHA256:
`d55b4487e56b29880fce4a0ba9da538f9dea2a5d92f9187df4886a68263ca5c2`.

Observer q1 incorrectly tested an FBT bool return as a full register; only
its stage4 terminal snapshots were true terminals. q2/q3 explicitly cast to
uint8_t. The decisive controls use q3. Public firmware/value layouts were
checked with static_assert on Linux and Tahoe (RXON50, request96, lease144).
No private ItlIwn/ic/mbuf/node offsets, key material or kernel writes were used.

## Loaded candidate and first two-direction RF pair

Source `f170870d` was committed and pushed before the build. The356-file
production manifest changes only ItlIwn.cpp relative to the prior SA Query
image; it is verified before and after the build. Manifest SHA256:
`26cd66f64bc8430161f52a041adf0de601589c53375d28b78e1d37dec57f778e`.
The40 AUTH cases also pass on Tahoe. All1088 imports resolve against BootKC.
Mach-O UUID `68F5B0D9-4863-3627-B30C-CC0CC111BB58`, SHA256
`7e368e43f9d2e968cf41f7512a57b0813d5d4443d10ea7244469a78604615f4d`.
Private admission and transactional activation preserve the exact five-member
AuxKC set. Transaction `activation-20260911T202639Z` retains rollback copies.
Normal guest reboot at20:27:10UTC loaded that image in boot
`B4FEB27A-EDDA-4A70-AA35-29B68EF3AE1A`; WPA3/DHCP returned automatically.

The first planned departure stopped before any stimulus because native policy
had already moved from02 to ca after boot. The two subsequent controls use
observer q4, identical public layouts and corrected bool predicates to q3;
only its header changes. No kernel writes, forced radio toggle or AP mutation.

| Actual control | First AUTH serial/epoch | Programmed-to-terminal | Traffic F/R |
| --- | --- | --- | --- |
| single-return-q1, ca9 ->0213 | 3/11 | 41.881354ms |249/250,249/250|
| single-depart-q2,0213 ->ca9 | 4/12 | 80.169531ms |249/250,248/250|

Both show exactly one unassociated target RXON (no old-channel reset), all
three actual command receipts and a fresh target beacon, followed by SAE and
first-attempt RUN. Each80s observer ends with errors0. The return forward
stream additionally reports one duplicate. Both final target/security gates
pass. These two samples do not prove the intermittent beacon failure cannot
recur and do not qualify seamless/zero-loss roaming. The five-second timer's
later callback is not a failed AUTH: it observes the already-consumed owner.

Trace SHA256: return
`d669a9c7e5ed6696d37ec863fee58d947ad89147176920be6b1ff0e80a6b5042`;
departure `dc3c4d53f286f58c5ffa503edb6dac05d141aec2f74004a3febfc8b4b9c57cba`.
The following sections retain actual S3, repeated post-wake native roaming,
normal open/WPA2/AP controls and the remaining failures. At this first-pair
checkpoint the public release was still ee501d78. The eventual exact-image
publication is recorded below; no earlier observation is relabelled.

## Real S3, post-wake roaming and guest AP

The strict pre-sleep WPA3 check passed20/20 both directions. Diagnostic USB
Ethernet and tablet were removed before sleep. QEMU was observed actually
suspended at20:35:10UTC, held for5s and woken once at20:35:15. macOS records
Normal Sleep20:35:09 ->20:35:17. Same boot and loaded UUID returned through
WiFi alone; DHCP and20/20 bidirectional1400-byte traffic passed before both
USB devices were restored. First-to-last arming latency is retained separately
from the actual sleep duration; no sleep-latency fix is inferred.

The first post-S3 requested02->ca transition failed before AUTH: scan serial45
completed with no target admission, and WCL published failure. The observer
saw no target beacon, but did not capture probe responses or independently
monitor RF. Source02 remained connected with250/250 packets each direction.
This is a failed roam, not a five-second AUTH/beacon timeout. The planned
reverse was not issued because its source precondition was not established.

One separately labelled additional pair was run; it does not replace that
failure. Departure q2 used AUTH serial6/epoch30 and admitted its fresh beacon
after284.091544ms, then SAE/RUN with247/250 packets each way. Return q2 used
serial7/epoch31 and50.110586ms, with249/250 each way. Both had one unassociated
target RXON, all three command receipts, first-attempt AUTH/RUN and errors0.
Return had one encapsulation drop. The four successful target admissions
around S3 show the intended command change on hardware; the intermittent
discovery, residual losses and broader AUTH timeout surface stay open.

On the same post-S3 image, native Internet Sharing was started sequentially
as WPA3, WPA2 and open. External AX211 negotiated SAE/PMF-required, WPA2-PSK
and NONE respectively and obtained192.168.2.2 by DHCP. Every mode passed
20/20 forward and10/10 cold-ARP reverse packets, the exact118-byte HTTP
payload through guest USB/NAT backhaul, normal stop/bridge100 disappearance
and10/10 restored STA gateway packets. All host-profile restorations returned0;
wired management route stayed unchanged. Last open mode ended20:47:25UTC.
No private unlocked ifnet-list observer was used: bridge disappearance is
an external service check, not proof of internal reference-count retirement.
This is AP start after S3, not active-AP continuity through sleep, concurrent
STA-radio backhaul or GUI-click qualification.

S3 controller SHA256:
`5892c0d13b8ae382be39082c3a0d43ecb759cc07c7d6f59c31fc37433b223f5d`.
Failed post-S3 discovery trace:
`14be364e04db31034a6172da7db02068698b6b9feea6a3f4ec2d01b4228f6c99`.
Post-S3 departure and return traces:
`5e1a6dc7b2e7f2726aa0be78cceadd30447e85eeb72d17cb5a6799f4780b846f`,
`4cc0ec14fc6874dd92788cf075319749207b139b4d82380c0bc678edc2c2a2f7`.

## Native STA selection and fixture failures retained

Post-S3 WPA3 off/on withdrew carrier/address, then restored its saved profile
and20/20 traffic each way. Open STA selection through networksetup subsequently
passed DHCP192.168.73.26 and20/20 each way, with full controller/host restoration
success. WPA3 fixture selection likewise obtained192.168.73.30 and20/20.
These are ordinary native client calls, not mouse-driven GUI actions.

WPA2 q1 was an invalid test request: the newly compiled unentitled CoreWLAN
helper received privacy-redacted SSID/BSSID. airportd explicitly records
location-services filtering and ASSOC for network(null), returning-3900.
The fixture received probes but no authentication/association. Its guarded
controller failed and restored the host; this is not a WPA2 hardware failure
or pass. No privacy grant, entitlement or kernel value was changed. The
corrected runner uses the system's ordinary networksetup client with the
configured fixture SSID and password.

WPA2 q2 then completed real RSN pairwise-key exchange (hostapd), DHCP
192.168.73.35 for5e:bf:a9:68:58:f9 and20/20 packets in both directions.
Its wrapper failed afterward with a wrong-shell pipefail error before the
extra station/readback steps. A comment in the parent script had been edited
while Bash was executing its long-running child; that unsafe harness practice
was stopped and subsequent controls use an unchanged runner snapshot. The
exact shell-offset cause is not independently proven. q2's wrapper exit2,
successful AP-side key/DHCP/data receipts and host restoration are retained
separately; no full wrapper success is claimed and no successful retry replaces
those records. Open and WPA3 run on the unchanged later runner.

The complete archive was prepared from the already installed/tested frozen
bundle, not rebuilt. ZIP SHA256
`187a1a65cdc1e0fa5ce94c8ba067094c6e9ce057a94559c9cb0ae6d4845779cb`,
15,688,990bytes; the extracted bundle compares equal and retains UUID68F5.
Preparing this archive is not publication. Exact-image SA Query and the
failed final restored-traffic check are recorded below.

## Exact-image protected liveness and final restored link

The unchanged post-S3 image joined the controlled WPA3 fixture using native
networksetup, negotiated SAE/PMF, obtained192.168.73.30 and passed20/20
packets each way. Its MAC was7a:b8:96:a9:d2:25. A raw hostapd command
`DEAUTHENTICATE 7a:b8:96:a9:d2:25 reason=7 test=0` injected an unprotected
reason7 without removing the station. Generation1/epoch197 submitted one
protected SA Query (transaction63870); its CCMP/PN-verified matching response
arrived43.804022ms after the actual TX commit. No native leave/associate or
failure claim occurred in the80-second observer.20/20 packets each way and
the AP's retained authorized/MFP station state passed.

The separate raw `DEAUTHENTICATE 7a:b8:96:a9:d2:25 reason=7 tx=0` removed
only that station, with no command-generated Deauth transmission. Data then
elicited unprotected reason7. Generation2/epoch197 submitted five protected
queries (IDs81,16092,32685,29119,26115), accepted at all five real doorbells.
No verified response arrived. Exactly one failure claim occurred1025.084824ms
after the first submission. Native WCL leave followed, and fresh SAE peer
phases1/2 and RUN completed3.772953843s after the failure claim. Both query
observers ended with errors0 and encapsulation drops0.

Native policy selected saved LabAP instead of the fixture, restoring
172.16.66.219. The original fixture-bound stream received0/30 and reported
address-unavailable errors; this is not same-profile or seamless recovery.
Recovered guest-to-gateway traffic was20/20. The controller restored the host
AX211's exact original profile at20:59:50UTC, returning0. Healthy and forgotten
station phases completed on this exact image without off/on or forced rejoin.

The independent final-restored-q1 check began at21:00:16UTC. It retained
WPA3_SAE/DHCP and received20/20 guest-to-AX211 packets, but only18/20 in
reverse: the missing Linux ICMP sequences were1 and2. Its strict controller
returned1, not a full bidirectional pass. Forward maximum RTT was948.276ms;
the18 reverse replies had8.982--20.948ms RTT. No repeat replaces this failure.

Read-only airportd logs for20:59:45--21:00:45UTC show channel9 throughout the
traffic window and no new join/roam event in that interval. The host's native
profile activation/DHCP completed at20:59:50.294UTC, about26s before traffic;
there is no host reconnect in the probe window. These logs do not identify
the drop location or prove that every packet reached either adapter. Endpoint
ARP/ICMP captures were absent from the failed check. A separately labelled,
fixed two-phase diagnostic observes untouched and explicitly cold host-neighbor
starts; its results cannot retrospectively turn q1 into a pass.

That diagnostic completed at21:11:29UTC, without any radio/PS/profile change.
Untouched-neighbor traffic was19/20 forward and20/20 reverse. Forward ICMP
ID6666/sequence7 appears in the guest's Ethernet egress capture at
1789161016.052495, but neither request nor response appears at the host peer.
The existing ARP exchange succeeded before traffic. This localizes the observed
loss between guest Ethernet egress and host Ethernet ingress, not yet to a
particular driver or RF hop. Both endpoints have zero capture drops. The
80-second value-only driver observer reports no newstate/scan events,
errors0 and encapsulation drops0.

The second, predeclared phase deletes only the host neighbor entry for
172.16.66.219 on wlp0s20f3. The guest's real ARP reply returns and both traffic
directions pass20/20. Thus cold ARP alone is not established as the explanation
of either original loss. This pass is not a replacement for the untouched-
neighbor failure. Next distinguish actual IWN packet submission/firmware
receipt from the remainder of the two-BSS data path; no new production change
is justified by an endpoint-only capture.

Diagnostic guest/host pcap SHA256:
`0e42e431cbd0d9175392971f0028fb9825433c41dbe6d56c06199cd22213e198`,
`febc2fb7b269d62136747c977e36e6aa45b4eac49831eabf98bba08ac1a1f930`.
Driver trace:
`ba4d334c13d50f4c0f54578772cb3baf6d7f762917bab7a2c42672d0f364368b`.

The first per-packet TX observer (q2) was not qualified: although its80-second
trace had errors0, it identified/submitted/matched **zero** packets. Its wrapper
omitted a positive coverage check and returned0; that is not a valid TX result.
The separate250-packet streams were249/250 forward and250/250 reverse.
Forward ID51978/sequence232 again appears only at guest Ethernet egress,
not at the host peer; both captures report zero drops. Scalar-only calibration
then proved that all22 observed iwn_tx/classifier calls held only the26-byte
802.11 header in their first mbuf segment. The length guard correctly refused
to read the IP header past that segment. No out-of-bounds/kernel writes were
used to compensate. The q3 observer instead binds the successful existing
20-byte IPv4-header copy in ieee80211_classify to its immediate encap/iwn_tx
owner, and its controller requires positive identity/scheduler/receipt coverage.

Q3 completed at21:24:32UTC with250/250 packets in each direction. Both
endpoint pcaps contain exactly1000 matching ICMP identities, with no duplicate
identities or capture drops. All250 outgoing nonzero-IPv4-ID requests have
one identity, one scheduler submission, one successful iwn_tx return and one
matched single-frame firmware receipt (status0x01); per-cookie validation
checks each chain, not just equal totals.176 receipts have ackfail0;74 have
one or more MAC retries (maximum13). There are no observed newstate/scan
events, errors or encapsulation drops. This establishes usable packet-to-
firmware observation and one clean control, not the cause or closure of the
earlier losses: no lost request occurred in this observed q3 window. Aggregate
multi-frame receipts and zero-IPv4-ID replies are outside its identity scope.

## Alpha publication decision

The two production changes are supported by their exact-image functional
controls: single owned RXON through four native BSS transitions, healthy PMF
challenge without reconnect, bounded recovery when AP state is lost, strict
WiFi-only S3 recovery, and open/WPA2/WPA3 STA/AP regression. This supports
publishing an explicitly limited alpha checkpoint, **not** declaring the
failed zero-loss gate passed or closing reconnect/reference parity. The
previous downloadable image has the measured persistent-outage failure and
does not include the new recovery. The release warning must retain both the
first-request discovery failure and all three observed lossy final/diagnostic
checks (20/18,19/20,249/250 forward as described above), alongside the distinct
clean q3 control. No driver code changed during the subsequent diagnostics.

## Verified publication

The qualification report was committed/pushed as0b8c5609. At21:29:36--21:29:51UTC
the exact prepared ZIP replaced the asset in
[v2.4.0-alpha](https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha),
title `AirportItlwm Tahoe v2.4.0-alpha (f170870d)`, asset ID558047145.
API digest/size match187a1a65cdc1e0fa5ce94c8ba067094c6e9ce057a94559c9cb0ae6d4845779cb
and15,688,990bytes. A separate complete download compares byte-for-byte equal
to the installed/tested archive. The current warning retains the failed
zero-loss and discovery checks; full WPA3/reconnect qualification is not claimed.

Publication/readback receipts and the previous DCCB archive are outside the
immutable534-file evidence root:
`/home/dima/Projects/itlwm/aiam-single-rxon-release-20260911.4dg4NN`.
The old ZIP is preserved as previous-DCCB.kext.zip for recovery. Release history
was retained under the new current-image section. This publication completes
the two changes' alpha delivery, not the full functional-parity goal.

Healthy trace SHA256:
`4f724e390771ab7c9fd3a88472dbbfc8cf3c37b7a4d364998700389e7498b390`.
Forgotten-station trace SHA256:
`f5a337b160c51f544aff5512f2558cf3a065842d72012dc61dd431a0649deda2`.
The current outstanding surface includes first-request target discovery,
remaining AUTH/SAE peer-response failures, initial/recovery data loss,
native profile selection, complete GUI/security/sleep combinations, AP-through-
sleep and simultaneous radio backhaul, and actual IWM/IWX radio qualification.
