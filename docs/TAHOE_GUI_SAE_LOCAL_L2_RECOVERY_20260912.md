# GUI saved-SAE recovery with same-L2 TCP/ICMP — 2026-09-12

P0.1 remains repeated actual GUI open/WPA2/WPA3, saved profiles and recovery.
This phase removes the preceding routed/NAT peer and AP-fixture teardown
from the measured data path. Existing host LabAP STA `wlp0s20f3`/.226,
BSSID`82:c3:97:84:51:c9`/channel153, is the peer for guest `en1`/.219.
No host route/profile or guest driver/daemon change occurs. Wired host and
USB guest management remain independent; physical.22/neighbor QEMU untouched.

Production remains5e98d640/AF16, boot7A5FAA36-5FAA-451F-A32B-70EB251B8660,
native WiFiAgent5894/runs576. The12s read-only GUI prerequisite passes by
10:46:00. A local service binds only172.16.66.226:18089, permits only guest.219
and one controlled82944-byte file, and flushes1024bytes every0.5s. Its actual
40-second TCP delivery overlaps normal connected operation. Each control
also sends600x1400-byte ICMP probes each way at100ms spacing, with80-second
captures on both Wi-Fi interfaces. This is post-DHCP service coverage, not
continuous traffic from the GUI click or an RF capture. Do not compare these
loss counts directly with the earlier60-probe/one-second-spacing controls.

| Actual GUI action, UTC | Observed result |
| --- | --- |
| ControlCenter LabAP disconnect10:46:43, same saved row reselect10:47:02 | Inactive/no DHCP10:46:47; SAE/DHCP.219 by10:47:05; forward600/600, reverse598/600; HTTP200/hash PASS in40.026s |
| System Settings off10:51:36, on10:51:59 with no selection click | Inactive/no DHCP10:51:40; automatic SAE/DHCP.219 by10:52:15;593/600 each way; HTTP200/hash PASS in40.218s |

The actual menu unjoin10:46:43.580 reaches airportd DISASSOC at.592 (12ms)
and succeeds at.613. The previous delayed native-service defect is not
reproduced. Both HTTP files match SHA256
`1d4065ad48ea2444d90e735735e1a6cc7d315281c890d900641dbd64fbbe82db`.
Both controllers retain their failures at the zero-ICMP-loss gate: successful
association/DHCP/TCP does not turn that gate green, nor does that gate alone
prove that SAE association or TCP service is broken.

## Preserved packet boundaries

First control retains guest BSSID02/channel13 at both readbacks. Native logs
show no ROAMED or new post-initial RSN completion in its service interval.
Host retainsc9/channel153; its bounded NM/supplicant journal query has no
entries, which is not exhaustive proof of all lower hardware activity.

Reverse id52559 sequence104 reaches guest at10:47:16.462964 and has a guest
reply at.462992, absent in host capture. Sequence179 is emitted by host at
10:47:24.115822, absent in guest capture. Neighbors succeed. These are two
different missing boundaries, without a proven driver/AP/RF cause. The next
GUI SCAN request10:47:16.968 is later than the first reply, not proof that
scan entry caused its loss. Host counters2571/2571/zero kernel drops;
guest2757/3777/zero. Cross-endpoint clocks are not calibrated for one-way
latency; sequence matching, not timestamp subtraction, establishes boundaries.

Second control changes guest BSSID02/channel13 to ca/channel9. Native RSN
completion10:52:43.561 precedes ROAMED10:52:47.547 and BSSID_CHANGED.555.
Complete capture census, independently of ping timeout notices, finds:

- Forward id1594 missing replies60,258,271,272,273,287,364. Only271 reaches
  the host and has an emitted reply absent in guest; six other requests appear
  at guest output, not host input.
- Reverse id55198 requests191,278,279,280 are absent in guest.265,277,293
  have guest replies absent in host.

Nine BSD timeout notices are not the identities of the seven finally missing
forward replies. The census preserves that distinction. Host2556/2556/zero
kernel drops; guest2721/3203/zero. HTTP completes despite these gaps. No further
GUI selection/recovery occurs while either first failure is inspected.

## Terminal state and next implementation boundary

The exact validated HTTP PID2279872 is stopped10:54:48 only after both service
controls/captures finish; bounded controller exits143. Port18089 is free,
no AP/monitor fixture exists, no other process is touched. Final guest read
retains same boot/AF16/WiFiAgent5894, LabAP/SAE/.219/channel9; host remains
LabAP/.226/channel153. No third reselection-after-off/on control is claimed.
Actual post-S3 GUI and remaining cross-security/recovery cells remain open.

Next is the separately reproduced accepted-roam/public-scan conflict and its
incomplete reference progress/terminal lifecycle, documented in
[`TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md`](TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md).
Implement it coherently in the driver using exact reference producers and
consumers; do not suppress all GUI scans or add start without real completion.
Its correction cannot be claimed to explain these packet gaps without runtime
evidence. The current full-lifecycle owner oracle still fails4/4 requirements
on unchanged production bodies; that is a regression baseline, not hardware
proof or a new correction. Remaining ordinary GUI cells retain P0.1 priority.

Immutable source `/dev/shm/aiam-gui-l2-recovery-20260912.dgvxIG`.
Durable archive `/home/dima/Projects/itlwm/aiam-gui-l2-recovery-runtime-20260912.8T5CMX`:
all73 files verify, manifest SHA256
`4692ea2a1d31ad30be2c9f7db1ebcb77359f1e323b9714e28f6aa77e6baaf256`.
Fresh implementation scratch `/dev/shm/aiam-gui-roam-lifecycle-20260912.2AEEv7`.
No production/release bytes changed in these runtime-verification controls.
