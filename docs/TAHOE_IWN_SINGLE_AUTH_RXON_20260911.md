# One owned target RXON for STA authentication

Status: production candidate and executable regressions pass; new-image RF
qualification is pending. This does not close all SAE/reconnect failures.

## Measured failure before the change

On loaded `59448436-3947-3A8D-8738-402DF8FED85C`, boot
`6886D4CD-BC3A-45BE-8D73-4ACC65414EF7`, native LabAP roaming from
`9a:fb:5d:97:a9:02`/13 to `82:c3:97:84:51:ca`/9 failed before SAE.
In `ready-depart-q2`, AUTH serial23/epoch151 programmed at
1789157770097889127ns. RXON/index70, ADD_NODE/index72 and LINK_QUALITY/index73
all received their actual firmware replies within19ms. At
1789157775097957874ns the owner terminated with ETIMEDOUT, stage4,
rxon_submitted=1, rxon_accepted=1, prep_submitted=3, prep_accepted=3,
beacon_seen=0. No scoped beacon was delivered through guest RX during that
five-second interval. This is not an independent over-the-air beacon census.

The lower transition issued two full unassociated RXON commands:
old BSSID/channel13 at1789157770097873359ns, then target/channel9 at
1789157770097878071ns, only4.712us later. Both had AID0 and filter0x4;
stale association/decrypt flags are therefore not this observed discrepancy.
The preceding successful reverse control also had the duplicate,4.851us
apart, so coalescing it is a justified candidate, not yet a proven universal
cause of missing beacons.

The failed control retained only206/250 guest-to-gateway and207/250
host-to-guest packets. Native recovery first rejoined the source BSS; the
80s control ended there and failed its requested-target gate. A later native
transition reached channel9, outside that control. It does not erase the
first failure or prove traffic continuity. DTrace ended with errors0.

## Reference and implementation

[Pinned Linux v6.18 Intel DVM RXON implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/rxon.c)
passes candidate `ctx->staging` into its BSS disconnect RXON, then restores
station/PAN state. Local source SHA256:
`c37ec7322e59349cd168d2622a74841fd711401f2cc815811a11094677323233`.
The updated-Ghidra `5995e24caa` export of Apple core `handleAuthEvent`
(`ffffff800159f7f0`) forwards actual auth status/reason to JoinAdapter; it
does not permit synthetic success in place of radio readiness. Export SHA256:
`b3b3dd2f513b4f74324fa8ae690247205e1904f4ae3765d648903c691e76f5af`.

The existing APSTA coalescing now applies to every identity-owned STA AUTH
or RUN-to-ASSOC continuation. Logical old-association cleanup is unchanged;
only `iwn_auth()` submits the candidate RXON and its existing broadcast
station/link-quality/PAN restoration. INIT/SCAN/non-owned reset behavior is
preserved. The command acknowledgments and fresh target-beacon gate remain
mandatory; no new sleep, fabricated receipt or timeout-as-readiness is added.

## Executable checks and evidence

The test extracts the real lower state function plus AUTH owner/helper.
Four new cases cover plain STA/live APSTA crossed with AUTH/RUN-to-ASSOC;
one checks that RUN-to-INIT still sends its reset. They require exactly one
owned RXON, zero unowned RXON, logical cleanup and one generic commit only
after receipts and beacon. Hardware/IOKit/generic calls remain boundary
doubles; these tests are not RF evidence. The old code fails the new command
count assertion. The candidate passes all40 AUTH cases, and the complete
payload aggregate including all five real nonblocking AUTH-body modes.

Working evidence (not sealed):
`/home/dima/Projects/itlwm/aiam-auth-readiness-runtime.ndbJOE`.
Aggregate SHA256:
`9a8c6843170f092b501b945d22efa9ec32fa1352f25f13037da23cfe0c904e7e`.
Failed baseline trace SHA256:
`d55b4487e56b29880fce4a0ba9da538f9dea2a5d92f9187df4886a68263ca5c2`.

Observer q1 incorrectly tested an FBT bool return as a full register; only
its stage4 terminal snapshots were true terminals. q2/q3 explicitly cast to
uint8_t. The decisive controls use q3. Public firmware/value layouts were
checked with static_assert on Linux and Tahoe (RXON50, request96, lease144).
No private ItlIwn/ic/mbuf/node offsets, key material or kernel writes were used.

Next gate: build/admit/load a new identity, repeat the same native two-BSS
requests with command/beacon and bidirectional traffic capture, then normal
open/WPA2 and sleep controls. The current public release remains ee501d78;
this candidate is not yet its downloadable image.
