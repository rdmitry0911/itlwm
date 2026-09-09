# Physical WCL scan observation census — 2026-09-09

## Reproduced user-visible failure

The released `150d7e73` IWN/6235 guest loaded UUID
`431DFEA8-4B59-3A52-B4B9-91677A7619B1`. A separate controlled open AP was
advertised by the external AX211 on 2.4 GHz, concurrent with its managed
connection. Wired management and the guest's saved WPA3 STA were unchanged.

At 17:26:04 UTC a native CoreWLAN single-channel scan issued a real physical
command and received that AP. The production metadata builder recorded sample
1155123983, no prior publication receipt, RSSI -33 dBm and flags `0x4046`.
CoreWLAN returned one result with measured RSSI. This proves on-air discovery,
not merely the host AP's ENABLED status.

The controlled AP was disabled at 17:27:38 UTC. At 17:27:41 another native
request issued another physical scan. Its terminal nevertheless published
the same node with sample=issued=1155123983 and flags `0x46`: no fresh
observation had occurred. Apple created a new BSS object with RSSI/time zero,
and CoreWLAN returned the disabled AP with `AGE=0`, RSSI 0. Both bounded
observers completed without diagnostic errors. Intel node offsets came from
the loaded candidate's DWARF; Apple inner offsets came from matching 25C56
BootKC disassembly, not the different 26.3 layout.

## Reference and correction

The complete saved 25C56 DriverKit
`AppleBCMWLANScanAdapter::processScanResults` at `0x10018a9a4` validates
the records in its incoming firmware buffer, builds beacon metadata for
each accepted record and calls `postMessageInfra`. It does not walk all
historical driver cache identities for each physical completion. This is
producer evidence, not a claim that the reference's separate system cache
instantly evicts a vanished BSS.

The Intel terminal collector previously walked the entire node tree. The
RSSI receipt correctly avoided refreshing an old measured value, but could
not prevent the enclosing old identity/IE message from creating a new Apple
cache entry. The physical terminal now restricts its value-only snapshot to
beacon/probe observations received in this request's window and channels.
The exact active channel plan must match the terminal generation.

The observation stamp is independent of RSSI validity: an accepted real
frame without usable signal metadata remains a discovery result. Starting a
local IBSS/AP clears received-observation state. Ordinary immediately admitted
requests capture their window before lower submission, including a terminal
that arrives before begin() returns. IWN's queued initial request moves that
floor to its existing post-WRPTR STARTED edge, excluding the foreground
predecessor's observations. Duplicate STARTED or the later setter acknowledgement
cannot move the floor again. Cancellation, terminal ownership, multi-band
continuation and the separately requested legacy cache route are unchanged.

Old and out-of-plan nodes are rejected before capacity accounting. This does
not flush the node tree, invent RSSI, widen channel permissions, change dwell,
or force the system's own cache retention policy.

## Verification boundary

The ASan/UBSan production-function regression now executes the actual
observation recorder, channel-plan predicate, terminal collector and
controller's activation/window method. Cases cover a queued predecessor,
exact-boundary receipt, wrong generation/backend, duplicate acknowledgement,
already completed/cancelled ownership, both bands, absent plans, invalid
channels, unavailable RSSI, and stale/out-of-plan entries at capacity. The
old collector fails the first stale-census assertion. Existing physical-scan
lifecycle, exact-plan, queued-band, SSID-refresh, APSTA epoch/roam and firmware
dwell regressions pass.

## Matching-image runtime qualification

Source `236f264e` built successfully with all 1083 BootKC symbols resolved.
Private AuxKC admission passed without canonical mutation; transactional
activation retained the four companion members. The disposable IWN/6235
guest loaded UUID `779BA606-C79D-36AA-8C6A-27948BF9BAAE`, matching frozen
Mach-O SHA-256
`d89ade2d0669f520130144662c5662f52419e034c652854ca4d1a7a21ebf45be`.
The node observer used this build's DWARF; the Apple consumer offsets came
from the matching 25C56 BootKC.

The controlled AP was enabled after loading. At 17:43:26 UTC a physical
scan observed it with sample 40271363 and published measured RSSI -33 dBm
with flags `0x4046`; another fresh receipt at 17:43:35 also reached Apple.
After AP disable at 17:43:56, the 17:43:58 physical scan still visited that
node with its unchanged observation/sample/issued stamp 49919609, but did
not build or publish a beacon message for it. CoreWLAN's own retained
record had age 22893 rather than being recreated with age/RSSI zero.

A second observed request at 17:44:42 returned zero results. Its physical
collector still visited the old node, and again did not publish it. Both
bounded disabled-AP observers completed without diagnostic errors. No cache
flush or intervening reboot was used. This proves suppression of stale
physical-result publication, not a replacement for Apple's cache policy.

After re-enable, the 17:45:21 physical scan received sample 155698963,
published `0x4046` metadata and returned the AP with measured RSSI and age
zero. The ordinary public open-network selection then completed in 11
seconds without an error or radio toggle. The external AP confirmed real
authentication/association and its DHCP server acknowledged the guest lease.
Source-bound 1400-byte ICMP passed 20/20 packets in each direction.

An open-STA sleep request at 17:46:59 reached true S3 (`ACPI SLEEP` and
the owned QEMU suspended). Wake at 17:47:47 retained boot epoch and loaded
UUID. The system automatically selected another saved WPA3 network, not the
open test network: the 25-packet test addressed to the old open lease failed.
This is not counted as open-profile automatic restoration. Airportd's
17:47:49 scan did receive the open BSS with measured RSSI and age 3 ms; its
auto-join policy marked that saved profile deferrable and requested the WPA3
network. No change to that policy is inferred or implemented here.

Explicit ordinary open selection after wake completed in eight seconds,
regained DHCP and passed 20/20 source-bound 1400-byte packets without off/on.
The test's preferred-network entry is temporary. Full last-selected-profile
and GUI behavior remain separate qualification work.

## Concurrent APSTA regression and release hold

The same candidate started a role-7 pure-SAE/required-PMF AP alongside its
WPA3 primary STA. The external AX211 completed SAE group 19 with BIP on the
required shared channel. Static-address 1400-byte traffic passed 20/20
client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets.

The 17:51:25 sleep request reached true S3; the owned monitor received wake
at 17:53:10, followed by `ACPI S3 WAKE`. Explicit client reselection completed
SAE/PMF, and client-to-AP passed 20/20 again. The independent emulated USB
Ethernet management path did not return, but physical Wi-Fi SSH confirmed
unchanged boot epoch/loaded UUID and primary-to-gateway traffic at 5/5.

The delayed AP-to-client check failed 0/5: its ARP entry was incomplete,
despite the client remaining associated. A subsequent client-originated ARP
exchange allowed reverse traffic to resume (the observed run recovered to
3/5). Removing only the test client's ARP entry reproduced 0/5. A bounded
production TX observer reported successful submission of the outgoing frames;
this is not evidence of successful over-the-air broadcast delivery. The
additional cold-neighbor/broadcast failure is under investigation, not a
qualified APSTA/S3 pass or an established regression introduced by this scan
change. Older warm-neighbor traffic tests do not cover this case.

Normal AP stop reached the lower and owner zero-result terminals at 17:59:19;
primary traffic passed 10/10. The candidate archive has been prepared but is
held from release while the newly exposed AP broadcast path is investigated.
Archive SHA-256:
`4853bc8661c7955d936a7f86b714ad296e5773fee025918cbffb39ebb904e869`.
Its extracted Mach-O matches the loaded candidate.

Missing physical 5-GHz reception, all UI/saved-profile reconnect combinations
and equivalent recent IWM/IWX hardware qualification remain separate work.
