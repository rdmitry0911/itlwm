# CR-479 current-BSS country-code publication

## Reference evidence

- AppleBCMWLANCore initializes a real current-country state during default
  country-code setup, with a normal alpha2 value distinct from the local
  fallback state.
- AppleBCMWLANCore's country-code changed event path copies the firmware/event
  alpha2 into that current-country state and then posts the country-code update.
- Tahoe airportd has scan-cache country-code update paths and CoreWLAN has a
  `CWLocationClient countryCodeDidChangeForWiFiInterfaceWithName:` consumer.
  That makes the associated BSS 802.11d country IE a first-class current-link
  source when firmware only exposes a local fallback.

## Local delta

The IWN firmware info path returns `ZZ`. Before this batch,
`APPLE80211_IOC_COUNTRY_CODE` and the `IO80211CountryCode` registry property
therefore exposed `ZZ` even when the joined BSS scan record already carried a
valid 802.11d country IE. Runtime showed the split directly:

- `wdutil info` could be driven to show the selected country through the ioctl
  path once the getter was fixed;
- `system_profiler SPAirPortDataType` and IORegistry still reported
  `IO80211CountryCode = "ZZ"` until the interface-side registry property was
  synchronized at the current-link publication boundary.

## Local closure

- `getCOUNTRY_CODE` now resolves the country code as:
  `itlwm_cc` boot override, non-fallback firmware alpha2, associated-BSS
  802.11d alpha2, geolocation alpha2, firmware fallback, then `ZZ`.
- The associated-BSS 802.11d parser walks the already preserved tagged IE tail
  in `ic_bss->ni_rsnie_tlv` and accepts only uppercase non-placeholder alpha2
  values.
- The resolved value is published consistently through the Apple80211 ioctl
  carrier and the interface-side `IO80211CountryCode` OSString property.
- Tahoe WCL connect-complete and explicit country-code update publication both
  refresh the registry property before user-space consumers observe the
  current network.

## Runtime validation

Tahoe runtime build `856B1DD8-977D-3BFF-B1D1-98826A10C030`
(`AirportItlwm` binary SHA-256
`ada010d6d694c79f059a376496d2e0a7eb981d46c7e5616542974fe6fcc075d2`)
loaded after AuxKC install and reboot.

After a controlled join to `ITLWM-Lab-3c95c7`, en1 reached DHCP
`10.77.0.157` and all country-code surfaces agreed:

- `wdutil info`: `Country Code         : US`;
- `system_profiler SPAirPortDataType`: `Country Code: US`;
- IORegistry: `IO80211CountryCode = "US"`, with real `IO80211SSID`,
  `IO80211BSSID`, `CoreWiFiDriverReadyKey = "true"`, and
  `IO80211RSNDone = Yes`.

The same loaded build passed the required 240-second concurrent data-path
stress: `240/240` ping replies, `0%` packet loss, RTT
`1.636/711.920/1453.550/255.094 ms`, and guest-to-host `iperf3` transferred
`604 MBytes` sent at `21.1 Mbits/sec` with `603 MBytes` received at
`21.0 Mbits/sec`. Host-side `iperf3` reported `603 MBytes` received at
`21.0 Mbits/sec`. Post-stress en1 remained active, DHCP stayed at
`10.77.0.157`, `wdutil`, `system_profiler`, and IORegistry still reported
country code `US`, and the log filter found no panic, stack-corruption,
CoreCapture, missed-beacon, deauth, disassoc, NoCTL, or IO80211QueueCall hits.

`networksetup -getairportnetwork en1` still printed
`You are not associated with an AirPort network.` That remains a public
CoreWLAN/Location authorization surface for that external client, not evidence
that this country-code driver surface failed.

## Non-claims

- This does not bypass LocationServices, TCC, CoreWLAN, or `networksetup`.
- This does not claim public external-client SSID/BSSID exposure complete.
- This does not change firmware regulatory programming; it publishes the
  current associated BSS country code through the Apple-compatible user-space
  surfaces.
