# CR-479 CARD_CAPABILITIES Tahoe Skywalk BSD route

Date: 2026-07-09

## Scope

The CARD_CAPABILITIES payload contract was already recovered and shared by the
Tahoe and legacy producers, but the Tahoe Skywalk BSD bridge still did not
route raw selector `12` to that producer. A direct Apple80211 IOCTL probe on
the live associated interface returned `errno=102` for
`APPLE80211_IOC_CARD_CAPABILITIES`, even though the getter existed.

## Reference Evidence

Primary Apple producer:

- `AppleBCMWLANCore::getCARD_CAPABILITIES(...) @ 0xffffff80015e4c66`

Recovered local contract before this route fix:

- `AirportItlwm::getCARD_CAPABILITIES(...)` fills the Apple-sized
  `apple80211_capability_data` carrier.
- `AirportSTAIOCTL.cpp::getCARD_CAPABILITIES(...)` uses the same shared
  capability cluster.
- `TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()`
  publishes the recovered bytes through `cap[9]`, including request bits for
  SSID/BSSID and current scan/profile readers.

The route mismatch was below the payload producer: Tahoe user-space enters the
Skywalk-only BSD bridge, where `TahoeSkywalkIoctlRoutes::shouldRoute(...)`
omitted selector `12`, and `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)`
had no `APPLE80211_IOC_CARD_CAPABILITIES` dispatch case.

## Local Closure

The Tahoe Skywalk route table now admits get-side selector `12`, and the BSD
bridge dispatches `APPLE80211_IOC_CARD_CAPABILITIES` to
`AirportItlwm::getCARD_CAPABILITIES(...)`. The route preserves the existing
producer bytes and rejects only nonzero buffers shorter than
`apple80211_capability_data`.

## Runtime Validation

Validated on the Tahoe guest after AuxKC install and reboot:

- loaded kext UUID `05E03C3E-4C0D-34C5-B105-E8839AD901D7`, binary SHA-256
  `e4ae370aa0de18532b8593d684321c7c709147f9cb0fcc386ba9e6fcb58b1caa`;
- raw `APPLE80211_IOC_CARD_CAPABILITIES` returned success with capability
  bytes
  `ef:e6:6f:27:00:40:0c:06:01:02:00:00:00:00:00:00:00:00:00:00:00:00:00:00`;
- the same raw probe returned associated state `4`, SSID
  `ITLWM-Lab-3c95c7`, BSSID `80:e4:ba:20:ef:f9`, channel `1`, and
  `CURRENT_NETWORK` with 16-byte SSID and IE length `168`;
- `en1` remained active with DHCP `10.77.0.157`, and
  `system_profiler SPAirPortDataType` reported `Status: Connected`,
  country `US`, channel `1`, rate `78`, and MCS `12`;
- IORegistry published `IO80211SSID = "ITLWM-Lab-3c95c7"`,
  `IO80211BSSID = <80e4ba20eff9>`, `CoreWiFiDriverReadyKey = "true"`,
  `IO80211RSNDone = Yes`, and `IO80211CountryCode = "US"`;
- the accepted 240-second concurrent stress passed with `PING_RC=0` and
  `IPERF_RC=0`: ping reported `240 packets transmitted, 240 packets received,
  0.0% packet loss`, RTT `1.393/862.723/2040.640/366.388 ms`, and iperf3
  transferred `590 MBytes` sent and received at `20.6 Mbits/sec`;
- a narrow post-stress log filter for panic, stack corruption, missed beacon,
  NoCTL, IO80211QueueCall, firmware crash, deauth, and disassoc signatures was
  empty.

Non-claims:

- this does not change CARD_CAPABILITIES content;
- this does not close public CoreWLAN/`networksetup`;
- this does not classify the remaining public current-network symptom as
  non-driver or TCC-only. The same runtime still had
  `networksetup -getairportnetwork en1` print
  `You are not associated with an AirPort network.`, while
  `CWInterface.ssid`, `CWInterface.bssid`, `CWFInterface.SSID`,
  `CWFInterface.BSSID`, `currentNetwork`, and `currentScanResult` returned
  `nil`.
