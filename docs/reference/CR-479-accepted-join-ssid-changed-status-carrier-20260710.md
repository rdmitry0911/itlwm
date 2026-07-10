# CR-479 accepted join SSID_CHANGED status carrier

## Reference evidence

Tahoe `AppleBCMWLANCore::handleSetSSIDEvent(wl_event_msg_t *)` builds an
8-byte `APPLE80211_M_SSID_CHANGED` payload before it calls into
`AppleBCMWLANJoinAdapter::handleSetSSID(...)`.

The recovered producer reads firmware event dwords at `wl_event_msg_t +0x08`
and `+0x0c`, maps nonzero values into Apple error domains, stores the results
as two dwords, and posts event `0x02` with length `0x08`. When both firmware
fields are zero, both output dwords remain zero.

## Local closure

The local Tahoe accepted-join path previously published
`APPLE80211_M_SSID_CHANGED` as a zero-length event. That was not the Apple
carrier shape and left the user-visible CoreWiFi associated-network path
blocked behind an empty interface `ssidData` model even while raw Apple80211
`SSID`, `BSSID`, and `CURRENT_NETWORK` returned the associated BSS.

The accepted join-up publisher now sends the Apple-shaped 8-byte status carrier
with `{status = 0, reason = 0}` for the successful local SET_SSID edge. It does
not copy SSID bytes into the event, does not synthesize Dynamic Store values,
and does not broaden any request gate.

## Runtime validation

Tahoe 25C56 loaded the rebuilt kext with UUID
`5F603693-8E18-322B-8169-31879650C6C9`, signed CDHash
`0599bebd76c46b176d598c1ce2ce7d243994225b`, and signed binary SHA-256
`ee19ec36bd0e4beb05e6a296919ea7e508582267e401571e57bb2c4bb14680e3`.

The lab join completed on `en1` with DHCP `10.77.0.47`. Raw Tahoe and legacy
Apple80211 probes returned SSID `AIAMlab6235`, BSSID `80:e4:ba:20:ef:f9`,
state `4`, and a populated `CURRENT_NETWORK` record on channel `6`.

The 240-second concurrent ping plus iperf3 stress run passed with
`PING_RC=0`, `IPERF_RC=0`, ping `240/240` with `0.0% packet loss`, RTT
`2.234/489.441/718.374/81.824 ms`, and iperf3 `813 MBytes` at
`28.4 Mbits/sec`. The stress-window fault filter found no panic, CoreCapture,
NoCTL, missed beacon, deauth, disassoc, `driver not available`, `0xe0822403`,
or `IO80211QueueCall` signature.

The remaining public surface did not change: `networksetup -getairportnetwork
en1` still printed `You are not associated with an AirPort network.`,
`CWInterface.ssid` and `CWInterface.bssid` remained `nil`, and Dynamic Store
still published top-level `SSID = 0x00` and `BSSID = 02:00:00:00:00:00` while
its `CachedScanRecord` contained the real BSS. Runtime logs showed airportd's
own `GET SSID` requests returning `err=0`, while external clients such as
`networksetup` and the probes still received `err=1`.

## Non-claims

- This does not change raw `APPLE80211_IOC_SSID`,
  `APPLE80211_IOC_BSSID`, or `APPLE80211_IOC_CURRENT_NETWORK` producers.
- This does not classify the public `networksetup` symptom as closed.
- This does not reintroduce the rejected broad fallback gate.
