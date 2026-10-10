# BTM scan ownership during saved network replacement

Pending BSS Transition scans now belong to the exact source association, not
only to a BTM request number. A native join or reconnect can replace that
association without receiving another BTM. Old work must then retire its
logical request rather than scan, delete successor results or reject the old
dialog token to the new AP. This shared correction applies to IWN, IWM and
IWX; current hardware qualification remains limited by the IWM 9260 RFKILL.

## Reproduced source replacement

The complete production RX parser, retained request helpers and timer worker
from `b277d8bc` reproduce six failures on Linux and macOS ASan and UBSan.
Changing the current source BSSID, SSID or association epoch before the timer
or during its lower abort call still starts a scan and deletes cached nodes.
The stale request stays active. All six controls compile and fail the intended
no stale scan assertion with exit 134; the previous 24 scenario control passes.

Node cleanup exposes another boundary: free_allnodes calls free_node, whose
lower node callback runs after BA cleanup. Without a post callback owner
check, replacement during that boundary still submits a scan and overwrites
the successor's scan flags. The seventh historical control also compiles and
fails its intended assertion. Callback scheduling and lower command results
are explicit fixture doubles; these are software reproductions, not observed
GUI or on air transitions on the current card.

## Association identity and retained handoff

Arming a BTM snapshots the nonzero association epoch under the existing
selected BSS leaf, alongside its source BSSID and SSID. Before candidate
confirmation, scan admission, retries, channel lookup, candidate filtering
and confirmation validate that exact RUN source. A mismatch clears only the
logical BTM record. No physical HAL scan lease, DMA descriptor or terminal
receipt is cleared or invented.

The existing request generation checks after lower abort and start calls now
also reject independent source replacement. Another check after node cleanup
prevents old work from publishing flags or submitting its command after a
callback admitted a successor. A current replacement BTM retains its own
pending timer and request identity.

Confirmed candidates deliberately retain their existing descriptor fenced
source leave and target handoff. This scan owner correction is not a blanket
epoch cancellation of that separate continuation. Its later descriptor and
retarget ownership remains a separate audit surface.

The saved 25C56 reference export WCLRoamManager::linkDown at
`ffffff8002105ae4` clears logical roam fields and invokes timer and policy
cleanup. The export is in the existing reference metadata package under
`aiam-gui-roam-lifecycle-handoff-20260912.HyoPu3/reference-metadata-91`.
That supports retiring logical work on source loss, not fabricating an Intel
physical completion. The source epoch is local ownership identity, not an
Apple ABI field, firmware capability or wire token.

## Software qualification

Linux and macOS ASan and UBSan pass the complete payload aggregate and shared
WNM source contract. The production fixture passes 89 scenarios: the previous
24 dispatch cases plus 65 source ownership cases. New coverage includes
BSSID, SSID, same BSS epoch replacement, zero epoch, leaving RUN, role change,
missing BSS, malformed lengths, replacement inside lower start on success
and failure, stale candidate filtering and confirmation, epoch wrap and
preservation of seven confirmed continuation controls. The node cleanup
controls cover both independent source replacement and a new BTM request.

The WIP archive SHA256 is
`eb03bcd3c186cf411d854b8bf63b9a59c6272dcee554861e73080fa13842e5ee`.
The isolated macOS stage is
`/private/var/tmp/btm-source-owner-20261010.o23IKI/source`.
The original dirty guest checkout, physical host 10.90.10.22 and all VM
backing images remain outside this cycle's mutations.

## Exact build and loaded regression

Production `f17c88b793e11e15279c0a5379d13044be16de62` is pushed and independently
matched to the remote branch. Exact committed Linux and macOS payload
aggregates pass, followed by a successful ordinary Tahoe build. All 1088
imports resolve against the running guest BootKernelExtensions.kc. The clean
isolated guest checkout is detached at that exact production commit.

- Source identity: `9759d7d1ae6a`.
- Mach-O UUID: `41139CBA-7667-320F-A897-2D4AD76C5BC2`.
- Mach-O SHA256: `415a2c12a219f44a60c0c9a30c0a2743b8b8a860e1abe94fb011c7d6fcf8030f`.
- Loaded boot: `0C9FEE46-4388-4E61-80B4-068A02B2C16B`.

The new private AuxKC transaction preserves four companion members and
rollback, reaches READY and verifies whole installed bundle equality. One
guarded guest reboot loads the exact image. Reboot SSH returns 255, and the
first postboot banner observation also times out; the second independently
verifies the new boot, loaded UUID, complete bundle and en2 management.
No VM restart or installer replay is used to reconcile those observations.

Two private monitor observations show paused suspended state; serial records
ACPI SLEEP. Power history records 45 seconds of actual S3, a power button wake
and WakeTime 1.254 seconds. The first wake SSH banner times out; the second
verifies the same boot, image, full bundle and en2 default route. WindowServer's
30 second sleep acknowledgement timeout remains recorded.

The bounded sleep trace and separate postwake trace both finish with COMPLETE
and remote DTrace status zero. Actual sleep cancellation records IWM disable
666.631 milliseconds and scan reset invalidation 4.636 microseconds. Four
postwake native controls and four additional traced controls retain Off and
management; traced On returns NotReady in 1.827, 1.862, 1.916 and 2.435
milliseconds. Utility process exit zero is not radio or connection success.
The cold boot power readback was On while lower initialization reported fatal
2; that public readback is not a successful radio admission either.

No actual BTM RX or source validation probe runs under hardware RFKILL. This
loaded regression does not qualify on air source replacement, physical scan
abort, SAE or restored Wi-Fi traffic, nor new IWN or IWX hardware behavior.

## Additional laboratory release

The additional unsigned Debug asset is
[AirportItlwm-Tahoe-BtmSource-f17c88b7.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-BtmSource-f17c88b7.kext.zip).
It contains the exact installed and loaded bundle, with build, installed and
extracted trees equal and no packaging rebuild.

- Asset ID: `627848930`; size: 15,717,686 bytes.
- ZIP SHA256: `7e2025092f1990d1e68444234bb0f132bdf6b1356c68cd6822f99bdadfee0b30`.

The default and all fourteen older assets retain their immutable metadata.
The complete previous notes remain unchanged below the new qualification
entry. This is an additional LAB artifact, not a promoted default build.
An independent release download has the same ZIP SHA256 and compares equal
byte for byte to the locally validated archive.
