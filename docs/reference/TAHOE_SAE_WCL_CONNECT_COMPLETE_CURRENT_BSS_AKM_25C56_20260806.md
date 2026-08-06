# Tahoe SAE WCL connect-complete from current-BSS AKM (25C56, 2026-08-06)

## User-visible gap

The AX211 driver-resident SAE path completed authentication, association and
the PMF four-way transaction on air. PTK, GTK and IGTK were accepted by the
firmware, Message 4 was transmitted, the selected node became port-valid and
the parent link-up was accepted. WCL nevertheless timed out and issued
`JOIN_ABORT`, so DHCP and data could not remain usable.

A behavior-neutral diagnostic build followed the real terminal through the
controller command gate. `handleKeyDone(true, false)` and the
`APPLE80211_M_RSN_HANDSHAKE_DONE` mail both succeeded. The only failed
completion predicate was:

```text
auth_upper=0x1000 rsnIeLength=0 current AKM=SAE
```

The direct SAE `CIPHER_PWD` path intentionally does not consume the optional
request-owned RSN IE. The old protected-join matcher was therefore using a
WPA2-oriented request-field proxy after the lower security transaction had
already proved a current SAE BSS.

## Tahoe 25C56 reference boundary

The reference was checked before changing the matcher. The combined exact
decompile is on `10.7.6.112` at:

`~/Projects/ghidra_output/cr357_wcl_join_lifecycle_boundary_repair_20260508_1005_decomp.txt`

Its SHA-256 is
`ddac02b370679db93cd2f5cbca84eb3ed31a44ad1c47a32ac59d71cbc0630c72`.
Relevant functions are:

- `AppleBCMWLANJoinAdapter::setAssocRSNIE` at `0xffffff80015795b8`;
- `AppleBCMWLANJoinAdapter::getBSSInfoAsync` at `0xffffff800157adec`;
- `AppleBCMWLANJoinAdapter::sendConnectComplete` at `0xffffff800157d472`;
- `AppleBCMWLANJoinAdapter::getBSSInfoAsyncCallback` at
  `0xffffff800157db28`.

`setAssocRSNIE` explicitly accepts length zero and sends a null/zero `wpaie`
value. The later current-BSS callback materializes the BSS, reads its current
AKMs, includes the SAE AKM families in its handling, and reaches
`sendConnectComplete`; it does not require the earlier association carrier to
have contained a nonzero RSN IE. This is consistent with the recovered
`0x1ba/0x6fc` `CIPHER_PWD` carrier documented in
`TAHOE_WCL_ASSOCIATE_CIPHER_PWD_25C56_20260722.md`.

## Local correction

The protected completion retains all existing candidate, epoch, BSSID, SSID,
authentication-success, association-completion, S_RUN, RSN-enabled and
one-shot fences. Its security-owner test now accepts either:

1. the existing nonzero request-owned RSN IE; or
2. an exact direct-SAE-capable WCL auth selector together with the current
   selected node narrowed to `IEEE80211_AKM_SAE`.

The second branch cannot admit an open network, a generic non-SAE zero-RSN
request, an alternate BSS, a stale epoch or a laboratory SAE stimulus without
a WCL completion owner. It does not manufacture RUN, port-valid, keys, link
state or a completion event; it only stops rejecting the real current-BSS SAE
terminal for the absence of an optional request field.

## Runtime verification

The first diagnostic candidate proved the previous rejection. With only the
matcher correction added, the same on-air path then published link and
connect-complete successfully for BSSID `82:c3:97:84:51:c9`.

All temporary `WCLDIAG` and `MFPDIAG` instrumentation was removed before the
clean candidate. That candidate had:

- Mach-O UUID `69A1ABDF-19FA-356B-A65C-E7FB2287E4EB`;
- binary SHA-256
  `ee3ac8f914a07593fb3c6981b8acee4fa54599e1fab2d655c0139899bb851838`.

On the physical passthrough AX211, a cold boot autonomously joined the pure
SAE/MFP `LabAP`. `wdutil` reported WPA3 Personal, channel 153/80 and DHCP
`172.16.66.213/24`. Source-bound ICMP to the router passed 20/20 and HTTP
returned 200 with 664 bytes. No late `JOIN_ABORT` was recorded.

The same loaded UUID then crossed real ACPI S3. The exact owned QEMU monitor
reported `paused (suspended)` after:

```text
ACPI SLEEP
acpi_sleep_kernel hib=0, cpu=0
```

Only that monitor received `system_wakeup`. Serial then recorded `ACPI S3
WAKE`, `PMRD: System Wake`, fresh candidate-matched SAE completions and active
ESS credentials without a join abort, panic or firmware fatal. Because the
unrelated virtio SSH forward did not recover, the host Intel 6235 joined the
same ESS with SAE group 19, PMF required and BIP, and reached the guest over
the physical Wi-Fi address. Post-wake host-to-guest and guest-to-host ICMP each
passed 20/20; SSH over Wi-Fi succeeded; guest-to-router HTTP again returned
200 with 664 bytes. Subsequent autonomous 2.4/5 GHz candidate changes also
reached fresh candidate-matched SAE terminals.

`networksetup -getairportnetwork en1` still reports its older public-status
failure even while `wdutil`, DHCP, ICMP, HTTP and direct Wi-Fi SSH prove the
WPA3 link. That CLI/CoreWLAN presentation mismatch remains a separate layer;
it is not evidence that the SAE/PMF data path failed.
