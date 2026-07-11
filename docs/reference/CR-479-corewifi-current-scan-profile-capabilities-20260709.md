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
`currentKnownNetworkProfile == nil`. A later item-220 follow-up classifies the
matching top-level Dynamic Store SSID/BSSID redaction as reference airportd
pruning through `wifi_allow_sensitive_info`; any future driver patch for the
public CoreWLAN allow/profile model still requires a separate driver-facing
mismatch beyond the redacted public value.

## 2026-07-11 CoreWLAN service-policy proof

The current Tahoe 25C56 guest was checked again with the PM-corrected kext
UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90`. Its `CWFInterface` owns a
`CWFXPCClient` with `_serviceType == 5`, the CoreWLAN service type. The live
driver-sourced `CWFInterface.capabilities` array includes both `57` and `58`,
but the CoreWLAN client's `allowRequestType:` returns zero for both while it
continues to admit ordinary types `7`, `9`, `22`, and `73`.

This is the expected framework policy boundary, not another
CARD_CAPABILITIES route or payload mismatch. In the exact 25C56 CoreWiFi
image, `CWFSupportedRequestTypesForServiceType` at `0x7ff81edd3980` maps
service type `5` to `CWFSupportedRequestTypesForCoreWLANServiceType` at
`0x7ff81edd6920`. That function returns the immutable object at
`0x7ff842f30648`; the live object is a 74-element `NSConstantArray` that
excludes `57` and `58`. `CWFXPCClient::allowRequestType:` at
`0x7ff81f01d533` tests that static object before it registers protocol classes
or sends any request.

Therefore `CWFInterface.currentScanResult` and
`currentNetworkProfile` are intentionally stopped above airportd and above
Apple80211 despite the driver correctly advertising their capability bits. On
the same live client, `SSID`, `BSSID`, and `currentScanResult` all return nil;
the first two request types remain admitted by the static set and stay subject
to the separate airportd/location-authorization privacy path. No driver-side
fallback, capability inflation, request-gate expansion, Dynamic Store update,
or synthetic current-network event is justified by this evidence.

The read-only live probe is
`/home/dima/Projects/aiam/scratch/corewifi_admission_probe.m`; the recovered
CoreWiFi functions are retained at
`10.7.6.112:~/Projects/ghidra_output/aiam_corewifi_request_sets_20260710.c`.
