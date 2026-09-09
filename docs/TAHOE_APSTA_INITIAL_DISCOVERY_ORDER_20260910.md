# Initial AP discovery versus the sharing interface cache — 2026-09-10

## Reproduced user-facing failure

The IWN/6235 guest loaded source `94b39c4b`, UUID
`4DB60C90-6ACF-3DA5-8C35-0700E97EE3B7`. Its clean boot at 20:52:36 UTC
followed the earlier, separately recorded VirtIO/configuration-service
failure. An ordinary saved WPA3 selection completed in ten seconds with
5/5 source-bound packets. A repeated role-7 APSTA run passed 20/20 external
client-to-AP, isolated cold-neighbor 10/10 and primary 5/5.

The 20:55:58 UTC sleep request reached actual S3, independently confirmed by
serial `ACPI SLEEP` and the owned VM's suspended state. Wake at 20:56:53
produced `ACPI S3 WAKE`. No post-wake VirtIO down/up was attempted. A fresh
temporary USB Ethernet was attached to a direct root port only after wake;
it obtained DHCP and passed HTTP to an isolated upstream lab endpoint.
It was not present during S3. The same boot and loaded UUID were retained.

Explicit external-client reselection joined the automatically restored
SAE/required-PMF AP. Without restarting that AP, the three paths again passed
20/20, isolated cold-neighbor 10/10 and primary 5/5. Capture records the
20:57:48 UTC cold ARP request/reply and the lower observer records queue-8
completion. Normal role-7 stop retained primary traffic at 10/10.

Standard WPA3 Internet Sharing then used the working new Ethernet upstream.
The real preference plugin started airportd HostAP, InternetSharing and
bootpd. The external client completed SAE but DHCP discovery received no
answer. `bridge100` existed with the primary `en1` as its only member; that
member and the bridge were media-inactive. The separately active radio AP
and its BSD data path were `ap1`. This is not the previous bridge-allocation
`EBUSY`, nor the earlier inactive-carrier-on-the-correct-AP-member defect.
No manual bridge-member change or static client addressing is used as a pass.

## Exact reference consumer and live cache evidence

The complete saved 25C56 InternetSharingPreference decompile is:

`InternetSharingPreference_25C56_20260801T1500Z/03_decomp/InternetSharingPreference_25C56/all_decompiled.c`

The reference's initial resolver at offset `0x1614` queries
`CWWiFiClient interfaceWithRole:1` and stores the primary name at global
`+0x70d8`. It separately queries role 3 for the AP name at `+0x70d0`.
When the AP name is absent, it retains the exact primary-name object as its
fallback. Its preference path replaces the selected primary name with the
cached AP name only when these names differ, then passes that interface to
NetworkSharing. This is an upstream-consumer dependency on initial identity
publication, not a driver-owned bridge-membership API.

The startup callback at `0xebf` initializes the pair. The dynamic-store
callback at `0x1827` and power callback at `0x2240` call the resolver again
only if the cached primary name is absent. Merely adding an AP interface
after an already-discovered primary does not refresh this fallback pair.

On the reproduced boot, configd logged Wi-Fi-name detection at
23:52:48.435 local time. Airportd's first `ap1` attach event arrived at
23:52:48.846, 411 ms later; its inventory then initially contained only the
primary and subsequently admitted `ap1` as the APSTA child.

At 21:04:15 UTC a bounded read-only PID probe sampled the two cache globals
in the live configd process. Its plugin mapping was obtained from that
process's `vmmap`; both globals held the identical nonzero tagged object
pointer. The observer reached its terminal without errors. A separate
read-only CoreWLAN process simultaneously resolved role 1 to `en1` and
role 3 to `ap1`. Thus the live inventory is correct now, while the sharing
plugin still holds its earlier same-name fallback.

This proves the failed binding was established at boot, before S3. The
post-sleep test exposed it; sleep is not demonstrated to have created it.

## Driver boundary to correct

The primary starts asynchronous BSD attachment in `AirportItlwm::start`.
The default APSTA role is currently materialized only at the lower
`WCL_SCAN_REOPENED` edge, after firmware capability discovery. This already
precedes radio-available publication, but it does not precede primary BSD
discovery by the consumer above.

The next implementation must order supported AP identity availability before
that first primary discovery. It must preserve real firmware/NVM capability
admission, pending/failed AP carrier semantics, unsupported-family STA use,
all three hardware families, and teardown safety. Firmware metadata parsing
before radio activation and the existing virtual `setBSDName` completion
edge are under review; there is no implemented correction in this note.
An arbitrary startup delay, forced configd restart, a fake capability or a
manual bridge rewrite would not close the producer ordering defect.

Qualification must include cold boots with the consumer's first name lookup,
real open/WPA2/WPA3 system sharing, DHCP, isolated cold-neighbor traffic,
routed traffic, repeated stop/start and APSTA S3 recovery. The latest kext
archive remains held, not promoted on the basis of role-7-only tests.

## Initial publication implementation and exact BSD ordering

The implementation now reads IWN's embedded firmware metadata after EEPROM
attach, without starting the radio. It uses the normal complete image parser
and discards its temporary upload buffer. A failed read clears partial TLV
capabilities and preserves the ordinary STA power-on retry. The reader now
checks allocation/decompression, complete section bounds and image ownership;
both preview and normal upload release all section pointers with the buffer.
IWM/IWX already perform firmware/NVM preinit during attach, and their existing
family/capability admission predicates remain unchanged.

The primary BSD matching request moves to the end of initialized controller
startup. The firmware-admitted AP is materialized first through the existing
reference `registerService(2)` path. Only then is primary `deferBSDAttach(false)`
issued, followed by controller publication. Lower radio-ready publication
remains an idempotent recovery check; neither identity publication starts AP
radio service or reports an unconfirmed carrier. AP allocation failure does
not disable an otherwise usable primary STA.

The saved YAML's proposed `setBSDName` completion interpretation was rejected
after inspection of the exact 25C56 binary. `IOSkywalkNetworkBSDClient::start`
starts at `0xffffff8002a06574`; its call to the provider's `setBSDName` slot at
`0xffffff8002a0682d` precedes the net-provider allocation call at
`0xffffff8002a06bc7`. That allocation invokes
`kern_nexus_controller_alloc_net_provider_instance` at `0xffffff8000987dd0`.
The complete raw start range ends at `0xffffff8002a06cf8`. A callback at the
name assignment would therefore release primary discovery too early.
The IOKit option is genuinely
[synchronous service matching](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/IOKit/IOService.h),
not a fixed sleep. Startup additionally observes the actual AP ifnet through
`ifnet_find_by_name`, compares it with the registered interface's ifnet and
releases the lookup reference before requesting primary matching.

All 12 shipped IWN images pass the production reader's metadata-preview and
subsequent-upload tests under ASan/UBSan. Failure cases cover allocation,
decompression, malformed/truncated images, partial-capability rollback,
repeat calls and retained upload-buffer ownership. The old reader fails the
negative-control ownership assertion. The production initial-publication
methods are separately exercised for ordering, capability denial, allocation
failure, failed BSD lookup and balanced references. Source checks preserve
post-init readiness ordering across IWN/IWM/IWX.

This change is not yet runtime-qualified. The first live boot must demonstrate
the AP's actual BSD attachment before primary discovery and distinct cached
names in the real sharing consumer, followed by the security/DHCP/traffic and
S3 matrix above. A proposed name, a successful source test or merely earlier
invocation is not substituted for that gate.

## Loaded-image initial discovery and native sharing

Source `893a3114` built with all 1085 external symbols resolved. Private
five-member AuxKC admission and transactional activation passed, preserving
the four companion members. The guest boot at 21:37:44 UTC loaded UUID
`5455B34A-AA04-34EE-8CC5-82B373798841`, matching frozen Mach-O SHA-256
`c308f9b5dbe8e3e52f88643aee35441a3384ed7c62da1d404c47f8db77f4d2b1`.

The serial boot record confirms IWN's attach-time firmware preview succeeded
with PAN capability, then reports the actual `ap1` ifnet attached before
primary matching. Airportd observed `ap1` attachment at 00:37:57.390 local
time and `en1` at .397. The sharing plugin's first Wi-Fi-name detection followed
at 00:37:58.427. A fresh CoreWLAN reader resolved primary/AP to `en1`/`ap1`.
At 21:39:20 UTC a read-only observer of the remapped live configd plugin found
different nonzero cached AP and primary objects, and terminated without errors.
No plugin restart, bridge rewrite, fixed startup sleep or radio toggle was used.

Native standard sharing then ran WPA3, WPA2 and open in sequence on this same
boot. Each used the real system producer, InternetSharing and bootpd, with a
working independent Ethernet upstream. Each external-client join obtained
DHCP and passed 20/20 1400-byte client-to-gateway packets. Each separately
awaited bridge-scoped cold-neighbor run passed 10/10 with client power save
enabled, captured ARP request/reply and successful firmware queue-8 completion;
the observers reported no diagnostic errors. The bridge member was the active
`ap1`, not the primary fallback. Each mode also passed routed HTTP from the
external Wi-Fi client through the guest upstream to a fixed laboratory payload.
The routing fixture changed only one temporary destination route and restored
it; the host's wired default management route stayed unchanged.

After normal sharing disable, primary traffic recovered without selection or
off/on. Its first ten-packet transition run lost one packet and received the
following nine; a subsequent check passed 5/5. This is service restoration,
not a claim of zero-loss native AP-to-STA role change.

A separate concurrent role-7 test on this image completed external SAE group
19, required PMF/BIP and power-save admission, with primary WPA3 still active.
It passed 20/20 client-to-AP, isolated cold AP-to-client 10/10 and concurrent
STA 5/5. The independent USB management interface was removed while awake
before requesting S3; emulated VirtIO remained available before sleep.

The 21:47:38 UTC request reached actual S3, independently confirmed by serial
`ACPI SLEEP` and the owned QEMU's suspended state. Wake at 21:48:38 produced
`ACPI S3 WAKE` and retained boot epoch and loaded UUID. A fresh USB Ethernet
was attached only after wake and verified as a working upstream; no post-wake
VirtIO interface down/up was attempted. The primary retained DHCP and the
driver restored the AP without another AP-start command. Explicit external
client reselection completed SAE group 19/required PMF/BIP. The recovered AP
passed 20/20 forward and isolated cold-neighbor 10/10 with queue-8 completion
and no observer errors; concurrent primary traffic passed 5/5. This proves
service recovery, not automatic client continuity or every GUI profile policy.

## Post-S3 native restart and exact retained-reference owner

Normal role-7 stop retained primary traffic at 10/10. After removing the
temporary AP address, native WPA3 sharing on the same boot obtained actual
DHCP and passed 20/20 client traffic, isolated cold-neighbor 10/10 and routed
HTTP. The real sharing plugin still held distinct AP/primary name objects.
The following WPA2 start failed at 21:52:27 UTC with `SIOCIFCREATE2: Resource
busy`, before DHCP setup. This is not a passed post-S3 security-mode matrix.

The old bridge object was detaching with two I/O references, while the
detacher's pending queue was empty. A two-second, 40-sample kernel spindump
at 21:54:40 UTC located its wait at exact 25C56 `0xffffff80005cf41d`, the
detacher's outstanding-I/O loop. The same snapshot located the network work
queue thread at `0xffffff80006f4456`, inside the ioctl event callback at
`0xffffff80005d42bc`, then `ifnet_ioctl`, IONetworkingFamily and AppleVirtIO.
That thread had stopped running around the S3 wake, before bridge retirement.

The matching read-only binary ranges were recovered with 40-CPU headless
analysis. `nwk_wq_thread_cont` at `0xffffff80006f4370` serially calls each
queued function; its pending head is `0xffffff8001162b78`. The ioctl event
callback at `0xffffff80005d4270` reads the ifnet at entry +0x18 and command
at +0x20, calls `ifnet_ioctl`, then releases its retained I/O reference.
Its enqueue producer takes that reference before submitting the work item.
These exact raw ranges, rather than a truncated C decompile, establish the
layout and ownership used by the bounded live observer.

At 22:04:24 UTC, that observer traversed all 367 pending work items and
terminated without an error or remaining cursor. Exactly two ioctl events
targeted the detaching bridge: `SIOCADDMULTI` and `SIOCDELMULTI`. Its add/del
pending flags were both set, and its I/O count was exactly two. Thus the
references belong to real queued multicast work stranded behind the blocked
AppleVirtIO ioctl; they are not unexplained Wi-Fi-owned references and must
not be decremented or bypassed by the Wi-Fi driver.

This is distinct from the earlier USB interface occupying the detacher.
The next control must repeat native sharing restart after S3 without an
emulated Ethernet device present during sleep, while retaining the same
loaded Intel image and using a fresh post-wake upstream. The release remains
held until that full runtime gate completes.

## Ethernet-free S3 control and completed native security matrix

The old guest shut down normally. The same overlay and unchanged Intel image
were then booted with the emulated VirtIO Ethernet device omitted entirely.
This controlled fixture change does not modify Apple's Ethernet driver or
claim to repair that unrelated transport. Wired host management and other
VMs remained untouched. The new guest boot at 22:07:53 UTC retained loaded
UUID `5455B34A-AA04-34EE-8CC5-82B373798841` and the frozen Mach-O hash above.

This second cold boot independently verified initial AP discovery order:
airportd saw AP attachment at 01:08:04.904 local time, primary at .911, and
the sharing plugin first resolved Wi-Fi names at 01:08:05.663. Its remapped
live cache again held distinct AP/primary objects. Native WPA3 sharing passed
DHCP, 20/20 client traffic, isolated cold-neighbor 10/10 and routed HTTP.
Normal stop retained primary traffic at 10/10; the bridge reached I/O zero
and detached state, and the network work queue was empty.

The first following APSTA test stopped before AP creation because its initial
STA precondition lost three packets during a real background WCL BSS change.
DHCP rebound at 22:11:04 UTC and a later check passed 5/5 without selection or
off/on. This transient remains part of the non-seamless roaming surface; the
failed precondition is not relabeled as a passed APSTA run. A fresh role-7
run then passed SAE group 19/required PMF, 20/20 client traffic, isolated cold
10/10 and concurrent primary 5/5.

The temporary USB Ethernet was removed while awake. A delayed sleep guard
verified both Ethernet interfaces absent and the work queue empty before
`pmset sleepnow` at 22:13:40 UTC. Actual S3 was independently confirmed by
serial `ACPI SLEEP` and QEMU's suspended state. Wake at 22:15:03 produced
`ACPI S3 WAKE`; the boot epoch and loaded UUID did not change. A fresh USB
upstream attached only after wake, obtained DHCP and passed HTTP. The network
work queue remained empty. There was no Wi-Fi toggle or repeated AP start.

Explicit external-client selection on the restored AP completed SAE group
19, required PMF/BIP and power-save admission. Post-wake traffic passed
20/20 client-to-AP, separately measured cold-neighbor 10/10 and concurrent
STA 5/5. Normal AP stop retained primary traffic at 10/10. The temporary
static AP address was removed before testing standard sharing.

Native WPA3, WPA2 and open sharing then completed sequentially on this same
post-S3 boot. Each obtained real DHCP, passed 20/20 1400-byte client packets,
isolated bridge-scoped cold-neighbor 10/10 and routed HTTP to the fixed lab
payload. The client state independently verified SAE/required PMF, WPA2-PSK
and open admission respectively. Before the WPA2 and open starts the old
bridge reached I/O zero and detached state; the new active bridge used the
AP member. WPA2's live network work queue was empty. No manual bridge
rewrite, reference decrement, userspace daemon restart or guest reboot was
used between these security modes.

Final native sharing disable again retired the bridge to I/O zero and
detached state, with an empty network work queue. The host's ordinary
wireless profile and wired default route were restored/preserved. The serial
qualification interval contains no matched driver panic, device-timeout or
firmware-fatal diagnostic. This closes the candidate's native post-S3
security/DHCP/traffic gate, not all GUI profile sequences, lossless roaming,
automatic external-client continuity or equivalent IWM/IWX hardware coverage.

The qualified ZIP is
`f15e21c9d29c185c021f4c962512fada3f1dd9d05c66c85e0ecdb4a05ccc42d8`;
its extracted Mach-O was rechecked against the loaded file and frozen image.

The public `v2.4.0-alpha` Tahoe asset was replaced at 22:24:52 UTC and its
notes updated at 22:24:53. A fresh GitHub download was byte-compared with
the qualified ZIP and independently hashed to the value above. The previous
asset was retained privately for rollback. Release notes distinguish this
IWN runtime qualification from remaining GUI/roaming and IWM/IWX coverage.
