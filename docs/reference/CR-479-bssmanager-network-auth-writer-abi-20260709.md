# CR-479 BssManager network/auth writer ABI

Date: 2026-07-09

## Scope

This note closes only the direct-call ABI for two `IO80211BssManager` writers
on the guest macOS 26.2 build 25C56 BootKC. It does not add local calls to these
writers. The producer mask/polarity for network flags and the associated auth
type byte-array source remain separate evidence requirements.

## Reference Evidence

Guest BootKC:

- `/System/Library/KernelCollections/BootKernelExtensions.kc`
- macOS build 25C56
- size: 66,437,120 bytes

Guest `nm` anchors:

- `0xffffff8002242562`
  `__ZN17IO80211BssManager15setNetworkFlagsEbj`
- `0xffffff8002243084`
  `__ZN17IO80211BssManager21setAssociatedAuthTypeEPht`

Ghidra 25C56 evidence:

- project: `10.7.6.112:~/Projects/ghidra_output/wifi_analysis_25C56`
- script output:
  `10.7.6.112:~/Projects/ghidra_additional/aiam_bssmanager_25C56_ranges_20260709.txt`
- run with `-max-cpu 24`, `-noanalysis`, `-readOnly`

Recovered `setNetworkFlags(bool, unsigned int)` body:

- tests `ESI` for the boolean enable argument;
- when true, ORs `EDX` into dword ivar `state +0x140`;
- when false, bitwise-negates `EDX` and ANDs it into dword ivar `state +0x140`;
- returns without setting a status value.

Recovered `setAssociatedAuthType(unsigned char*, unsigned short)` body:

- returns immediately when the byte pointer argument is null;
- zero-extends `DX` into the length register;
- accepts lengths up to and including `0x101`;
- copies the caller byte array to `state +0x14e`;
- writes the accepted `uint16_t` length to `state +0x14c`;
- returns without setting a status value.

Therefore the local declarations are:

```c++
void IO80211BssManager::setNetworkFlags(bool, unsigned int);
void IO80211BssManager::setAssociatedAuthType(unsigned char *, unsigned short);
```

## Local Closure

`include/Airport/IO80211BssManager.h` now declares both writers with the
25C56-proven mangled signatures. `tests/tahoe_payload_builders_test.cpp`
type-checks both declarations with `static_assert`.

No runtime seed path was added in this batch. Prior writer-reference scans did
not prove a local producer for the network-flags mask/polarity or the associated
auth type byte-array source, so adding calls would fabricate state rather than
match the reference.
