# IWN AP WPA3 sleep recovery runtime evidence (25C56, 2026-08-01)

## Scope

This observation exercises the user-visible IWN HostAP path before and after
real S3.  It distinguishes three different contracts:

- WPA3/SAE AP service and data traffic before sleep;
- what CoreWiFi asks the driver to do while Tahoe prepares for sleep;
- whether a fresh WPA3 AP can be configured and used after wake.

The tested AirportItlwm binary has Mach-O UUID
`A740A626-6DD2-3DD2-9091-FD0DDD526CA9` and SHA-256
`721f78130babf89cc5d2b4e0600bb090f07d38fea8a9e5670369e242ef4ce1b8`.
The guest runs Tahoe build `25C56`.

## Exact reference contract

The Tahoe BootKC/KDK reference was re-decompiled on `10.7.6.112` with 40
active analysis cores.  The exact artifacts are under:

`/home/dima/Projects/ghidra_output/aiam_apsta_reference_lifecycle_kdk_26_3_20260801/`

The recovered sequence is:

- `AppleBCMWLANCore::powerOffSystem()` calls the APSTA owner at core-private
  `+0x2c30`, vtable slot `+0x428`, before the superclass power-off slot
  `+0x438`;
- `AppleBCMWLANCore::powerOnSystem()` calls the same owner at vtable slot
  `+0x430`, before the superclass power-on slot `+0x440`;
- `AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff()` returns immediately
  when AP-up state `+0x26c` is clear;
- when AP is up with no associated stations, `hostAPPowerOff()` sets power-save
  state `(0, 0x0c)`, clears state byte `+0x0e`, calls
  `setHostApModeInternal(NULL)`, and emits core event `1`;
- with associated stations and no SoftAP concurrency, it selects power-save
  state `(3, 3)` and does not stop the AP in that branch;
- an explicit upper-layer `HOST_AP_MODE(NULL)` is nevertheless a normal AP
  stop request.  The reference does not preserve an AP configuration after
  that request.

Consequently, retaining or replaying an AP after CoreWiFi has explicitly sent
`NULL` would not match the reference.

## Fixture correction

The CoreWLAN AP owner originally used one bare `sleep(3)` call for its hold
interval.  IOPM signals userspace before S3, so that call could return early
and let the helper itself enter the stop path before the requested hold had
elapsed.  The helper now uses interrupt-resuming `nanosleep(2)` with a
sub-second remainder.  This both survives the IOPM interruption and avoids
extending the hold through integer-second rounding under frequent signals.

With the corrected owner still alive, both the public NetworkRelay sharing
flow and the direct CoreWLAN HostAP flow nevertheless produced
`AP stop requested` before the serial trace recorded `PMRD: System Sleep`.
The stop therefore came from Tahoe's CoreWiFi/NetworkRelay policy, not from
the fixture and not from an APSTA power watchdog.

## On-air results

Before sleep, the NetworkRelay WPA3 sharing path completed the entire service
cycle:

- the Linux AX211 associated with `key_mgmt=SAE`, `pmf=2`, and
  `mgmt_group_cipher=BIP`;
- DHCP assigned `192.168.2.3` with gateway `192.168.2.1`;
- host-to-guest and guest-to-host ICMP both passed 5/5;
- HTTP through the shared uplink returned status 200.

The direct CoreWLAN WPA3 HostAP path was also exercised independently with
guest `ap1` at `192.168.3.1` and host `sta0` at `192.168.3.2`.  SAE with
required PMF/BIP completed and bidirectional ICMP passed before S3.

After a real wake, the primary IWN STA recovered its external connection.  A
fresh direct WPA3 AP named `AIAMGuestWPA3PostWake` was then configured without
rebooting or resetting the adapter.  The AX211 observed and joined BSSID
`ce:f7:33:f4:97:4b` with:

```
key_mgmt=SAE
pmf=2
mgmt_group_cipher=BIP
wpa_state=COMPLETED
```

Host-to-guest ICMP passed 5/5.  Guest-to-host traffic passed 4/5, with the
first delivered packet taking 217 ms and the following delivered packets
taking approximately 1.2--1.3 ms.  This is positive bidirectional datapath
evidence after wake, while the one startup loss remains visible rather than
being reported as a perfect result.

The post-wake AP was explicitly stopped, the guest returned to its primary
STA connection at `172.16.66.120`, and the host AX211 baseline AP was restored
as `AIAMlab6235` on channel 153/VHT80.

## Result

IWN now has direct runtime evidence for WPA3/SAE HostAP operation and traffic
both before S3 and after a wake-time reconfiguration.  This does **not** claim
that Tahoe preserves an active public NetworkRelay/CoreWLAN AP across sleep:
the current upper layer removes it before the driver power-off callback, and
the reference treats that explicit removal as authoritative.  No speculative
driver retention change is justified by this trace.
