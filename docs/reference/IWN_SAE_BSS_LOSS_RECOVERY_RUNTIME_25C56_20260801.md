# IWN SAE BSS-loss recovery runtime evidence (25C56, 2026-08-01)

## Scope

This observation exercises a real user-visible failure and recovery cycle.  It
does not call `disassociate`, reset the adapter, reboot the guest, or fabricate
a driver completion.  A controlled pure-SAE/MFP-required `LabAP` BSS is first
selected on the host AX211 and is then withdrawn while two independently
operated same-ESS BSSes remain on air.

The tested AirportItlwm binary has Mach-O UUID
`A740A626-6DD2-3DD2-9091-FD0DDD526CA9` and SHA-256
`721f78130babf89cc5d2b4e0600bb090f07d38fea8a9e5670369e242ef4ce1b8`.
The guest runs Tahoe build `25C56`.

## Reference mechanism

The recovered IO80211 reference path does not turn a missed beacon into an
invented successful reassociation.  `WCLNetManager::handleMissedBeacons`
routes the loss through the link-state owner; the higher CoreWiFi owner is then
free to start a new known-network join.  The relevant recovered reference
artifact is:

`/home/dima/Projects/ghidra_output/cr298_handle_missed_beacons_disasm.txt`

The runtime observation below follows that split: net80211 first reports the
lost BSS and moves the channel to zero; airportd then performs a fresh saved
network association.  It is not classified as a `WCL_REASSOC` roam.

## On-air result

1. The guest started on an external pure-SAE `LabAP` BSS on channel 9.
2. A directed public CoreWLAN scan selected the one controlled `LabAP` record
   on channel 153.  The eventual asynchronous known-network join completed
   even though the command-line CoreWLAN caller had already received error
   `-3900`; that early caller result is therefore not used as link evidence.
3. The host observed station `92:48:10:dd:32:38` with
   `authenticated=yes`, `associated=yes`, `authorized=yes`, and `MFP=yes`.
   The guest acquired `10.77.0.118` and passed 5/5 source-bound ICMP packets
   to `10.77.0.1`.
4. Only the controlled channel-153 BSS was withdrawn.  The serial trace then
   recorded, in order:

   - `channel changed from<153:14> to<0:0>`;
   - `receiving no beacons from 80:e4:ba:20:ef:f9; leaving the lost BSS`;
   - successful WCL auth/association completion for the replacement join;
   - `iwn_sae_roam ACTIVE_ESS_CREDENTIAL`;
   - `channel changed from<0:0> to<13:a>`.

5. The replacement association became usable after approximately eight
   seconds.  The guest reacquired `172.16.66.120`, passed 5/5 source-bound
   ICMP packets to `172.16.66.1`, and received HTTP 200 through `en1`.
6. No guest reboot, adapter reset, kernel panic, or management-path loss was
   required.  The local host AP was restored to `AIAMlab6235`, and the
   temporary guarded state was retired.

## Result

The IWN pure-SAE saved-network path now has direct runtime evidence for the
complete controlled BSS-loss cycle: working link, real beacon loss, automatic
same-ESS reconnect, DHCP recovery, and post-recovery data traffic.  A future
test harness must treat CoreWLAN association completion as asynchronous and
must bind success to the observed interface/BSS and traffic state rather than
to the early `associateToNetwork` return alone.
