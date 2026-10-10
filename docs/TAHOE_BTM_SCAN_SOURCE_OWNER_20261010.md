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
backing images remain outside this cycle's mutations. Build, loaded image,
sleep regression and additional release qualification follow separately.
