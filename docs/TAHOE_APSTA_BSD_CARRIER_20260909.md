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

## Loaded-image qualification and remaining lifecycle failure

Source `6433e0d3` built successfully with all 1083 BootKC symbols resolved.
Private AuxKC admission and transactional activation passed. The disposable
IWN/6235 guest loaded UUID `29FD701D-9A8E-3D53-997F-3E71612750BF`, matching
frozen Mach-O SHA-256
`f50f0cd9afb2a1c82b15b8fe6c28f2c6fc52e7949b2d6fed8031b6f93e29e8fa`.

Standard Internet Sharing, through the real system producer and bootpd,
successively admitted an external WPA3-SAE/required-PMF client, a WPA2-PSK
client and an open client. Each obtained DHCP. Separate bridge-scoped cold
ARP runs at 19:13:33, 19:14:50 and 19:15:44 UTC passed 10/10 source-bound
1400-byte packets with client power save enabled. Captures show the bridge's
broadcast ARP request reaching the client and its reply. The matching lower
observer records successful queue-8 completions and no diagnostic errors.
Independent client-to-gateway runs each passed 20/20. Both the AP member and
bridge reported active media. There was no reboot between security modes.

A normal sleep request while Internet Sharing was active was denied by the
system sharing preference plugin; that is not an AP sleep qualification.
After sharing was disabled, the pending request entered S3 before the
intended concurrent roles were established. That first wake is not counted
as an APSTA recovery result either. A public primary-STA selection initially
reported network-not-found, then a retry restored the link without off/on;
the public reconnect surface remains open.

The valid subsequent role-7 WPA3 APSTA setup passed an isolated cold AP
10/10 and simultaneous primary-to-gateway 5/5. Its 19:21:30 sleep request
reached serial `ACPI SLEEP`; the owned QEMU monitor independently confirmed
`paused (suspended)`. Wake at 19:28 UTC produced `ACPI S3 WAKE`, with unchanged
boot epoch and kext UUID. Explicit external-client reselection completed SAE
group 19/required PMF/BIP. The restored AP followed the recovered primary's
shared channel. At 19:29:10, another isolated cold run passed 10/10, with the
ARP request/reply in external capture and successful queue-8 completions.
Client-to-AP then passed 20/20, primary traffic 5/5, and normal AP stop retained
primary traffic at 10/10. This role-7 check uses static addressing and proves
service recovery, not automatic client continuity.

On that same boot, standard WPA3 sharing subsequently recreated the bridge,
supplied DHCP and passed a bridge-scoped cold 10/10 at 19:31:36 followed by
client-to-gateway 20/20. The cold script was detached before ARP removal, so
no management SSH traffic crossed the test AP during the measured window.

The next normal WPA3-to-WPA2 sharing change completed the external client's
four-way handshake at 19:32:34 but did not supply DHCP. This is not a passed
post-S3 security-mode matrix. The carrier fix closes the reproduced
media-inactive broadcast omission, not the separate stop/start failure.
The candidate release remains held while that failure is investigated in
`TAHOE_INTERNET_SHARING_BRIDGE_RECYCLE_20260909.md`. Recent hardware coverage
here is IWN; the shared carrier change is not equivalent IWM/IWX qualification.
