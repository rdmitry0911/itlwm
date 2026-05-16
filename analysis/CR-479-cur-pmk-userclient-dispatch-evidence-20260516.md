# CR-479 Tahoe CUR_PMK SET ingress dispatch evidence (2026-05-16)

correlation_id: CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion
review_stage: REFERENCE_DECOMP_RECOVERY_FOLLOWUP
provenance: Static symbol-table evidence extracted from the Tahoe BootKC
KDK symbol map at `/srv/project/ghidra_output/kdk_symbols.txt` on the
Ghidra host. The KDK is the standard source for IO80211Family symbol
recovery in this project and is the same evidence base accepted by the
prior auditor import review for the CUR_PMK carrier layer.

## Scope

Recovered the missing piece of the CUR_PMK active dispatch path that
neither the rev3 nor the rev4 Stage 1 request could establish: whether
selector 0x168 / IOC 360 reaches the V2 controller card-specific bridge
(`AirportItlwm::handleCardSpecific`) for the SET direction or only
through a separate IO80211APIUserClient-mediated path that ends at the
recovered Skywalk virtual receiver at vtable offset `+0x1770`.

## Recovered IO80211Family CUR_PMK dispatch symbols

The following symbol set is the authoritative kernel-side dispatch
surface for `apple80211setCUR_PMK` / `apple80211getCUR_PMK` on Tahoe
26.3 BootKC.

| Address                | Symbol                                                                                  | Role                                              |
| ---------------------- | --------------------------------------------------------------------------------------- | ------------------------------------------------- |
| `0xffffff80021e61fd`   | `apple80211getCUR_PMK(IO80211SkywalkInterface *, apple80211_pmk *)`                     | Selector 0x16a GET trampoline                     |
| `0xffffff80021eb3b9`   | `apple80211setCUR_PMK(IO80211SkywalkInterface *, apple80211_pmk *)`                     | Selector 0x168 SET trampoline                     |
| `0xffffff80022003a1`   | `getCUR_PMK(IO80211Controller *, IO80211SkywalkInterface *, IO80211APIUserClient *, apple80211req *)` | UserClient GET dispatcher (req validator before trampoline) |
| `0xffffff80022a7149`   | `getCurPmk(IO80211Controller *, IO80211SkywalkInterface *, apple80211req *, bool)`     | BSDCommand GET dispatcher                         |

A directed `grep -E "CUR_PMK|CurPmk|Cur_pmk|curPMK"` over the entire
`kdk_symbols.txt` returns those four IO80211Family entries plus the
AppleBCMWLAN-side `getCUR_PMK` receivers (`AppleBCMWLANInfraProtocol::getCUR_PMK`,
`AppleBCMWLANCore::getCUR_PMK`, `AppleBCMWLANCore::saveCUR_PMK`). No
`setCurPmk` BSDCommand dispatcher exists. No `setCUR_PMK` UserClient
dispatcher with the `(IO80211Controller *, IO80211SkywalkInterface *,
IO80211APIUserClient *, apple80211req *)` signature exists. The
underlying static-helper pattern used by every other Apple80211
selector is missing for the SET direction of CUR_PMK.

For comparison, CIPHER_KEY uses the full standard pattern:

| Address                | Symbol                                                                                  | Role                                              |
| ---------------------- | --------------------------------------------------------------------------------------- | ------------------------------------------------- |
| `0xffffff80021e2774`   | `apple80211getCIPHER_KEY(IO80211SkywalkInterface *, apple80211_key *)`                  | Selector GET trampoline                            |
| `0xffffff80021e78d9`   | `apple80211setCIPHER_KEY(IO80211SkywalkInterface *, apple80211_key *)`                  | Selector SET trampoline                            |
| `0xffffff8002206ea8`   | `setCIPHER_KEY(IO80211Controller *, IO80211SkywalkInterface *, IO80211APIUserClient *, apple80211req *)` | UserClient SET dispatcher                          |
| `0xffffff80022d411e`   | `setCipherKey(IO80211Controller *, IO80211SkywalkInterface *, apple80211req *, bool)`  | BSDCommand SET+GET dispatcher (selected by bool)   |

The CIPHER_KEY family is reachable through both userspace paths:
BSDCommand for `ioctl(SIOCSA80211 | SIOCGA80211)` traffic, and
UserClient for `IO80211APIUserClient::externalMethod` traffic.

## Active SET CUR_PMK dispatch path

The absence of a `setCurPmk` BSDCommand static helper in IO80211Family
means there is no kernel-side route from `processBSDCommand` /
`processBSDCommandGated` -> `setCurPmk` -> `handleCardSpecific` for the
SET direction of selector 0x168. Tahoe userspace airportd that wants to
publish a current PMK to the driver must dispatch the request through
the IO80211APIUserClient external-method surface, which:

1. Receives the selector 0x168 from userspace through
   `IO80211APIUserClient::externalMethod`.
2. Dispatches into `apple80211setCUR_PMK` trampoline at
   `0xffffff80021eb3b9`.
3. The trampoline passes the `+0xcc8` command gate on the Skywalk
   interface vtable.
4. The trampoline invokes the Skywalk virtual receiver at vtable
   offset `+0x1770` with `(this, apple80211_pmk *)`.

None of these four steps reach `AirportItlwm::handleCardSpecific`,
`routeTahoeSkywalkIoctl`, `shouldRouteTahoeSkywalkIoctlReq`, or
`processApple80211Ioctl`. The local IOC 360 dispatcher case added in
rev3 / rev4 is only reachable for the GET direction (via the
BSDCommand `getCurPmk` static helper which exists at
`0xffffff80022a7149`). The SET direction is unreachable through the
local card-specific bridge regardless of whether the V2 allow-list
lists `APPLE80211_IOC_CUR_PMK`.

## Implication for the local driver

The rev4 V2 `handleCardSpecific` allow-list entry remains defensible
for the GET direction (since `getCurPmk` exists and routes through
BSDCommand), and is harmless for the SET direction (a request that
never arrives is never dispatched). It is NOT, however, the active SET
ingress.

To service userspace SET CUR_PMK delivery, the local driver must
override the IO80211SkywalkInterface vtable slot at offset `+0x1770`
with a project-owned method that calls the existing
`installExternalPmkLocked` helper. Adding this override requires
recovering the exact set-side vtable layout of IO80211SkywalkInterface
between the already-mapped slots (notably setCIPHER_KEY's slot in the
Apple-side vtable) and `+0x1770`, then placing pure-virtual /
override declarations in `IO80211InfraProtocol.h` so the C++ compiler
positions the override at the correct vtable offset without shifting
other already-tested SET-side overrides (`setCIPHER_KEY`,
`setRSN_IE`, `setWCL_*`, and others).

That recovery is a focused decomp task that depends on direct vtable
walk of `__ZTV23IO80211SkywalkInterface` at `0xffffff80023e5950` with
chained-fixup decoding (`LC_DYLD_CHAINED_FIXUPS` kernel-cache format)
to map each vtable slot to its function pointer and then to a
KDK symbol. The static recovery for that walk is not yet complete;
attempts to decode the chained-fixup target pointers without the
proper kernel-cache fixup parser produced raw chain-link entries
rather than function pointer targets in the slots near `+0x1770`.

## Provenance and verification path

- Primary evidence file: `/srv/project/ghidra_output/kdk_symbols.txt`
  on the Ghidra host (`ssh 192.168.40.116`). The file is the standard
  symbol map produced by the project's KDK extraction pipeline for the
  Tahoe 26.3 BootKC and is the same evidence base referenced by the
  prior auditor-accepted import review.
- Reproducer command: from the Ghidra host,
  `grep -E "CUR_PMK|CurPmk|Cur_pmk|curPMK" /srv/project/ghidra_output/kdk_symbols.txt`
  and
  `grep -E "__ZL.*set[A-Z_]+P17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req" /srv/project/ghidra_output/kdk_symbols.txt`.
  Neither returns a `setCurPmk` or `setCUR_PMK`-named static helper
  with either dispatcher signature; only the trampoline and the GET
  side dispatchers exist.
- Cross-checked by enumerating all `apple80211setX` trampolines in
  `0xffffff80021e9XXX..0xffffff80021ec7XX` and confirming that
  `apple80211setCUR_PMK` is a pure trampoline that does not have a
  matching SET static helper (`__ZL13setX...`) anywhere in the binary.

## Next required work

The active CUR_PMK SET ingress closure for the local driver is a
decomp-first task that the carrier-layer Stage 1 currently submitted
cannot complete inside its own claim scope: recovering the exact
local-class vtable position for the SET-side Skywalk receiver at
`+0x1770` is an additional reference-decomp project, not an incremental
code change. The recovered KDK symbol evidence here is the input
artifact that bounds that decomp task.
