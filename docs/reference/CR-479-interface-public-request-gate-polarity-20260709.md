# CR-479 Tahoe interface request-gate public fallback correction

Date: 2026-07-09

## Scope

This correction narrows `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
back to the only decompile- and runtime-proven slot `[411]` owners:

- hidden association carrier `0x45`;
- hidden association carrier `0x46`.

Public current-link selectors `1`, `4`, `9`, `0x67`, and `0xd8` must not be
admitted through this gate. Their payloads are already produced by the recovered
Tahoe Skywalk BSD Apple80211 dispatcher.

## Evidence

The 25C56 vtable still maps interface slot `+0xcc8` to
`IO80211SkywalkInterface::isCommandProhibited(int)`, and IO80211Family fallback
helpers call that slot with both hidden and public request numbers. The mistake
was treating the public-helper branch result as if it were a successful
Apple80211 payload-producing route.

Older CR-068 runtime evidence had already rejected that interpretation:

- public request-number admission leaked raw `1` for `CHANNEL` and
  `ROAM_PROFILE`;
- `SSID`, `BSSID`, and `CURRENT_NETWORK` still did not become valid through
  that route;
- the correct current-link owner was the explicit link-edge/current-BSS state
  publication plus the BSD Apple80211 dispatcher.

The same regression reproduced on the 2026-07-09 Tahoe runtime after commit
`9cc31e4`: public CoreWLAN `GET CHANNEL` reached Apple80211, but
`Apple80211IOCTLGetWrapper` returned `1/0x00000001` for
`APPLE80211_IOC_CHANNEL`, and `CWFInterface.channel` remained nil. The local
`getCHANNEL(...)` producer itself returns `kIOReturnSuccess`, so the raw `1`
was leaked by the gate/fallback route before the payload producer owned the
request.

## Local Closure

`AirportItlwmSkywalkInterface::isCommandProhibited(int)` now returns non-zero
only for hidden Tahoe association commands `0x45` and `0x46`. Public
current-link request numbers fall back to inherited family behavior and remain
owned by `processApple80211Ioctl(...)`.

## Runtime Validation

Validated on the Tahoe 25C56 guest after AuxKC install and reboot:

- loaded kext UUID `DF37D5FE-928C-361F-93A4-A0A394CC2806`, binary SHA-256
  `a2c3c2a4e284c42e5598e52f3b51c10445ed334d81508d6078b576c3ae18c0d9`;
- controlled public CoreWLAN probe returned `CWInterface.channel == 1`,
  active `CWChannel` channel `1`/20 MHz, and `CWFInterface.channel == 2g1/20`;
- the same controlled log window showed `GET CHANNEL err=0` and no
  `APPLE80211_IOC_CHANNEL ... return 1/0x00000001` hit;
- raw BSD Apple80211 probe on associated `en1` returned state `4`, SSID
  `ITLWM-Lab-3c95c7`, BSSID `80:e4:ba:20:ef:f9`, channel `1` flags `0x8a`,
  the recovered CARD_CAPABILITIES bytes, and `CURRENT_NETWORK` with 16-byte
  SSID and IE length `168`;
- concurrent 240-second stress passed with `PING_RC=0` and `IPERF_RC=0`:
  `240 packets transmitted, 240 packets received, 0.0% packet loss`, RTT
  `1.724/763.861/1633.649/289.603 ms`, and iperf3 transferred `567 MBytes`
  sent at `19.8 Mbits/sec` with `566 MBytes` received at `19.7 Mbits/sec`;
- post-stress `en1` remained active at DHCP `10.77.0.157`, IORegistry
  published real `IO80211SSID`, `IO80211BSSID`, `IO80211Channel`,
  `IO80211CountryCode`, `IO80211RSNDone`, and `CoreWiFiDriverReadyKey`, and
  `system_profiler SPAirPortDataType` reported connected infrastructure WPA2,
  country `US`, channel `1`, rate `104`, and MCS `13`;
- the stress-window fault filter found no panic, firmware crash, NoCTL, missed
  beacon, stack corruption, deauth, disassoc, `driver not available`,
  `0xe0822403`, or `IO80211QueueCall` signatures.

Non-claims:

- this does not bypass CoreWLAN, LocationServices, TCC, or `networksetup`;
- this does not mark public `CWInterface.ssid` / `bssid` complete;
- this does not remove the hidden association-carrier gate;
- `networksetup -getairportnetwork en1` still prints
  `You are not associated with an AirPort network.` on the same runtime.
