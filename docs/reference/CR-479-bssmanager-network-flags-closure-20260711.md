# CR-479: BssManager network-flags no-producer closure

Date: 2026-07-11

## Scope

This note resolves the remaining question around
`IO80211BssManager::setNetworkFlags(bool, unsigned int)`. It records the
reference result that is relevant to AirportItlwm's driver-owned BssManager:
the 25C56 BootKC contains the direct writer but no static producer of a
network-flags mask. The local driver must therefore keep the ABI declaration
without manufacturing a call, a mask, or an enable polarity from association
state.

## Reference evidence

The analyzed image is the macOS 26.2 build 25C56 guest
`BootKernelExtensions.kc`, SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.

`IO80211BssManager::setNetworkFlags(bool, unsigned int)` is at
`0xffffff8002242562`. Its body uses `ESI` as an enable bit and either ORs or
clears the `EDX` mask in the manager state dword at `+0x140`. That ABI remains
documented in `CR-479-bssmanager-network-auth-writer-abi-20260709.md`.

Two independent 25C56 reference checks find no producer:

- Ghidra `getReferencesTo(0xffffff8002242562)` reports `count=0`.
  The retained output is
  `10.7.6.112:~/Projects/ghidra_output/aiam_bssmanager_setnetworkflags_xrefs_20260709.txt`
  (SHA-256 `ab3fd0fb960c697724925c4a8042a97ddc1a0b641bc5baa4e7c1aa01242d5c66`).
- A raw packed-address scan of the same image finds one occurrence at file
  offset `0x34c2148`. Ghidra maps it to `0xffffff80035ef038` in
  `__LINKEDIT`, next to the symbol metadata for the exported direct-call
  surface; it is not a caller in executable or regular data memory. The
  current direct-reference/decompile capture is
  `10.7.6.112:~/Projects/ghidra_output/aiam_bssmanager_networkflags_xrefs_25C56_20260710.c`
  (SHA-256 `e5e46c03fe6abfacad53d7b0a84fecf939d794f3119458389eb50b58887efb5c`).

The method is a non-virtual direct-call IO80211Family export. Thus a call by
the Apple driver would be visible as either a direct reference or a resolved
call pointer; neither exists in the inspected 25C56 BootKC. The exported
writer is an available framework primitive, not evidence that the current
AppleBCMWLAN association lifecycle writes a mask.

## Local disposition

AirportItlwm owns its BssManager at the same driver boundary as the recovered
Apple owner and seeds only values with a recovered producer. The local source
retains the exact `void setNetworkFlags(bool, unsigned int);` declaration for
ABI completeness, but does not add a local setNetworkFlags call. Rate, MCS,
auth, band, RSSI, ad-hoc, and current-BSS writes remain independently proven
and are unaffected.

`scripts/bssmanager_networkflags_closure_report.py` makes this restriction
deterministic: it verifies the reference anchors, declaration, and that no
AirportItlwm, itl80211, or non-declaration include source contains a runtime
call. Its checked-in result is
`evidence/state/bssmanager_networkflags_closure_report.json`.

## Non-claims

- This does not infer a value from association, auth type, BSSID, SSID, WCL,
  or any public CoreWLAN symptom.
- This does not remove or alter the framework writer ABI.
- This does not close or alter the separately classified CoreWiFi/airportd
  privacy boundary for public SSID, BSSID, current-scan, or profile requests.
