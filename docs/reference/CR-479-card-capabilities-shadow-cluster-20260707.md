# CR-479 CARD_CAPABILITIES legacy shadow cluster

Date: 2026-07-07

## Scope

The Tahoe controller path already sanitized `getCARD_CAPABILITIES` after the
CR-031/CR-032 runtime work. The legacy dispatcher shadow still published the
old hard-coded cluster:

- `cap[2] = 0xef`
- `cap[3] = 0x2b`
- `cap[6] = 0x8c`

That reintroduced the same Apple-impossible advanced capability bits through a
second public surface.

## Reference evidence

Primary reference remains:

- `AppleBCMWLANCore::getCARD_CAPABILITIES(IO80211SkywalkInterface*, apple80211_capability_data*)`
  at `0xffffff80015e4c66`

The recovered producer never sets:

- `cap[2] bit 0x80`
- `cap[3] bit 0x08`
- `cap[6] bit 0x80`

Current symbol metadata on the decompile host:

- `10.7.6.112:~/Projects/ghidra_additional/kc_all_symbols.txt`
- `0xffffff80015e4c66 AppleBCMWLANCore::getCARD_CAPABILITIES(...)`
- `0xffffff80021e2c16 apple80211getCARD_CAPABILITIES(...)`
- `0xffffff80021fa139 getCARD_CAPABILITIES(...)`

## Local closure

`TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()` now
owns the deterministic cluster shared by both producers:

- `cap[0] = 0xef`
- `cap[1] = 0xe6`
- `cap[2] = 0x6f`
- `cap[3] = 0x27`
- `cap[5] = 0x40`
- `cap[6] = 0x0c`
- `cap[7] = 0x06` for CoreWiFi current-scan/current-profile request gates
- `cap[8..9] = 0x0201` in little-endian byte order

The `cap[0..1]` prefix is also fixed, not derived from net80211 `ic_caps`.
Tahoe CoreWiFi treats these bytes as a request-capability bitmap: for example,
`CWFInterface_SSID` returns `nil` before asking the driver unless request type
7 is present. AppleBCMWLANCore seeds the prefix with `0x6f, 0xe6`, then
`OR`s `cap[0]` with `0x80` when
`AppleBCMWLANCore::shouldSupportTethering()` is true. That function returns
`featureFlag(0x10)` when `featureFlag(0xb)` is clear. Publishing the resulting
`0xef, 0xe6` pair exposes current-link properties. CR-479 current-scan/profile
capability publication extends the same Apple producer evidence to
`cap[7] = 0x06` without advertising the separate local `cap[10] = 0x08`
LQM-create gate. Runtime CoreWiFi admission for request types `57` and `58`
remains a separate public-surface issue.

Both `AirportItlwmV2.cpp::getCARD_CAPABILITIES` and
`AirportSTAIOCTL.cpp::getCARD_CAPABILITIES` call the helper, so the Tahoe and
legacy dispatchers no longer drift on the same Apple-visible capability
contract.
