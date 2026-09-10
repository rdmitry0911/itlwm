# DVM scheduler queue retirement before reuse

## Evidence and correction candidate

The AP non-data station correction did not resolve the repeated post-pressure
restart fatal. Its complete loaded-image trace instead completed the last
management frame before the fatal, with an AP data descriptor still pending.
The problem therefore remains a lower transport/firmware lifecycle failure,
not an established Association Response ownership defect.

The pinned Intel Linux v6.12
[queue disable implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/tx.c)
first marks the queue inactive, clears four 32-bit TX-status words in SRAM,
then unmaps submitted descriptors. Its
[register definitions](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/iwl-prph.h)
place these words at scheduler base + `0x6a0 + 16 * qid`, independently of
the two configuration words at `0x600 + 8 * qid`. Queue enable, not disable,
assigns the successor sequence/cursors and programs its context.

The local AP and STA 5000-family stop methods disable scheduling and reclaim
descriptors, but neither clears the TX-status words. Both also publish a new
write/read pointer during retirement. Clearing the configuration words at a
later start is not equivalent to clearing the retired queue's TX status.
Global firmware initialization clears the whole area, so reset recovery cannot
prove that ordinary stop/reuse does so.

FIX_CANDIDATE: align both 5000-family aggregate stop backends with this queue
lifecycle: deactivate, clear only the selected four-word TX-status area, drain
the real submitted interval, and retain its empty transport cursor until a
subsequent start assigns a new sequence. Preserve NIC-access admission, AP's
completed FIFO-flush prerequisite, per-peer queue ownership and concurrent
STA isolation. The distinct 4965 layout and IWM/IWX backends are not changed.

The neighboring station hypothesis is not silently substituted for this
contract. Intel's complete DVM `iwlagn_rxon_disconn` path documents that
unassociated RXON clears the firmware station table; its software station
clear only updates host bookkeeping. An absent explicit REMOVE_STA in PAN
stop alone is therefore insufficient evidence of retained firmware stations.

## Verification gate

Compile the complete production stop/retirement methods under ASan/UBSan.
Seed nonzero scheduler SRAM and real pending descriptor intervals; check all
four selected words are cleared after deactivation and before release, with
adjacent contexts/status/translation data unchanged. Cover empty and nonempty
queues, wrap, multiple AP peers/TIDs, independent STA queues, NIC/flush/RXON
errors, repeated retirement and the unchanged 4965 path. The unchanged
predecessor must independently compile and fail the missing-clear check.

Only the exact built/loaded image's real nonempty AP stop/restart repetitions,
native open/WPA2/WPA3 DHCP/traffic, concurrent STA and actual S3 can qualify a
replacement release. Source alignment does not decode firmware assert
`0x22CE` or establish that this is its sufficient root-cause correction.

## Implemented source verification

Both 5000-family stop backends now clear the selected TX-status area after
deactivation and before descriptor release. They no longer publish a new
hardware cursor or another queue activation/status configuration at stop.
Existing start methods remain responsible for the successor sequence and
configuration. The separate 4965 branches retain their previous behavior.

The complete production AP and STA stop regressions pass under ASan/UBSan.
AP cases include every TID, queues 11/12/19, zero/one/three/213 descriptors,
four starting cursors including wrap, PS-transferred packet ownership,
multiple peers and error/retry paths. STA cases cover every TID, logical BA
endpoints differing from the physical interval, empty/nonempty/wrapped queues
and failed NIC admission. Seeded nonzero SRAM proves the exact four-word
clear before release and preservation of all other context, status and
translation words. No 5000-family stop doorbell is permitted by the harness.

Unchanged `e045c453` AP and STA production methods each compile independently
and fail at descriptor retirement because their selected TX-status words are
still nonzero (exit 134). The full payload suite, AP RX/TX A-MPDU, STA DVM
scheduler, client-materialization and multicast/backpressure regressions pass.
These are source-contract checks with external hardware I/O substituted;
build, exact-image activation and runtime remain the next gate.

The first whole-target build caught a missing forward declaration for the
existing SRAM-fill helper, now called earlier by the AP backend. The extracted
method harness's external-I/O substitute did not check that translation-unit
declaration order. The declaration is added beside the other memory helpers,
and the source integration check now covers its position. No failed-build
artifact was activated; whole-target build and symbol admission are repeated.

## Loaded-image nonempty retirement and failed restart

The corrected whole-target build at `cb6a7bc0` resolved all 1085 external
symbols. Private five-member AuxKC admission and transactional activation
passed without changing the four companion members. The 03:17:02 UTC boot
loaded UUID `6C186AD5-08FD-3F8B-8284-DD726B715325`, matching the frozen and
installed Mach-O SHA-256
`cb5a76918d383c956ab75f919e85d56676a74d6c42775c8052a56eeb9231e6e5`.
The initial primary STA check passed 5/5 and the independent management
upstream passed HTTP.

The first concurrent role-7 SAE/required-PMF AP passed 20/20 external-client
packets, an isolated cold-neighbor 10/10 reverse run and concurrent STA 5/5.
These role-7 checks use isolated static addressing, not DHCP qualification.
At 03:19:43 UTC, normal public AP stop during AP-originated UDP pressure
retired 208 pending aggregate descriptors to zero. A read-only observer
independently recorded the actual four-word SRAM clear at `0x80d4d0` in that
retirement call. After a separate 15-second dwell the primary passed 10/10.

The next public AP start at 03:20:18 still failed the reset-free restart gate.
Its Association Response used queue 7, index 61, non-data station 14, and
the firmware fatal arrived at 03:20:35: type `0x22CE`, PC `0x26294`, source
line `0x5E`, data `0x000000FF0000005E`. The independent serial dump has one
descriptor on queue 7 and one on primary queue 10, but zero on AP data queue
5 and retired aggregate queue 11. Hardware stop and automatic AP replay
followed. Subsequent 20/20, cold 10/10 and STA 5/5 traffic therefore proves
recovery after the failure, not a successful ordinary AP restart.

Normal cleanup retained primary traffic at 10/10. The bounded observer
terminated at 03:24:45 UTC with zero diagnostic errors, one fatal and an
empty stderr file. Its complete trace is preserved separately from earlier
candidate observations. This proves the loaded retirement correction ran;
it also disproves that this correction alone resolves the remaining firmware
lifecycle failure. No replacement release is qualified or uploaded. The
next discriminating control repeats this pressure stop/restart with WPA2
and PMF disabled on the same loaded image before attributing the fatal to
SAE/PMF or moving the same physical card to native Linux.
