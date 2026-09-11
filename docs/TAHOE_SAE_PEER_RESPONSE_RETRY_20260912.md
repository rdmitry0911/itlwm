# Missing same-exchange SAE peer-response retry

Status: the single-Commit omission reproduces failed native connection on the
published f170870d image; its unchanged pass-through control completes SAE,
DHCP and traffic. A driver-resident retry candidate now passes actual core,
all-family worker and timer tests on Linux and Tahoe, including both complete
payload aggregates. It has not yet been built, loaded or radio-qualified.
The production-candidate section below does not relabel the baseline runs.

## Controlled radio experiment

Guest IWN/6235 retains UUID68F5B0D9-4863-3627-B30C-CC0CC111BB58 and boot
B4FEB27A-EDDA-4A70-AA35-29B68EF3AE1A. Owned AX211 hostapd2.10 advertises the
fresh SSID AIAM-SAE-RETRY-0912, BSSID80:e4:ba:20:ef:f9, channel9, group19 SAE,
mandatory PMF and CCMP. The guest station is62:ad:f9:b9:cc:f4. Wired host
management and guest USB diagnostic SSH are separate from both radios.

The host's normal managed STA producer is closed for the fixture, preventing
its background scans from interrupting the same physical AP radio. Cleanup
restores the exact original NetworkManager profile. No physical10.90.10.22,
QEMU lifecycle, PCI bridge, base disk or kext activation is involved.

The fixture uses hostapd's existing external-management test interface. It
passes received management frames and actual TX statuses unchanged through
MGMT_RX_PROCESS/MGMT_TX_STATUS_PROCESS, except for **one** well-formed first
SAE Commit received for this AP. That frame is intentionally not processed
by hostapd. No password is changed and no Authentication frame is fabricated.
Radio TX has already occurred and the guest's firmware reports its real
successful completion. Public authentication-body digests identify the exact
omitted frame; crypto keys/passwords are not sampled or printed.

The first q1 request failed before driver entry: the reused native-client
wrapper whitelisted only the previous fixture SSIDs and returned2. There was
no SAE TX and no omitted frame. Its controller result1 and ordinary host
restoration are retained. The new task-local native wrapper admits only this
new SSID, with exact boot/image guards and password via stdin. No macOS privacy
grant or entitlement was changed.

## Negative control q2: one omission destroys the exchange

One ordinary networksetup request starts at22:11:11.605732179UTC.

- Target SAE epoch220/relay23 prepares Commit ticket9223372036854775853,
  transaction1/status0/body98, at1789164679080500713ns on the guest clock.
- Its actual TX terminal result0 arrives1789164679083732964ns and the engine
  consumes it successfully.
- Hostapd records that Commit at1789164679109746677ns on its own clock and
  omits only that frame. Independent monitor capture contains the same complete
  frame at1789164679.109339s, with identical public-body SHA256
  `663fe5197309df39b9d742d6492f1aacc4386a2c62338bdb113deb4fb31eac12`.
- No second target Commit, target peer response or same-epoch retry occurs.
  At1789164683776606979ns the owner retires with request_scan1 and destroys
  the engine: **4.692874015s after its successful TX terminal**.
- networksetup reports failed join/error-3912. Its process status is0; that
  is not a successful connection result. Native recovery later starts a new
  SAE exchange with saved LabAP02/epoch242, then moves to ca/epoch243.

At the90-second terminal, the guest has LabAP DHCP172.16.66.219, not the
requested fixture. The target link check returns1. The overall controller's0
means the diagnostic and cleanup completed, not that target connection passed.
Hostapd control records exactly one omitted frame,30 passed management frames
and five passed TX statuses. Monitor capture contains one Authentication frame
for the fixture with zero capture drops. The observer ends with errors0 and
encapsulation drops0.

Guest and host clock values are not subtracted from one another. The timeout
interval is entirely guest-clock, and the monitor/control identity comparison
uses the same host clock plus the exact frame digest. Generic tcpdump labels
Algorithm3 as Reserved/[|802.11]; a bounded radiotap/pcap decoder confirms full
capture lengths and decodes the actual SAE algorithm/transaction/status/body.
This is decoder support, not a truncated packet or an Open-System frame.

## Positive control q3: the same fixture succeeds when nothing is omitted

After q2's normal fixture teardown/restoration, the same AP configuration,
SSID, station identity, hostapd external-management path and exact guest image
are exercised with an explicit forward-all controller. There is one new native
request, no radio toggle and no guest restart.

Epoch251/relay26 sends Commit ticket9223372036854775858 and Confirm
ticket9223372036854775859. Both real TX terminals are successful; the engine
accepts peer Commit and validates peer Confirm, followed by RUN at
1789164883257132610ns. AP-side AP-STA-CONNECTED follows, and DHCP supplies
192.168.73.28.20/20 packets each direction pass. The monitor contains exactly
the four complete expected Authentication frames, zero capture drops, and
public-body digests matching the hostapd receive events. The observer has
errors0 and encapsulation drops0.

Two later native background roam scans fail to find a replacement while the
fixture remains connected. This is not a claim that all scanning/profile policy
is solved. The passing target data check is retained independently.

Normal fixture cleanup completes22:16:31UTC and restores the exact host
profile, normal power-save-on setting and wired route. Without a manual guest
selection/off-on, saved LabAP returns. A separate restored-link check passes
20/20 each direction on172.16.66.219. The same boot/image is retained and no
observer/capture remains. This control does not erase the failed first request.

## Reference and implementation boundary

Pinned [Linux v6.18 mac80211 authentication](https://raw.githubusercontent.com/torvalds/linux/v6.18/net/mac80211/mlme.c)
retains auth_data's current SAE transaction/status/body, allows three total
Authentication transmissions and schedules a two-second peer-response timeout
after an ACKed SAE frame. ieee80211_sta_work repeats ieee80211_auth for the
same owner rather than starting fresh password-derived state on each retry.
Source SHA256:
`db2115573e018bbca11ba6f430d803f6a8659f4396f313a1f011dad8c12d5aad`.
The hostapd2.10 delayed-Commit test and its MGMT_RX_PROCESS/TX status handlers
provide the test-interface contract. The available updated-Ghidra Apple
handleAuthEvent export forwards real status into JoinAdapter; it does not
expose Broadcom firmware's exact SAE retransmission timer. Linux timing must
not be presented as independently recovered Apple firmware timing.

Local ieee80211_sae_engine_tx_complete clears prepared_body immediately after
successful Commit/Confirm TX. The worker handles start, pre-doorbell private-
gate retry, terminal TX and received peer frames, but has no peer-response
retry event. Those existing private-gate retries are **not** protocol retries.
The common management timer eventually revokes the epoch and cancels the
exchange. The radio negative control executes precisely that missing edge.

FIX_CANDIDATE: adapt the bounded same-exchange Authentication retry to the
driver-resident SAE engine and IWN/IWM/IWX owners. Required complete boundary:

1. Retain only the current public serialized Commit/Confirm while awaiting its
   peer response; preserve anti-clogging token and Confirm counter correctly.
   No password reload, new scalar, association epoch or BSSID for a retry.
2. Distinguish a protocol retry from rollback of a never-submitted frame.
   Every actual retransmission obtains a new lifetime-monotonic TX ticket and
   uses the normal HAL descriptor/doorbell and real completion paths.
3. Arm from a real terminal receipt, retain a bounded attempt count and
   monotonic deadline, and handle no-ACK/reset/cancel distinctly. Never turn
   failed TX into success. Preserve the same upper JoinAdapter/reassoc owner.
4. Queue work outside leaves; only the serialized crypto worker mutates the
   opaque engine. Queued peer progress wins over a stale timeout. Stop, S3,
   detach, replacement and stale/duplicate callbacks cannot transmit later
   or consume a successor. Exhaustion must use the normal owned failure path.
5. Execute actual production core/worker tests on Linux and Tahoe for HnP/H2E,
   Commit and Confirm omissions, token/duplicate peer handling, deadline
   edges, cancellation, retry exhaustion and all three HAL admission paths.
   Existing body-string contracts are not sufficient evidence for this layer.
6. After source tests, commit/push the candidate, then build/load that exact
   image and repeat omission and forward-all controls, real S3, off/on, target
   switching and open/WPA2/WPA3 STA/AP regression. Publish the kext with
   accurately scoped runtime receipts, retaining any failed controls.

The omission experiment does not explain every earlier natural SAE timeout:
the exact missing over-the-air frame in depart-q5 was not captured. It proves
a concrete capability gap worth closing, not a universal retrospective cause.

## Evidence

Working root: `/dev/shm/aiam-sae-peer-retry-20260912.SgPpSi`.
Durable root: `/home/dima/Projects/itlwm/aiam-sae-peer-retry-runtime.aMvMK5`.
Both failed setup and all negative/positive/restored results are retained.
All68 entries of EVIDENCE.sha256 verify; manifest SHA256:
`e23bbb3ded4422d293944780007b19263531d1b98ebcf7cad00af2cea04edd0f`.
The durable root is frozen; do not append later implementation/runtime work.
Q2/Q3 trace SHA256:
`7a13c72e200f361b16a031a18e66966f997410557116d8a679f29ecd5551d7e5`,
`20197d022fee9f3184f660d77135697cd75d208fad8332115e86275406f84be2`.
Q2/Q3 monitor pcap SHA256:
`79a6269bb46b935cab71afa00b098c1ea0a819400b4549bd59f6433a20202cb2`,
`31cb0ec6a99e1b5f91e4e94335bb06be08f7a6f83a1f96ae293b61d17a2e169d`.

The driver and public release remain f170870d/UUID68F5. This diagnosis does
not warrant rebuilding the same code or publishing a new kext without a fix.

## Production candidate and executed source checks

The shared opaque core retains its current public serialized Commit/Confirm
after successful terminal TX. An exact last-terminal-ticket retry prepares
that same body, without reloading a password, regenerating scalar/element,
incrementing Confirm or changing the BSSID/epoch/relay. A new monotonic ticket
is required even after pre-doorbell rollback. The limit is three actual
successful transmissions per serialized body; one anti-clogging response can
replace that body and start its own bounded budget. Failure/completion/destroy
scrub the retained body. Nonzero TX terminals still fail closed: this change
does not reclassify no-ACK, reset or cancellation as success or retry them as
though a frame had been ACKed.

IWN, IWM and IWX record an absolute two-second peer deadline at the actual
terminal-event enqueue. Delayed crypto-worker execution does not renew it.
The worker signals a retained interrupt event source; only the main workloop
arms the timer. Timer callbacks only inspect the current public owner and
schedule the serialized worker, never execute crypto or wait for a private
gate. Queued peer progress wins before timeout claim; duplicate/dropped peer
frames do not restart the deadline. Timeout claims revalidate selected-BSS,
credential, epoch and peer-RX admission under the established leaf order.
Old timers re-read the successor deadline rather than transmit for the old
exchange. Stop/S3/detach flags close admission; detach drains worker scheduling
and workers before removing both event sources and destroying the engine leaf.

After exhaustion the exact current AUTH owner arms the existing native
watchdog for one tick. That preserves ordinary owned AUTH timeout handling
instead of inventing an AP status or directly issuing SCAN from the crypto
worker. Final failure publication can therefore follow up to one watchdog
tick later than the two-second peer deadline; exact Apple firmware timer
parity is not claimed. A genuine late peer response can still win before
native timeout consumes the AUTH epoch.

Executed checks on Linux and Tahoe:

- 30 scenarios compile and link the complete production core plus vendored
  hostap/mbedTLS: HnP/H2E, zero/one/two Commit and Confirm omissions, unchanged
  bodies, actual token-bearing crypto continuation, stale/duplicate completion,
  ticket reuse, pre-doorbell rollback, exhaustion, failed TX and destruction.
- 26 scenarios per IWN/IWM/IWX compile each complete actual worker, terminal
  receipt, identity, timeout claim and main-workloop timer-drain body. Only
  hardware, crypto results, scheduling and kernel leaves are explicit doubles.
  They check receipt-anchored timing, exact deadline, peer priority, cancellation,
  replacement, no obsolete-timeout busy loop and native timeout publication.
- 12 scenarios execute the complete timer helper with IOKit/clock doubles,
  including each allocation/add-source failure, rounded-up delay, invalid gate,
  retained objects, callback during removal and repeated shutdown.
- Existing 21 IWN failure/retirement scenarios and both full payload aggregates
  pass. This is source execution, not new on-air evidence.

The first core-test fixture used a four-byte token that the chosen hostap peer
parser cannot consume as an HnP token. The corrected peer uses its supported
32-byte token and additionally checks that the original scalar/element are
unchanged. This fixture failure is not a driver retry failure. The actual
driver serializer remains bounded by its existing variable-token contract.

The independent existing IWM/IWX peer-rejection retirement gates were rerun:
both still fail their original semantic assertion (exit134), retaining
generic_scan1, owned_cleanup0 and producer_ack0. This candidate does not claim
to fix that separate all-family failure-integration surface.

Fresh working evidence (not the frozen baseline root):
`/dev/shm/aiam-sae-peer-implementation-20260912.soPlBh`.
Tahoe tests run in a separate source tree
`/private/var/tmp/sae-peer-implementation-tests.vya2G9`; the build mirror and
loaded kext are unchanged at this checkpoint. Source-test archive SHA256:
`093149edc0b55d71d916d17dcdc9909fb12570b89990dc453385071c055ed815`.
Linux aggregate SHA256:
`8c23f5ffff6e1d48c814ae71fe20dadc048644f980a414f54fd0ab4a6ab71898`.
Tahoe selected/aggregate SHA256:
`c5a6ec1722298cc94cf27e344052c07261fd49da0da972637ef1f5f6e25962dd`,
`ab514a9b8ae319351aa0b9ee4d7d119f33b25317cc47247b3f596b4a7e2ebe98`.

Next: commit/push this candidate, build and load its exact source manifest,
repeat the single-Commit omission and forward-all controls, add Confirm-loss
and bounded exhaustion controls, then S3/off-on/STA/AP security regression.
Only those new RF receipts can justify publishing a replacement kext.
