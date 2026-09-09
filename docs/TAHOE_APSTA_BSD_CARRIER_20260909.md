# APSTA carrier after BSD protocol re-enable — 2026-09-09

## Reproduced boundary

On loaded IWN source `ad910bab`, standard Internet Sharing admitted an
external SAE group-19/required-PMF client and supplied a DHCP lease. A fresh
bridge neighbor lookup nevertheless failed 0/10. Capture showed five outgoing
bridge ARP requests, none at the external client, and no lower AP-data send.
The live AP member reported media inactive. The successful static-interface
DTIM checks do not qualify this upstream bridge path.

The 19:05:10 UTC bounded stack observer identified ordinary protocol removal
and reattach between lower AP start and bridge membership. Base BSD disable
set VirtualInterface link down and Skywalk carrier to 1. APSTA re-enable
restored VirtualInterface link up but left Skywalk carrier down. A bridge's
learned-unicast output does not use the same member-selection path as
broadcast, explaining why a DHCP client could appear functional initially.

## Reference and correction

Apple's [bridge implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/net/if_bridge.c)
excludes members without `BIFF_MEDIA_ACTIVE` from broadcast delivery. The
matching 25C56 binary was also inspected read-only with a 40-CPU headless
configuration:

- The raw APSTA HostAP success tail at `0xffffff800166c08b` calls
  `reportLinkStatus(3, 0x80)` before `enableAPInterface`.
- `IOSkywalkNetworkInterface::reportLinkStatus`, exact range
  `0xffffff8002a12378..0xffffff8002a12424`, writes the provider carrier and
  emits the corresponding event. It is not an IORegistry-property workaround.
- `IOSkywalkNetworkInterface::enable` calls its virtual interface-enable
  method; `IO80211VirtualInterface::setLinkState` publishes association state.
  The observed re-enable does not restore the separately withdrawn carrier.
- The raw APSTA enable range `0xffffff8001673760..0xffffff8001673860` checks
  association admission, calls its inherited enable and then concrete
  datapath enable. Its initial decompile had no function and is not evidence.
  The reportLinkStatus C output was truncated at a bad no-return lock
  annotation; the complete raw range above is the evidence for that method.

After a successful inherited APSTA enable, the local implementation now
reconciles carrier through the existing reference publication only if the
AP owner's lower-start state is still RUNNING. Pending, failed, absent and
stopped owners cannot publish link-up. The lower-start publication remains
in place for asynchronous IWM/IWX completion after interface enable.

This is an Intel lifecycle reconciliation using the reference carrier API,
not a claim that Apple's APSTA enable body contains the added reconciliation.
It does not bypass bridge admission, invent an ARP lease, alter key/packet
ownership, or treat a queued AP start as a successful radio start.

## Verification boundary

`scripts/test_tahoe_apsta_bsd_carrier.sh` compiles the actual production
enable/disable methods under ASan/UBSan. Cases cover repeated protocol
disable/enable with a running lower AP, exact base/confirmation/carrier/queue
ordering, pending/stopped owners, an inherited enable error, no controller
and failed association admission. The old `ad910bab` method fails the first
carrier restoration assertion. Existing APSTA interface lifecycle,
shared-channel admission and IWN multicast/queue-capacity tests pass.

Build, loaded-image cold-neighbor delivery, repeated standard DHCP lifecycle
and S3 qualification are pending. This change does not yet close the separate
retained-INUSE bridge creation failure documented in
`TAHOE_INTERNET_SHARING_BRIDGE_RECYCLE_20260909.md`.
