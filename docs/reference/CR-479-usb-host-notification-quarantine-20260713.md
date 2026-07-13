# CR-479: USB host notification false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public `USB_HOST_NOTIFICATION` setter
boundary. It does not emulate Apple's Broadcom USB/coexistence owner, alter
the generic Tahoe commander, or change MWS, BTCOEX, WCL, radio, power,
association, or another public selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra wrapper `setUSB_HOST_NOTIFICATION` at `0x100018890` follows the
Core pointer and tail-dispatches its virtual slot `+0x720`. The recovered Core
implementation is `AppleBCMWLANCore::setUSB_HOST_NOTIFICATION` at
`0x100120ae0`. Before the carrier payload work it invokes the hidden owner at
Core `+0x48 + 0x1510` through virtual `+0x170`. It then submits the four-byte
value at raw carrier `+0x0c` to
`AppleBCMWLANCommander::runIOVarSet("asym_mit_ext_usb")`. If the dword at raw
`+0x08 <= 1`, it also submits that four-byte value through
`runIOVarSet("asym_mit_ext_usb_chg")`. Each commander status is retained and
returned rather than replaced with an acknowledgement-only result.

The static Core body dereferences the non-null carrier while performing those
operations; it does not independently establish a public null-input status or
the complete carrier allocation. This note therefore makes no Apple
null-input or valid-input return-code parity claim.

## Local divergence

Before this correction, the Intel public setter retained its local null guard,
then called `runSetUSBHostNotification`. That path updated
`TahoeOwnerRegistry`, routed a synthetic hidden callback and synthetic IOVAR
events, and copied three values into `cachedUsbHostNotification*`. Its
`dispatchTransport` immediately completed an async context with status zero
and returned success; it did not reach an Intel USB/coexistence hardware
backend. Scoped source inspection finds no local
`asym_mit_ext_usb`/`asym_mit_ext_usb_chg` implementation or equivalent
`runIOVarSet` transport. The three Skywalk cache values occurred only in their
declarations, two initialization/reset blocks, and this setter.

## Local correction

The pre-existing local null rejection remains unchanged. Every non-null
carrier now returns `kIOReturnUnsupported` before invoking the synthetic
owner/transport or mutating cache state, and the three dead Skywalk cache
members plus their initialization/reset sites are removed.

This is a no-backend quarantine, not Apple valid-input return-code parity:
the reference accepts a carrier only while performing its hidden-owner and
Broadcom commander work.

## Verification boundary

The deterministic report requires the reference wrapper/Core/owner/IOVAR
anchors, retained local null guard, non-null fail-closed return, removed
pseudo-state, and lack of an Intel transport equivalent. Build/load and normal
association/traffic gates establish regression safety. No guessed carrier or
private setter ioctl is issued, so runtime results must not be presented as
direct setter execution. Radio OFF/ON remains excluded because the A2DF
baseline independently reproduces its WCL lifecycle panic.
