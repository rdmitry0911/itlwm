# CR-479 CoreWiFi current scan/profile request capabilities

Date: 2026-07-09

## Scope

Public CoreWLAN and `networksetup` remained nil/not-associated even after the
direct Apple80211 current-link carriers returned valid SSID, BSSID, state, and
`CURRENT_NETWORK` data. The failure was narrowed to CoreWiFi request admission:
`CWFInterface::currentScanResult` checks `CWFXPCClient::allowRequestType(0x39)`
before it sends the XPC request, and the local CARD_CAPABILITIES bitmap did not
advertise request types `0x39` or `0x3a`.

## Reference Evidence

Primary Apple producer:

- `AppleBCMWLANCore::getCARD_CAPABILITIES(...) @ 0xffffff80015e4c66`

Relevant recovered writes:

- `0xffffff80015e4ee6: OR byte ptr [RBX + 0x0b], 0x02`
  after `featureFlagIsBitSet(0x2e)` succeeds. With the four-byte `version`
  prefix, `[RBX + 0x0b]` is `capabilities[7]`; bit `0x02` advertises CoreWiFi
  request type `0x39` (`currentScanResult`).
- `0xffffff80015e4f5f: OR byte ptr [RBX + 0x0b], 0x04`
  after `featureFlagIsBitSet(0x30)` succeeds and the hardware feature gate does
  not block it. This advertises request type `0x3a`
  (`currentNetworkProfile`).

CoreWiFi evidence:

- `-[CWFInterface currentScanResult] @ 0x7ff81f09d620` calls
  `allowRequestType:0x39` before sending
  `queryCurrentScanResultWithRequestParams:reply:`.
- `-[CWFXPCRequestProxy __currentScanResultWithInterfaceName:forceNoCache:reply:]`
  builds a `CWFXPCRequest` with type `0x39`.
- `-[CWFXPCRequestProxy __currentNetworkProfileWithInterfaceName:]` builds a
  `CWFXPCRequest` with type `0x3a` and then uses the resulting profile for the
  service-type-4 SSID/BSSID checks.

Runtime evidence before the local fix:

- `_capabilities` exposed request types through `73`, but not `57` or `58`.
- `CWFXPCClient::allowRequestType(57) == 0` and
  `allowRequestType(58) == 0`.
- `CWFInterface::currentScanResult` returned `nil` while low-level
  `CWFApple80211 currentNetwork:` and raw Apple80211 `CURRENT_NETWORK` returned
  the associated BSS.

## Local Closure

`TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()` now
sets:

- `capabilities[7] bit 1` for CoreWiFi request type `0x39`;
- `capabilities[7] bit 2` for CoreWiFi request type `0x3a`.

Both Tahoe and legacy CARD_CAPABILITIES producers use the shared helper, so the
request bitmap no longer drifts across Apple-visible paths. The change does not
enable the unrelated `capabilities[10] = 0x08` LQM-create gate that previously
triggered the unsafe QueueCall path.

## Runtime Classification

The rebuilt Tahoe kext loaded with UUID
`1F012C39-0D8D-3254-9C76-3CA7037033D1` and binary SHA-256
`05cdf28f080ccdbae94bfaa52532a3df4a208e4ed020802cd344fda403a9af69`.
After controlled join to `ITLWM-Lab-3c95c7`, low-level current-link probes
reported associated state `4`, DHCP `10.77.0.157`, BSSID
`80:e4:ba:20:ef:f9`, and `Apple80211CopyValue(12)` returned a capability list
including request types `57` and `58`.

That closes the Apple80211 CARD_CAPABILITIES advertisement mismatch only.
Live CoreWiFi admission still returned
`CWFXPCClient::allowRequestType(57) == 0` and
`CWFXPCClient::allowRequestType(58) == 0`, with
`CWFInterface.currentScanResult == nil` and
`currentKnownNetworkProfile == nil`. Therefore public CoreWLAN/
`networksetup` remains an open driver-facing surface; the next layer must
explain why CoreWiFi's live allow/profile model does not consume the now
advertised request bits.
