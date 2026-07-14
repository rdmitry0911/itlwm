# CR-479: Tahoe no-APSTA channel fallback false-success quarantine

Date: 2026-07-14

## Scope

This correction is limited to the Tahoe V2/Skywalk `setCHANNEL` fallback used
when `AirportItlwm::fAPSTAOwner == NULL`. It neither implements Apple's
channel-programming path nor modifies the separate owner branch
`fAPSTAOwner->setChannel(in)`.

## Recovered reference contract

The 25C56 KDK image is
`/Users/devops/kdk-cache/25C56/Kernel_Debug_Kit_26.2_25C56.dmg`, SHA-256
`db163f75110e7e79aafa580396285275df1a7a0105da82cf1a05912a5e24c401`.
The corresponding BootKernelExtensions.kc reference has SHA-256
`aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8`;
the symbol map has SHA-256
`1ee52696618d1ed8d14272c077121dccd1fb90c6f6ced6bc737abd2bf099b748`.

`AppleBCMWLANCore::setCHANNEL(apple80211_channel_data *)` is the 450-byte
Core body at `0xffffff8001602b3e`. It returns `0xe00002bc` for null, rejects
a channel id `>= 0x100` with raw `0x16`, copies the 16-byte carrier into Core
state, then calls the chanspec helper at `0xffffff8001602f74`. A helper error
or zero chanspec returns `0xe00002c2`; a valid chanspec proceeds to the real
four-byte `chanspec` firmware IOVAR path. Existing bounded reference records
are `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md`
and `docs/reference/AppleBCMWLAN_APSTA_channel_csa_sta_control_2026_04_27.md`.

The APSTA producer is a distinct owner with its own carrier and null contract;
it is evidence for the real `chanspec` transport, not a reason to alter the
no-APSTA fallback.

## Local divergence

The active public route is `IOC_CHANNEL/SIOCSA` to
`AirportItlwm::setAPSTA_CHANNEL`. With no APSTA owner it reaches the Skywalk
fallback; with an owner it directly calls `fAPSTAOwner->setChannel(in)`.

Before this correction, the fallback copied every valid request into
`cachedRequestedChannel`, marked it present, searched only the local channel
table, and returned success for a known channel. The two cache fields existed
only in their declaration, two initialization/reset sites, and those writes;
they had no reader or channel-programming consumer. There is no local
`chanspec` owner or transport in this fallback.

## Local correction

The fallback retains its existing internal null, range, zero/absent-controller,
and unknown-channel gates. A known local channel now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead cache
fields plus their initialization are removed.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts a valid request and performs real chanspec/firmware work.
The BSD bridge's own precheck is separately scoped, so this note does not claim
public null or short-carrier parity.

## Verification boundary

`scripts/channel_fallback_quarantine_report.py` verifies the reference
identities and anchors, retained internal gates, known-channel fail-closed
result, removed cache, public route, and untouched APSTA owner dispatch.
No private carrier is constructed or invoked at runtime. Build/load and normal
network gates remain regression checks only.
