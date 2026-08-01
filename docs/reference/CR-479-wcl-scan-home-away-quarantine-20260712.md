# CR-479: WCL scan-home-away false-success quarantine

Date: 2026-07-12

## Reference contract

Tahoe 25C56 public bridge FUN_ffffff8001522d28 passes the
scanHomeAndAwayTime dword to scan adapter Core +0x1530. Adapter function
FUN_ffffff80016ac8a6 builds the scan_home_away_time firmware iovar and
submits it through the controller workqueue, returning that transport status.

The Core-owned route therefore performs an actual adapter operation. A copied
dword is not an equivalent user/kernel-space effect.

## Local divergence

AirportItlwm retained the caller dword in unconsumed
cachedScanHomeAwayTime and returned success, but it has no scan adapter, iovar
transport, queued callback, or firmware implementation for this selector.

## Local correction

The existing local NULL guard remains. Every non-null request now returns
kIOReturnUnsupported before pseudo-state mutation. This is a local
no-backend quarantine, not a claim that Apple rejects an available scan
adapter request.

## Deterministic guard

scripts/wcl_scan_home_away_quarantine_report.py --check requires the 25C56
bridge/adapter/iovar anchors, preserved local NULL guard, non-null unsupported
result, removal of the dead cache, and absence of a matching Intel backend.

## Runtime closure (2026-08-01)

The quarantine is superseded now that a real Intel consumer exists. A narrow
Tahoe 25C56 diagnostic build observed the live WCL request before association:
`milliseconds=110`, `ic_state=IEEE80211_S_SCAN`. The reference still forwards
that single dword to persistent `scan_home_away_time` firmware policy.

The local setter now validates the value, retains it through a narrow
system-policy bridge with a separate validity bit, and returns success only
because all three Intel backends consume it in their next associated scan
command. The bridge deliberately leaves the shared `ieee80211com` layout
unchanged:

- IWN/DVM programs `max_out` and packed `pause_scan` (microsecond/TBTT form).
- IWM programs both LMAC and UMAC `max_out_time`/`suspend_time` fields.
- IWX programs the legacy, v12, and v14 UMAC command layouts.

An explicit zero remains distinct from an unset policy: zero disables the two
home/away fields, while an unset policy preserves the prior backend defaults.
Values above 1000 ms are rejected because DVM's common packed representation
cannot encode them for every legal beacon interval. The old interface-local
cache remains absent; it would still be a false-success implementation.

The deterministic report keeps its historical filename so existing callers do
not break, but schema `itlwm-wcl-scan-home-away-intel-backend-v2` now requires
the live IWN/IWM/IWX consumers instead of the superseded quarantine.

The final IWM/6235 on-air validation loaded Mach-O UUID
`16BB5D9F-9E22-3148-9F74-79EAF29C7195`. A radio off/on cycle caused Tahoe to
issue `WCL_SET_SCAN_HOME_AWAY_TIME` while the driver was unavailable; the new
handler returned `GOOD`, and WPA3/SAE association, DHCP, and 5/5 gateway ICMP
recovered. A controlled `LabAP` BSS on channel 149 then authenticated with
required PMF (`authenticated=yes`, `associated=yes`, `authorized=yes`,
`MFP=yes`), acquired `192.168.149.99`, and passed 8/8 gateway ICMP. After only
that hostapd PID was stopped, Tahoe's unmodified auto-join policy scanned 37
channels, considered two candidate BSSes, selected OpenWrt BSSID
`82:c3:97:84:51:ca` on channel 9, completed WPA3/SAE and DHCP after its normal
retry-schedule backoff, and passed 10/10 gateway ICMP. The airportd success
metric reported `result=1`, `retrySchedule=ac`, `retryScheduleIndex=2`, and
`linkRecoveryDelay=141522` ms.
