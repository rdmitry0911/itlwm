# CR-501 — legacy P2P fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the three historical `AirportItlwm` P2P SET
handlers implemented in `AirportAWDL.cpp`:

- `APPLE80211_IOC_P2P_LISTEN` (92);
- `APPLE80211_IOC_P2P_SCAN` (93); and
- `APPLE80211_IOC_P2P_GO_CONF` (98).

Both the historical controller and historical virtual-interface dispatchers
retain their existing `IOCTL_SET` routes and typed carriers.  No GET route,
selector number, ABI declaration, P2P event producer, interface lifecycle, or
Tahoe Skywalk/V2/HP2P method changes here.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`
and outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`.  Its embedded Wi-Fi
`MH_KEXT` begins at file offset `0x1fe3000` and has UUID
`8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

The nested `LC_SYMTAB` was parsed directly from that KEXT rather than trusting
Ghidra function labels.  The exact external section symbols are:

| Public stub | VM address |
| --- | --- |
| `setP2P_ENABLE` | `0xffffff80021c40e4` |
| `setP2P_LISTEN` | `0xffffff80021c40ef` |
| `setP2P_SCAN` | `0xffffff80021c40fa` |
| `setP2P_GO_CONF` | `0xffffff80021c417b` |

Each has the identical 11-byte body
`55 48 89 e5 b8 0e 28 82 e0 5d c3`:

```text
push rbp
mov  rbp, rsp
mov  eax, 0xe082280e
pop  rbp
ret
```

It reads neither public argument and has no call, transport, state, or event
operation before its fixed nonzero return.  `0xe082280e` is deliberately kept
numeric: the local/public headers do not establish a canonical name, and it
is not `kIOReturnUnsupported` (`0xe00002c7`).  The raw records and their
manifest are retained under
`docs/reference/artifacts/p2p-public-fixed-stubs-bootkc-current/`.

An earlier 26.3 Ghidra slice named `setP2P_LISTEN` began in the middle of an
instruction and is explicitly not used for this correction.

## Local divergence and correction

Before this correction, all three local handlers ignored both their object and
typed carrier and returned success unconditionally.  They had no P2P
transport, state transition, event publication, or carrier read.  The local
handlers now leave both arguments unread and return the recovered fixed
`0xe082280e` status.

The current exact reference covers modern public stubs; `AirportAWDL.cpp` is
compiled only by historical source phases.  Applying the same fixed stub to
those otherwise blind-success handlers removes a false capability claim.  It
does not claim that Apple historical P2P implementation, caller population,
or runtime reachability is otherwise identical.

## Preserved boundaries

- The declarations and `IOCTL_SET` routes for 92, 93, and 98 remain intact;
  GET is still not introduced.
- Declared but unimplemented historical P2P selectors 91 and 99–101 remain
  outside this patch.
- The effectful normal `SCAN_REQ` path remains distinct from P2P scan.
- Historical virtual-interface create/delete (94/95), including P2P-interface
  and newer APSTA branches, remain unchanged.
- Separate Tahoe `getP2P_DEVICE_CAPABILITY` and `setHP2P_CTRL` surfaces are
  unchanged; Tahoe does not compile `AirportAWDL.cpp` in its source phase.

## Verification boundary

`scripts/legacy_p2p_fixed_stub_alignment_report.py --check` verifies current
raw identity and manifest, all three fixed stub bodies, both historical routes,
the retained declarations/ABI, absence of new GET or sibling routes,
preservation of normal scan and virtual-interface behavior, Tahoe source-phase
exclusion, and the scope above.

No private selector/carrier is invoked.  This layer makes no deployment,
radio, association, traffic, P2P discovery, firmware, event, carrier-layout,
or runtime-execution claim.
