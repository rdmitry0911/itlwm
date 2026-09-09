# Tahoe IWN multi-BSS reconnect runtime — 2026-09-09

## Scope

This is a guest-only, on-air runtime result for the default IWN Tahoe build
from source commit `6eb51401`.  The loaded `AirportItlwm` image had UUID
`9B5CED54-D384-3460-B90B-B1B5C4C7C8B9`.

The test used one pre-existing saved WPA3/SAE profile.  No network identifier,
BSSID, credential, physical-host address, or route is recorded here.

## Controlled two-BSS procedure

1. The lab AX211 remained associated with the ordinary lab BSS and exposed a
   separate temporary virtual AP on a legal 5 GHz channel.  The temporary AP
   advertised the same saved WPA3/SAE identity, required PMF, and used an
   isolated DHCP subnet.  The physical management Ethernet was not changed.
2. The Tahoe guest performed a normal scan.  A public saved-network selection
   (with no password supplied to the command) associated it to the temporary
   BSS.  The host AP observed a completed WPA3 four-way handshake.
3. The guest received a DHCP lease from that AP and completed 8/8 ICMP
   exchanges with the AP-side host.
4. The test stopped only the temporary `hostapd` and `dnsmasq` processes by
   their recorded PIDs.  It did not touch the ordinary lab AP or either
   physical host's management connection.
5. The guest independently re-associated to the original BSS through its
   saved profile in 10 seconds, without a radio toggle or explicit join.
   It then completed 8/8 ICMP exchanges on the restored path.

The temporary AP interface was removed after the check; both temporary
services were confirmed stopped, and the AX211 remained associated with its
original lab BSS.

## Adjacent saved-profile recovery check

On the same loaded image, a public `networksetup` radio OFF/ON sequence caused
an observed reachability loss at two seconds and restored saved-profile
connectivity at nine seconds.  The recovered guest then passed 5/5 ICMP
exchanges and reported radio power `On`.

## Claim boundary

This closes the tested user-facing BSS-loss/reconnect path: saved WPA3 profile
selection to another BSS, DHCP/data on that BSS, and automatic return after a
controlled BSS disappearance.

It does not prove seamless RSSI-triggered roaming, 802.11k neighbour-report
selection, 802.11v BSS-transition handling, or a handoff specifically induced
by an AP steering request.  Those remain separate roaming surfaces.
