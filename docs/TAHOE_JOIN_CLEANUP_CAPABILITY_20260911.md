# No-candidate cleanup enrollment — 2026-09-11

## Production correction

IWM/IWX now carry the exact fresh-join generation through physical scan
admission and terminal handling. Both advertise `IEEE80211_C_SCANALLBAND`.
Their `endscan` therefore reaches common `ieee80211_end_scan_owned` with a
nonzero generation, including a real empty candidate census. However neither
HAL yet installs `ic_wcl_join_failure_scan` or acknowledges the complete
PRODUCER/LOWER/SAE failed-join retirement sequence. IWN does.

The common NO_NETWORKS producer previously entered FAILING on all three
families, then made `end_scan_owned` return before its existing next-scan
path. For an unenrolled backend no participant could finish that attempt.
This is a source-connected regression with an executable negative control,
not an assertion that an IWM/IWX radio reproduced it in this laboratory.

`ieee80211_wcl_join_scan_failed` now requires the backend cleanup callback
before changing the ledger. Without enrollment it returns false, leaves the
same DISCOVERY attempt intact and permits the existing next-scan path and a
later actual candidate. It does not acknowledge nonexistent cleanup, invent
an upper success/failure event or discard scan-generation ownership. IWN
continues to wait for its real three-part retirement instead of rescanning.

The callback's presence is a lifecycle enrollment prerequisite, not a claim
that the callback has completed. Physical receipt identity alone is not such
an enrollment. Full IWM/IWX fresh-AUTH SAE failure handling remains next:
capture the accepted join at engine admission, claim only its real peer
result, retain the generation through asynchronous lower cleanup, drain the
actual engine/transport and acknowledge each real participant. The separate
generation-zero accepted-roam AUTH observation/completion must not borrow a
fresh JoinAdapter generation.

Reference contract remains the exact 25C56 JoinAdapter and Core/WCL lifecycle
in `TAHOE_WCL_JOIN_FAILURE_CANDIDATES_20260910.md` and
`TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md`; the latter was re-exported
with the updated Ghidra `5995e24caa` native/JumpTable tooling and 40 actual
interfaces. This patch is a safe compatibility correction until the missing
backend completion ownership is implemented, not full reference parity.

## Executed tests

- The complete production join bridge, under ASan/UBSan, fails on previous
  source `9a830ac6`: with no cleanup callback, `scan_failed` returns true
  instead of preserving DISCOVERY (exit 134). This also reproduces with a
  callback removed after physical-generation admission but before terminal.
- Corrected complete helpers pass. The **actual extracted no-candidate
  branch** additionally executes its next-scan call for unenrolled backends,
  preserves the same ledger and permits subsequent AUTH binding. The enrolled
  control enters FAILING without an autonomous replacement scan. Candidate
  discovery and actual physical rescan are explicit fixture boundaries; this
  is not the whole `end_scan` function or an RF test.
- Linux full payload aggregate: exit 0, including adjacent three-family
  physical TX/reset, primary station/BA, scan and join suites.
- macOS exact bridge/branch, IWN SAE worker failure and all 22 shared
  management-queue ownership scenarios: exit 0 under ASan/UBSan.

Evidence SHA-256:

- Previous-source negative log
  `/tmp/aiam-join-cleanup-capability-negative-20260911.log`:
  `582776d71b5ebe3fa283fddf5fc01bd98690e9c022ba58150c8f40ed2d5e150c`.
- Linux aggregate `/tmp/aiam-join-cleanup-capability-linux-full-20260911.log`:
  `82afb717ac3b4d89056af832e7efb7bfd99018cfae28f4f3ac05a57449047e5b`.
- macOS selected `/tmp/aiam-join-cleanup-capability-macos-selected-20260911.log`:
  `7eb892e31dfcf018617c0a1f825cbba450c9c4b5d3a9fb1ee467db96812dbfae`.
- Full production manifest
  `/tmp/aiam-join-cleanup-capability-production-20260911.sha256`:
  `61462712c76c1614681a01eeb7349d603751149e73f353a53bda7824e74256bd`.

## Build/runtime checkpoint

At this source checkpoint the owned lab still runs `20dd3d8a`, boot
`C7693AA2-C5CD-4515-B7AA-508BAB9344F6`, loaded UUID
`4B2B526E-9D1D-3C51-9509-0C11C7BE30E9`. The new guard is not yet loaded.
The exact guest source mirror received only the changed production file and
test files after its prior production hash was verified. Its old Git HEAD
is not used as build identity; the manifest above is the build input gate.
Build, recoverable guest activation and on-air IWN regression follow.

No new IWM/IWX on-air qualification, all-mode UI/sleep/AP completion or public
release is claimed. Physical host `.22`, unrelated QEMU, base disks and the
user's local `Build/` were not changed. The original autonomous goal remains
active; this bounded guard does not replace the missing full cleanup layer.

### Later build and private-admission result

Production commit `17b63574` is pushed. The full source manifest verifies in
the guest mirror. Tahoe build completed with source ID `61462712c76c`; all
1085 undefined symbols resolve against the guest BootKC, without a
`thread_call_cancel_wait` dependency.

- Mach-O SHA-256:
  `681d9f223e1dbc032e8c8681316b0d3a73082ac32ba9555e5d238cbbd611b0ae`.
- Mach-O UUID: `7B77B2CB-D2B3-3B72-BCBF-5EFF73B5F53A`.
- Build log `/tmp/aiam-join-cleanup-capability-build-20260911.log`, SHA-256
  `00272d9572750803f344a8e570a6d275c483996441f6310fcbf935ac2078090c`.

The root-owned frozen candidate under
`/private/var/tmp/aiam-iwn-activation-cleanup-cap-61462712c76c-1` passed the
separate five-member AuxKC preflight. This was deliberately performed before
any canonical activation. The running boot remains `C7693AA2` and the loaded
image remains `20dd3d8a`. Canonical Mach-O remains
`ce03f7c2d130b8ceea90491114bc031cb4b7083fa98e093df0ddca2ccf21e878`;
canonical AuxKC remains
`a52d7d7fd8628c457b6ad584789820e9a845b19b41f8e51ed76c8f2a0b7a4229`.
The preflight reports `canonical_mutation=none`, `private_admission_result=PASS`
and exit 0. Local `preflight-summary.txt` SHA-256:
`0b851f7bceaf5b54f9d1840839bbf19a21141b057dacec07ebeed8abfb88e36c`.

New evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-cleanup-cap-runtime.gL7vxS`.
Its exact-parent launcher and frozen-candidate script are prepared, but the
new child disk has **not yet been created**, no shutdown/reboot has occurred,
and no new on-air result is claimed at this checkpoint.

The local pool has only about 1.6 GB free. A read-only census of all 373
project qcow2 images found no child of the old owned fixture leaf
`/home/dima/Projects/itlwm/overlays/overlay-live-0b90ed7-fixture-20260721T024518Z.qcow2`;
`fuser` found no user. It is 2,557,804,544 bytes, SHA-256
`f394ef589d7076f172bcbe7bfba0c1a9824000cc7ea93135f4e5d83b11825903`.
An archive copy is still running to
`10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/cleanup-capability-space-20260911.QuGFPs/`.
The first uncompressed transfer was stopped by its exact rsync PID with
SIGINT (exit 20); the separate compressed/sparse run continues. Neither that
interrupted transfer nor the in-progress copy is an accepted backup. **The
local file has not been deleted.** Before freeing it, verify remote length
and hash, recheck full backing census and open users. Never remove its parent
`overlay-live-bb7366b-20260721T012732Z.qcow2`, which has a real child.

After that archive completes, preserve the current working disk offline,
create its separate writable child/variables, activate the already-frozen
candidate there, and verify the exact loaded UUID before IWN regression.
Keep the original autonomous objective and IWM/IWX cleanup integration open.

## Full MVM SAE worker negative controls

While that same archive transfer remained live, the next full parity gate
was reproduced with **each actual complete** IWM/IWX SAE worker, cancellation,
owner predicates and retirement helper. `scripts/test_mvm_sae_peer_failure.sh`
takes `iwm` or `iwx`, extracts the exact family owner structure and functions,
then normalizes family spelling only. Crypto outcome, task admission,
scheduling and lower hardware completion are explicit boundaries. It does
not invent the absent join-generation slot in the real MVM owner structure.
The required assertion is intentionally red, excluded from the passing
aggregate; it is not a legacy-behavior PASS switch.

On both Linux and macOS, a consumed empty-body status-1 Confirm rejection
destroys the engine and requests generic SCAN, but leaves the accepted fresh
JoinAdapter in AUTH: `phase=2 generic_scan=1 owned_cleanup=0 producer_ack=0
destroyed=1 published=0`. All four actual executions fail the required
FAILING-state assertion with exit 134. The first IWX compile attempt lacked
its distinct task-admission boundary double and yielded no execution evidence;
the corrected r2 executes and reproduces the same production gap. This is
not an on-air IWM/IWX observation and does not establish the later cleanup
behavior that has not been implemented.

Evidence hashes:

- Linux IWM `/tmp/aiam-iwm-sae-peer-failure-baseline-20260911.log`:
  `667790d265cab63ee45d6c8bdf0c1a6630fc0d6eac2fc5b63fac5eb812fe457e`.
- Linux IWX `/tmp/aiam-iwx-sae-peer-failure-baseline-20260911-r2.log`:
  `5a724078efb770c3ebe67118828d4962ef2a2c100ecb2cc5a12342bd8d6593d1`.
- macOS IWM `/tmp/aiam-iwm-sae-peer-failure-macos-20260911.log`:
  `63439a344065158984eac716150f346104cbc6d6faf36bec43eb6dbee2097128`.
- macOS IWX `/tmp/aiam-iwx-sae-peer-failure-macos-20260911.log`:
  `3636a658d34ab497ce6b5f3a6a97f8ab649857637cccd157a80a1dd8701e7234`.

The implementation must capture the accepted join at real AUTH admission,
retain its failure through asynchronous state work, retire actual station /
BA / command / DMA and crypto owners, and only then publish once. The full
admission path and real lower-retirement continuations need additional
executable coverage; setting a synthetic generation in this fixture is not
a replacement. In particular, the existing MVM state request has immutable
epoch/join identity and real station-user draining, but its SCAN worker
unconditionally starts a physical scan and its generic commit can reenter.
Do not enroll the callback or call `cleanup_done(ALL)` merely to make these
negative tests green. `17b63574` is the compatibility guard during this work,
not the desired final failed-join implementation.

The prepared IWN runtime rejection runner in the new evidence root now uses
the tracked host-exclusive AP fixture and explicitly pins `80:e4:ba:20:ef:fa`,
matching the native directed request. It retains the bounded single observer
and only retries an explicitly rejected EBUSY request. It has not yet run on
the new image. The test-only changes do not alter that frozen candidate.

## Completed offline copy, activation and IWN runtime

This later checkpoint supersedes the pending archive/activation statements
above; those statements describe their original timestamps, not current state.

The compressed archive completed at 08:48:40 UTC. Remote length and SHA-256
matched the exact fixture leaf, followed by a new 373-image backing census,
local hash and open-user recheck. Only that now-recoverable local leaf was
removed at 08:49:10. Its parent was retained. The verified archive remains at
the exact `10.7.6.112` path recorded above.

The working `20dd3d8a` guest shut down normally. Its disk and UEFI variables
were copied offline with ordinary `cp` and verified with `cmp` and hashes;
**no extra backing-chain level was added**. The preserved working disk is
`aiam-iwn-mgmt-queue-runtime.5zvejm/tahoe-mgmt-queue.qcow2`, length
1,174,077,440 bytes, SHA-256
`fcc7a8ace06bd0146416d1b557e600b4f2232b3a54ac7384d7751668fc3eef1d`.
Its variables hash is
`281ec757b967ea88d53537d0fabf29842d9358b09ad758c55cd605cc825f0ed9`.
Both still match after the new runtime suite. The existing underlying
`aiam-iwn-bss-identity-runtime.mbFA1U` disk/variables also retain their
previous `421daa36...` / `8334cff9...` hashes.

Only owned QEMU PID 1346908 uses the writable copy
`aiam-iwn-cleanup-cap-runtime.gL7vxS/tahoe-cleanup-cap.qcow2`, separate
variables, IWN VFIO `0000:25:00.0` and management SSH 3338. It first booted
the copied old image as `5DEB9292-2813-415C-B385-9AB5D6348C44`.
Transactional activation `activation-20260911T085738Z` validated the exact
five-member AuxKC and reached READY. One normal guest reboot at 08:58:11
loaded the frozen candidate:

- Boot `E8E936A5-94B0-44A1-9C16-736EAB2470F5`.
- UUID `7B77B2CB-D2B3-3B72-BCBF-5EFF73B5F53A`.
- Mach-O SHA-256 remains
  `681d9f223e1dbc032e8c8681316b0d3a73082ac32ba9555e5d238cbbd611b0ae`.
- Saved WPA3 recovered automatically, DHCP lease at 08:58:47 UTC, without
  another selection or radio toggle.

### Traffic and profile matrix on that exact image

Payload is 1400-byte ICMP. Forward is guest to router/AP; reverse is physical
host AX211 to guest. Native profile requests are **not GUI qualification**.

| Case | Forward / reverse | Maximum RTT, ms | Boundary |
| --- | --- | --- | --- |
| Initial saved WPA3 | 20/20, 20/20 | 25.047 / 31.657 | Automatic after reboot |
| Recovery after controlled SAE rejection | 20/20, 20/20 | 17.961 / 20.267 | Valid BSS; repeated bad-BSS selection remains |
| WPA3 after actual S3 | **19/20, 19/20** | 26.983 / 20.682 | Wi-Fi only; losses coincide with a subsequent roam |
| WPA2 selection after S3 | 20/20, 20/20 | 94.244 / 182.480 | First native request, real RSN handshake and DHCP |
| WPA2 off/on | 20/20, 20/20 | 94.764 / 93.640 | Same profile and address, no second join request |
| Open selection after S3 | 20/20, 20/20 | 59.531 / 154.515 | First native request; not a proven cold scan cache |
| Final automatic WPA3 return | 10/10, **7/10** | 8.160 / **1046.074** | Initial reverse-path loss remains unexplained |

The later separate control from the same host to router and guest returned
20/20 each (max 54.569 / 22.829 ms). No profile change, toggle or ARP flush
was used between the failed initial reverse run and that control. This does
not erase the first result or identify which radio/bridge segment lost it.
The final short airportd window contains only the delayed auto-join metric,
not a demonstrated concurrent guest roam. Host NetworkManager has no entries
in that initial traffic window. Neighbor MACs match at the later readback.

WPA2 fixture ready/first request were 09:08:14/26. Its first actual RSN
completion was 09:08:33 and DHCPACK 09:08:37 (`192.168.73.35`). Native off
at 09:09:12 explicitly reached Off/inactive/no IPv4, on at 09:09:13 recovered
the same profile; external second handshake 09:09:15 and DHCPACK 09:09:17.
Open fixture ready/first request were 09:10:16/28; DHCPACK at 09:10:39
(`192.168.73.26`). No additional scan/retry was inserted into either first
selection. Both use the tracked host-exclusive fixture, pinned BSSID
`80:e4:ba:20:ef:fa`, channel 9 and 12-second readiness dwell. These success
runs do not close the earlier matched cold-cache discovery question.

### Real SAE rejection, still-distinct roam ownership

The wrong-password SAE/required-PMF fixture was ready at 09:00:21. One
directed native request was accepted at 09:00:36; no resubmission occurred.
The real peer observer ended normally at 09:01:48 with errors=0:

- 09:00:41, epoch 11/relay 3: empty Confirm, status 1, engine AP_REJECT,
  fresh-join claim 0 and generic scan.
- 09:00:54, epoch 30/relay 4: same rejection, fresh generation 2, actual
  cleanup participants 1/2/4 and one failure publication returning zero.
- 09:01:26, epoch 36/relay 6: another generation-zero bad-BSS attempt.
- 09:01:35, epoch 53/relay 7: fresh generation 4, the same three cleanup
  participants and one successful failure publication.

The valid BSS `9a:fb:5d:97:a9:02` completed SAE at epochs 35 and 58.
The second bad-BSS cycle required no second manual request. Thus this is a
regression check of enrolled IWN retirement, **not** closure of bad-BSS
selection or accepted-roam AUTH/completion. The separate exact-reference
`0x4a` and `0x50` lifetimes remain required. The updated-Ghidra full Core
AUTH/build/post and WCL roam-done consumers were re-read during this suite;
the 168-byte roam-done event triggers the FSM and deferred `tryReassoc`,
whereas the existing timer is not equivalent completion.

### Real S3 and the observed roaming loss

The first sleep helper exited 1 at its Ethernet guard: device removal landed
after its 20-second window. It never requested sleep and is not a sleep
failure/pass. The separate r2 helper ran after direct Wi-Fi SSH confirmed no
en0/en2 and no USB tablet. It requested sleep at 09:05:20. Native pmset
records entry at 09:05:50, a 30-second WindowServer acknowledgement timeout,
and one Normal Sleep/Wake. Owned monitor `s3-status-r5.log` confirms
`paused (suspended)`; serial confirms `ACPI SLEEP`. One `system_wakeup` at
09:06:42 produced `ACPI S3 WAKE`; DHCP was republished at 09:06:49. The same
boot/UUID and Wi-Fi-only interface inventory were verified before traffic.
USB management was restored only after both traffic probes ended.

Both post-S3 probes lost sequence 3. Their host log birth times are
09:07:15.079 / 09:07:16.260 UTC, placing those losses near 09:07:18.
The retained airportd log records `APPLE80211_M_ROAMED` at 09:07:18.294 and
the associated channel changing from 13 to 9; serial also records a real
WCL scan and target-RUN transition. This is a useful correlation with an
actual BSS transition, **not proof of the exact packet-drop site** or a
claim that S3 itself corrupts the radio. No GUI-after-S3 or active-AP-through-
S3 success is claimed; the WindowServer timeout remains visible.

### Evidence and next functional layer

The 75-file manifest in the runtime evidence root is
`sta-runtime-evidence.sha256`, SHA-256
`4c0fdef7176a2cf2f1c01bc272f5dc774fd08051e9c3d144fe4a8a924a1ab389`;
all entries verify. It includes the exact helper sources, activation and
boot records, both rejected/actual sleep attempts, peer observer, private-
fixture hostapd/DHCP outputs, all traffic results and bounded airportd logs.
The live serial writer is excluded; `serial-sta-checkpoint.log` is an
immutable snapshot. Credentials were not embedded in scripts or reports.

At the end of the suite, owned guest and host both returned automatically
to LabAP; guest is WPA3/DHCP `172.16.66.219`, same boot/UUID, management en2
restored. No fixture process or observer remains live. Physical `.22`, other
QEMU, base images, PCI bridge and the user's `Build/` were not changed.

`17b63574` has now completed build/activation/IWN ordinary-path regression,
with all adverse results retained. No actual IWN no-candidate failure was
forced by this matrix, and IWM/IWX remain source-tested only. Their real SAE
worker negatives are still red. The next user-facing priorities are the
reproducible BSS-transition interruption and accepted-roam failure lifecycle,
alongside complete MVM lower/producer/crypto retirement—not another static
selector inventory. The public qualified release was **not replaced** by
this mixed, incompletely qualified stack. The full autonomous goal remains
active; this checkpoint does not reduce its scope.
