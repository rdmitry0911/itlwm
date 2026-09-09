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

The candidate release remains held. The physical user machine and other
agents' virtual machines were not touched.
