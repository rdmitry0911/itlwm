# CR-479: MWS RFEM configuration false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_RFEM_CONFIG_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change another
MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[652] wrapper at `0x100019694` tail-dispatches to
`AppleBCMWLANCore::setMWS_RFEM_CONFIG_WIFI_ENH` at `0x10014131e`:

- `NULL -> 0xe00002bc`;
- a valid opaque carrier contributes ten effective dwords at `+0` through
  `+0x24`; the `+0x24` RF-band bitmap is stored first, followed by the nine
  channel bitmap dwords at Core `+0x292c` through `+0x2950`; and
- the setter dispatches virtual offset `+0x640`.

The recovered `__ZTV16AppleBCMWLANCore` starts at `0x1003a10d8`; its
virtual-base entry `+0x640` is `0x1003a1728`, holding
`0x100122ce6`, `setWiFiRfemConfigBitmapWiFiEnh`. That terminal owner constructs
a `0x24`-byte `mws` firmware-iovar packet with command `0x1018000` / opcode
`8`: it maps RF-band bits `0..2` and packs the low-16-bit portions of nine
channel bitmaps. It uses `AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSWiFiRfemConfigCoexBitmapsWiFiEnhAsyncCallback`, or `runIOVarSet`, and
returns transport failure. The reference surface therefore has real firmware
work, not merely a cache acknowledgement.

The recovered effective reads establish only the ten dwords described above;
they do not prove the complete public-carrier allocation size.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise copied ten
dwords into `cachedMwsRfemConfig` and returned success. Scoped local source
inspection finds that cache only in its declaration, two initializers, and this
setter. There is no local `mws` iovar, RFEM owner, matching callback,
`sendIOVarSet`, or `runIOVarSet` backend. Existing generic Intel coexistence
code is not treated as an equivalent of Apple's MWS firmware contract.

## Local correction

Retain the recovered null error. Every non-null request now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead cache
plus its initializers are removed.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts the input and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, ten-dword/RF-band/subcommand description, retained null guard,
non-null fail-closed return, removed pseudo-state, and absence of the local
RFEM owner or transport tokens. Build/load and normal association/traffic gates
establish regression safety. No guessed opaque carrier or private setter ioctl
is issued, so runtime results must not be presented as direct setter execution.
Radio OFF/ON remains excluded because the A2DF baseline independently
reproduces its WCL lifecycle panic.
