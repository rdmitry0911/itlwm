# Tahoe protected join completion after committed RUN (25C56, 2026-08-05)

## User-visible gap

AX211 could complete a saved WPA2 association on air after a Tahoe Wi-Fi
off/on cycle, briefly recover DHCP and pass traffic, yet remain "Not
connected" in System Settings and then disconnect.  The controlled host AP
completed authentication, association and the RSN four-way handshake; the
failure was after radio security, inside the driver/framework join handoff.

A passive `AirportItlwmRegDiag` capture on the released `789b8f24` candidate
proved this sequence:

1. the cached candidate reached `IEEE80211_S_RUN` and controller link status
   became active;
2. the accepted parent link-up was published and DHCP restored
   `192.168.87.226/24`;
3. WCL later invoked `setWCL_JOIN_ABORT` while net80211 was still in RUN and
   requested `JOIN_ABORT_COMPLETE`;
4. the abort returned the station to AUTH/SCAN and removed the route.

The existing serial log identifies the missing terminal before that abort.
For several cached-candidate attempts, auth success and candidate-matched
association completion both succeeded, but the RSN completion action logged
that its WCL link and connect-complete builders were called with
`ic_state=3` (`IEEE80211_S_ASSOC`) and skipped.  IWX queues its S_RUN backend
transition; received EAPOL can therefore open the kernel PAE port before the
queued newstate task commits S_RUN.  The former protected path attempted
key-done, WCL link-up and connect-complete only once at that early PAE edge.
There was no protected equivalent of the already-correct open-network
`STA_OPEN_RUN_DONE` continuation.

## Reference order

The existing Tahoe 25C56 reference package on `10.7.6.112` was used before
changing code.  No new decompilation was required.  The complete combined
decompile is:

`/home/dima/Projects/ghidra_output/cr357_wcl_join_lifecycle_boundary_repair_20260508_1005_decomp.txt`

SHA-256: `ddac02b370679db93cd2f5cbca84eb3ed31a44ad1c47a32ac59d71cbc0630c72`.

The matching static slices are under:

`/home/dima/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/`

Relevant exact functions and assembly SHA-256 values are:

- `AppleBCMWLANNetAdapter::handleLink` at `0xffffff800154590a`:
  `af9ae0d0a8af7bd9e03c2fd117b2742bbb4af6d1af1c3d3a4d3b742c0ef5614e`;
- `AppleBCMWLANJoinAdapter::getBSSInfoAsync` at `0xffffff800157adec`:
  `5db5846a1056674cf1213b34d01bc93e170d041eaa01496fb429cd1bd843efa6`;
- `AppleBCMWLANJoinAdapter::sendConnectComplete` at
  `0xffffff800157d472`:
  `96c8b95c5b22509d8c1f752b871dc287916557bad801906ca65bff9b0aa43978`;
- `AppleBCMWLANJoinAdapter::getBSSInfoAsyncCallback` at
  `0xffffff800157db28`:
  `6dac7b2e68cc9c59df1b5a072a98ddf9ea043419336ae9140a675d3f027817c4`;
- `WCLNetManager::connectComplete` at `0xffffff8002111594`:
  `ddf8714149dbf94298913016afe7f3cbf48f9478900010007016ad9ee99eca68`.

The reference does not make the supplicant/key terminal the sole owner of
connect-complete.  JoinAdapter first requests current BSS information.  Its
callback validates/materializes the associated BSS and only then reaches
`sendConnectComplete`.  WCL consumes that terminal and
`WCLNetManager::connectComplete` subsequently calls
`updateLinkState(true, false, true, ...)`.  This keeps key completion and
current-BSS/RUN completion as distinct facts even when firmware reports them
close together.

## Local contract repair

The Intel bridge now follows the same observable ordering:

- `IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE` remains the one real kernel-PAE
  port-valid/key-done edge;
- when an asynchronous backend commits S_RUN with that port already valid,
  net80211 emits the credential-free `IEEE80211_EVT_STA_RSN_RUN_DONE` fact;
- the continuation does not repeat key-done;
- protected WCL link/connect completion requires S_RUN, RSN enabled, current
  selected-BSS epoch and BSSID, successful authentication, validated
  candidate association, and an unused completion lease;
- both `connectCompletionPublished` and `joinTerminalObserved` are claimed
  before the two asynchronous WCL messages, preventing a nested duplicate
  carrier from replacing the accepted candidate;
- synchronous IWN/IWM behavior is unchanged: if port-valid occurs after
  S_RUN, the original RSN event still completes key and WCL in one gated
  action.

This is an event-order repair, not a retry or forced state transition.  It
does not invent RUN, reuse stale identity, extend a timeout, or suppress a
real join abort.

## Verification

Static contracts passed before runtime build:

- `test_tahoe_wcl_join_completion_gate_contract.sh`;
- `test_tahoe_wcl_auth_assoc_completion_contract.sh`;
- `test_net80211_public_initial_bssid_pin_contract.sh`;
- `test_tahoe_wcl_link_down_reconnect_contract.sh`;
- `test_iwm_iwx_duplicate_newstate_contract.sh`;
- the focused IWX PNVM, init-epoch, AUTH/ASSOC TX, session-protection,
  driver-resident SAE, AP TX watermark and A-MPDU contracts.

The default, opt-out and IWN software-PMF Tahoe variants all built
successfully against the 25C56 BootKC.  Each resolved all 1074 undefined
symbols and imported no `_thread_call_cancel_wait`.  The exact default
candidate installed through the five-member AuxKC had:

- Mach-O UUID `12393D25-5490-38F1-A9F5-B9FF87F798C3`;
- binary SHA-256
  `cf1492c3ab375a6e9d2af12e7d0e3572388ecd96782fbf54338333187103c77f`.

The disposable AX211 Tahoe guest loaded that UUID after the guest-only
reboot.  No boot panic or firmware fatal occurred.

With the saved WPA2 Personal profile `AIAM-IWX-WPA2-0805`, an ordinary
System Settings Wi-Fi off/on cycle returned to an authorized on-air station,
the existing `192.168.87.226/24` DHCP lease and green `Connected` UI state in
18 seconds.  The UI remained connected beyond 60 seconds.  Source-bound
guest-to-host and host-to-guest ICMP each passed 20/20, and an HTTP transfer
matched the host file's SHA-256.  The passive RegDiag trace remained in
`IEEE80211_S_RUN`, published the accepted parent link terminal, and contained
zero `join-abort` records.

The same profile then crossed real S3.  Serial recorded, in order:

```text
ACPI SLEEP
acpi_sleep_kernel hib=0, cpu=0
ACPI S3 WAKE
PMRD: System Wake
```

After wake, System Settings again showed `Connected`; direct SSH over the
physical Wi-Fi address succeeded; the DHCP address, WPA2 BSSID and channel 6
were current; HTTP content still matched; and ICMP again passed 20/20 in both
directions.  The post-wake RegDiag snapshot was in RUN and contained zero
`join-abort` records.  QEMU's separate virtio user-NAT SSH forward did not
recover after S3, so it was excluded from the Wi-Fi result rather than being
mistaken for a driver failure.
