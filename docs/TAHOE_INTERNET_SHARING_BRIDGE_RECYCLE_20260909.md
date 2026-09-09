# Internet Sharing bridge reuse failure — 2026-09-09

## Runtime evidence

The disposable IWN/6235 guest loaded source `ad910bab`, UUID
`5EBD9812-B43C-3720-8ADF-B3C85C68AE8B`. Role-7 APSTA, an actual S3/wake
and isolated cold-neighbor DTIM tests passed as described in
`TAHOE_IWN_AP_MULTICAST_DTIM_20260909.md`.

On the same boot, standard `configd -> airportd -> InternetSharing` started
a WPA3 AP, created its bridge and bootpd service, and supplied DHCP plus
bidirectional traffic to the external AX211. At 18:24:00 UTC a normal numeric
NAT disable stopped that service. The test then configured WPA2 and enabled
sharing at 18:24:05. The AP completed the real WPA2 four-way handshake, but
the client timed out obtaining DHCP.

The system daemon's 18:24:09 log identifies the failure before DHCP:
`SIOCIFCREATE2: Resource busy`, followed by bridge-creation/start failure
with error 16. The bridge was absent from the interface list. Static addressing
on the real AP interface later passed bidirectional traffic and cold ARP;
this is diagnostic separation, not a successful DHCP result.

A later standard disable/enable retry still failed. A bounded, read-only
observer at 18:30:09 captured actual `bridge_clone_create(unit=100)` under
InternetSharing, `ifnet_allocate_extended` returning 16, and bridge creation
returning 16. Its diagnostic-error file was empty. Another request at
18:39:25 reached `dlil_if_acquire` for `bridge100` and returned 16.

The latter observer also sampled the AP-reset boundary using the loaded
image's DWARF. Queues 5, 7 and 8 had zero queued descriptors both entering
and returning from AP reset. That excludes pending descriptors in those
queues at this particular stop; it does not prove that every earlier
resource reference was released or establish that this is unrelated to the
new multicast implementation. Mutex probes used to try to identify the
colliding object were unavailable and produced no object census. No claim
about that object's exact identity or reference count is made yet.

## Reference evidence and next action

The saved, complete 25C56 InternetSharing function at `0x10001705f`
(`mis_bridge_create`) checks the requested interface name, deletes an existing
bridge when required, and uses `SIOCIFCREATE2` to create it. Its real error
propagates to the sharing-start failure. This is not a DHCP protocol timeout
that should be hidden by a fabricated address or successful return.

The guest BootKC resolves `dlil_if_acquire` to `0xffffff80005db310`, ending
before `0xffffff80005dbc70`. Its saved Ghidra project was opened read-only
with a 40-CPU configuration. The initial C and function-body listing were
truncated by an incorrect no-return annotation on `lck_mtx_lock`; they are
not complete evidence. A separate bounded pseudo-disassembly over the exact
symbol range recovers the missing instructions without changing the project.

The recovered code compares the requested identity/name against existing
DLIL objects and branches to busy when their in-use flag remains set. The
same mechanism is documented in Apple's
[dlil_ctl.c](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/net/dlil_ctl.c).
The next task is to identify the colliding object's owner and incomplete
detach/reference lifetime, then repeat standard open/WPA2/WPA3 stop/start
with actual DHCP and traffic. The observed failure does not authorize clearing
kernel flags, forcibly freeing an interface, or manufacturing a fresh bridge
name to disguise a lifecycle defect.

## Retired-interface census

After final sharing disable, bootpd and InternetSharing were absent and the
interface list contained no bridge. The primary STA retained DHCP and passed
10/10 source-bound 1400-byte packets without another reboot. At 18:43:53 UTC,
a bounded read-only walk of the kernel's `dlil_ifnet_head` recorded nine
objects, including a `bridge100` object in Ethernet family 2 with flags
`0x1` (INUSE). The observer completed without diagnostic errors. The list link,
name, family and flag offsets came from the exact 25C56 raw instruction range,
not a different SDK layout. No interface creation/destruction was requested
during this short census; it remains a point-in-time observation, not a
lock-held proof of the entire teardown sequence.

A second census at 18:45:46 found the same object with 713 DLIL references.
The reference-counter offset (+0x94c) was independently recovered from the
matching `dlil_if_ref` increment and `dlil_if_free` decrement instructions in
the bounded range `0xffffff80005e1050` through `0xffffff80005e1290`; its
diagnostic-error file was empty. The retaining call sites remain unknown.
The next live test must trace reference acquisition/release on a fresh
standard bridge, rather than manually decrementing the old object's count.

This identifies a retained in-use bridge object after ordinary removal from
the public interface list. The candidate release remains held. The physical
user machine and other agents' virtual machines were not touched.

## Fresh-boot control and separate carrier defect

A fresh boot of the same `ad910bab` image loaded the same kext UUID. A bounded
90-second reference/free observer covered standard WPA3 sharing creation at
18:57:19 UTC, a real external SAE/PMF/DHCP client, traffic and ordinary stop
at 18:58:02. The bridge initially held three references. Deleting only the
client's scoped bridge ARP entry exposed failed neighbor resolution; its
count reached 12 before stop. The post-detach object changed to `bridge?`,
INUSE clear and raw detach byte zero. The remaining count reached 52, with
the principal unmatched observed acquisition stack in `udp_send+0x7ae`.
All diagnostic error files were empty. These stacks do not identify a driver
packet-ownership leak or justify manual reference decrements.

Crucially, the next ordinary start at 18:59:35 created `bridge100` successfully
without reboot. Thus the older EBUSY case after APSTA/S3 is not reproduced by
every stop/start, and a residual reference count alone is not proof of the
in-use identity collision. The EBUSY/S3 matrix remains open.

A separate isolated cold-neighbor test at 19:00:26 failed 0/10. Guest capture
contains five ARP requests on the bridge; external capture contains none.
The matching lower observer saw no `iwn_send_ap_data_frame` call for them.
Both the AP member and bridge reported inactive media despite an associated
SAE client. Apple's bridge broadcast path excludes media-inactive members,
while its learned-unicast route can still carry traffic. This explains why
client-originated DHCP and warm-neighbor traffic were insufficient coverage.

At 19:05:10, stack tracing identified the lost carrier sequence: lower HostAP
success published role-7 carrier/link-up; protocol removal then entered
`IO80211VirtualInterface::setInterfaceEnable(false)` through BSD disable and
`IOSkywalkNetworkInterface::disable` published status 1. Bridge protocol attach
subsequently entered ordinary APSTA enable and restored virtual association
state, but no status-3 carrier publication followed.

The correction and its qualification are tracked in
`TAHOE_APSTA_BSD_CARRIER_20260909.md`. It must not be described as a proven fix
for the retained-INUSE EBUSY case until that sequence passes independently.
