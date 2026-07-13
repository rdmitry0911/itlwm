# CR-479: IBSS mode false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public IBSS_MODE setter boundary. It does
not enable IBSS, AP, GO, proximity, NAN, BssManager positive state, WCL, or
radio transitions.

## Recovered reference contract

The Tahoe 25C56 reference image is
/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
with SHA-256
4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab.

The Infra wrapper setIBSS_MODE is at 0x10001814c and dispatches to
AppleBCMWLANCore::setIBSS_MODE at 0x10011c94c. The recovered non-null path
tears down existing Proximity, NAN, and NAN-data links as needed, calls
AppleBCMWLANJoinAdapter::createAdhocNetwork at 0x10003d7ea, and preserves
the resulting lifecycle status while bringing the affected interfaces up.
This is real ad-hoc/proximity/NAN work, not an acknowledgement-only state
write.

The existing BssManager ad-hoc lifecycle reference independently establishes
that positive ad-hoc-created edges follow successful createAdhocNetwork work.
It does not establish the complete public-carrier allocation size or an Apple
null-input return-code contract.

## Local divergence

The public APPLE80211_IOC_IBSS_MODE selector 24 reaches
AirportItlwmSkywalkInterface::setIBSS_MODE through the SIOCSA80211 dispatcher.
Before this correction, every non-null carrier was copied into seven
cachedIbss fields and success was returned. Those fields occur only in their
declarations, two initialization/reset blocks, and the setter; there is no
local creator, IBSS owner, or transport.

The local backend capability census records IEEE80211_STA_ONLY. IBSS creation
is deliberately gated out of the net80211 and Intel HAL path, so replaying
Apple success cannot create the requested network mode.

## Local correction

The pre-existing local null rejection remains unchanged. Every non-null
request now returns kIOReturnUnsupported before mutation, and the seven dead
cachedIbss fields plus their initialization/reset sites are removed.

This is a no-backend quarantine, not Apple valid-input return-code parity.
Apple accepts a carrier only while performing its actual ad-hoc lifecycle.

## Verification boundary

The deterministic report requires the reference wrapper/Core/createAdhoc
anchors, the retained local null guard, a non-null fail-closed return, removed
pseudo-state, and documented STA-only backend absence. Build/load and normal
association/traffic gates establish regression safety. No IBSS carrier, private
setter ioctl, or guessed ad-hoc lifecycle is invoked. Radio OFF/ON remains
excluded because the A2DF baseline independently reproduces its WCL lifecycle
panic.
