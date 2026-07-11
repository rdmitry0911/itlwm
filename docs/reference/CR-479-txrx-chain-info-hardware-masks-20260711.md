# CR-479 TXRX_CHAIN_INFO hardware masks, 2026-07-11

## Scope

This note closes the caller-visible data source and byte order for
`AppleBCMWLANCore::getTXRX_CHAIN_INFO(...)`. It does not infer a chain mask
from NSS and does not add a Broadcom-iovar emulation layer to the Intel HAL.

## Current 25C56 reference

The current guest BootKC function is
`AppleBCMWLANCore::getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *)` at
`0xffffff800159567c`. The matching artifacts on `10.7.6.112` are:

- `~/Projects/ghidra_output/aiam_txrx_chain_25C56_20260711.c`
- `~/Projects/ghidra_output/aiam_txrx_chain_25C56_20260711.disasm.txt`
- `~/Projects/ghidra_output/aiam_txrx_chain_refs_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_infra_link_properties_25C56_20260711.c`

The function has these exact public effects:

1. A null output returns `0xe00002c2`.
2. A controller state that does not admit the synchronous iovar path returns
   `0xe00002e2` and writes no chain byte.
3. It reads `hw_rxchain`, `hw_txchain`, `txchain`, and `rxchain` in that order.
4. Each successful one-byte result is written immediately at output offsets
   `+0`, `+1`, `+2`, and `+3`, respectively.
5. An individual iovar failure returns `0xe00002bc`; bytes written by earlier
   successful reads are not rolled back.

The same order is present in the earlier 26.3 recovery. Therefore the four
bytes are masks with independent RX/TX meaning, not four copies of a stream
count transformed into `(1 << NSS) - 1`.

The immediate current-BootKC consumer
`IO80211InfraInterface::getInfraLinkProperties(...)` requests selector
`0x176` with length `4`. On success it popcounts output byte `+1`
(`hw_txchain`) and byte `+2` (`txchain`) and publishes the smaller count as
the link's maximum NSS. It fails the complete link-properties operation when
the selector fails. This makes chain-mask production an input to LQM options;
it does not make an associated-peer NSS value a valid source for either mask.

## Intel ownership

The local backends expose authoritative configured antenna masks without
issuing a firmware command from the Apple80211 getter:

- `ItlIwn` owns `iwn_softc::txchainmask` and `rxchainmask`, populated from
  device EEPROM/RF configuration with the existing per-device corrections;
- `ItlIwm::iwm_fw_valid_{tx,rx}_ant(...)` intersects firmware PHY chain bits
  with NVM-valid antenna bits;
- `ItlIwx::iwx_fw_valid_{tx,rx}_ant(...)` performs the corresponding firmware
  PHY/NVM intersection.

Those same per-direction masks are programmed into the Intel firmware's TX
antenna and PHY/RX chain configuration. Intel packet-rate selection may choose
a subset for a particular transmission, but there is no separate persistent
global `txchain`/`rxchain` owner in these backends. Consequently the exact
local mapping is:

| output byte | Apple source | Intel source |
| --- | --- | --- |
| `+0` | `hw_rxchain` | configured valid RX mask |
| `+1` | `hw_txchain` | configured valid TX mask |
| `+2` | `txchain` | configured valid TX mask |
| `+3` | `rxchain` | configured valid RX mask |

This preserves non-contiguous masks and independent RX/TX masks. It also
preserves the reference null return. The Broadcom synchronous-command state
and per-iovar failure returns have no local equivalent because the Intel
getter reads already-owned HAL state and cannot fail after the output pointer
has been validated.

## Prior local divergence and closure

The previous getter used `getTxNSS()`, clamped it, constructed a contiguous
low-bit mask, and copied that value to all four bytes. This lost real antenna
identity, RX/TX asymmetry, and device-specific non-contiguous masks.

The closure adds explicit TX/RX chain-mask accessors to `ItlDriverInfo`, wires
all three Intel backends to their existing authoritative mask sources, and
uses a packed four-byte carrier to publish the reference byte order. Contract
tests use asymmetric, non-contiguous values so a future NSS-derived or
byte-order regression is observable.

## Non-claims

- This does not synthesize Broadcom iovars or add fallback behavior.
- This does not report a per-packet rate-control antenna choice as a global
  chain configuration.
- This does not change association, rate control, firmware antenna selection,
  LQM admission, public CoreWLAN, `networksetup`, or Dynamic Store behavior.
