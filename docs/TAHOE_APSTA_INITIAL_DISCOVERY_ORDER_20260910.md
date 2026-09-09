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
