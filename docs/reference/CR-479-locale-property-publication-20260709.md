# CR-479 locale property publication

## Reference Evidence

Tahoe 25C56 `IO80211InfraInterface::updateCountryCodeProperty(bool)` at
`0xffffff80022bc5a8` publishes `IO80211CountryCode` and then tail-calls
`IO80211InfraInterface::updateLocaleProperty()` at `0xffffff80022bcf1c`.

`updateLocaleProperty()` reads an `apple80211_locale_data` carrier and publishes
`IO80211Locale` with the recovered BootKC table:

- `1 -> FCC`
- `2 -> ETSI`
- `3 -> Japan`
- `4 -> Korea`
- `5 -> APAC`
- `6 -> RoW`
- `7 -> Indonesia`
- otherwise `Unknown`

The local `getLOCALE` producers in both the legacy STA and Tahoe Skywalk paths
already return `APPLE80211_LOCALE_FCC`.

## Local Delta

The local country-code repair intentionally published `IO80211CountryCode`
directly on `fNetIf` instead of entering the family helper. That left the paired
locale updater unwired: live IORegistry and `system_profiler SPAirPortDataType`
reported `IO80211Locale = "Unknown"` while `APPLE80211_IOC_LOCALE` still
returned `APPLE80211_LOCALE_FCC`.

## Local Closure

- Add the recovered locale enum-to-string table to the shared country-code
  helper.
- Whenever local code publishes `IO80211CountryCode` through the direct
  registry path, publish the paired `IO80211Locale` value from the same
  recovered table.
- The current local locale remains `APPLE80211_LOCALE_FCC`, so the published
  property is exactly `FCC`.

This does not widen `isCommandProhibited`, retry a request, infer locale from
SSID/country code, or mask any CoreWLAN result. It mirrors the reference
property-pairing that `updateCountryCodeProperty(bool)` performs after a
successful country-code refresh.

## Runtime Validation

Tahoe 25C56 loaded the rebuilt kext with UUID
`E736151C-C298-392D-AE5E-E0FA76C496AE`, binary SHA-256
`9e8f685a94f64f4cf97ca26da18d493a614fa11ebb10d5c9b84fbb49b1e8e9a2`,
and build source id `79886c005985`.

Before association, IORegistry already exposed `IO80211Locale = "FCC"` instead
of the previous `Unknown`. After the saved-profile join to `AIAMlab6235`,
the associated-state surfaces stayed aligned:

- IORegistry: `IO80211Locale = "FCC"`, `IO80211CountryCode = "US"`,
  `IO80211SSID`, `IO80211BSSID`, `IO80211Channel = 6`, and
  `IO80211RSNDone = Yes`.
- `system_profiler SPAirPortDataType`: `Locale: FCC`, `Country Code: US`,
  `Status: Connected`, channel `6`, rate `117`, MCS `14`.
- `en1` remained active at DHCP `10.77.0.47`.

The same loaded build passed the required concurrent 240-second data-path
stress against host `10.77.0.1`: ping returned `240/240`, `0.0%` packet loss,
RTT `0.808/544.595/1615.601/173.862 ms`, and `/usr/local/bin/iperf3`
transferred `772 MBytes` at `27.0 Mbits/sec` sender / `771 MBytes` at
`26.9 Mbits/sec` receiver. Host-side `iperf3` on port `5207` received
`771 MBytes` at `26.9 Mbits/sec`.

The stress-window severe fault filter over `kernel` and `airportd` found no
panic, firmware crash, NoCTL, missed beacon, stack corruption, or
`IO80211QueueCall` signatures.

`networksetup -getairportnetwork en1` still printed
`You are not associated with an AirPort network.` The same log window still
contains the known public CoreWLAN/airportd `0xe0822403` / `driver not
available` surface. This batch does not claim that layer closed.
