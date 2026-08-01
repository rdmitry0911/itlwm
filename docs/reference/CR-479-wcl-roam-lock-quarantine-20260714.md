# CR-479 — WCL_SET_ROAM_LOCK Intel host-roam backend

Date: 2026-07-14

## Scope

This correction covers only public
`AirportItlwmSkywalkInterface::setWCL_SET_ROAM_LOCK`. The former local handler
accepted a non-null carrier, read byte 0 into two unread cache flags, and
returned success. It was first quarantined as unsupported because the port had
no proven consumer. The runtime closure below supersedes that checkpoint: the
actual Tahoe producer sequence and Intel's real net80211 host-roam owner are
now connected without adding a guessed Broadcom transport or carrier layout.

The raw null guard remains `kApple80211ErrInvalidArgumentRaw` (`0x16`). A
non-null request now consumes only proven byte 0, applies it to the shared
Intel autonomous background-roam owner, and returns the real local operation
status. Reassoc, roam profile, user-cache, explicit scan, key, link, and WCL
event paths remain separate.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_SET_ROAM_LOCK` at `0x100018adc`
  dispatches through virtual `+0x4b0` to Core `0x10011ed1e`.
- A null carrier takes cold path `0x1002082a6`, which writes raw `0x16`.
- A non-null carrier selects RoamAdapter at `+0x15c0`, reads byte 0 as a
  boolean, and tail-jumps to `setRoamLock` at `0x10001e4e0`.
- `setRoamLock` serializes that input as a 4-byte boolean payload for
  `"roam_off"`, calls `sendIOVarSet` `0x10017b900`, records and returns the
  enqueue/transport result, and installs
  `handleRoamOffAsyncCallBack` `0x10001e59e` for asynchronous failure handling.

Those branches are a RoamAdapter state and transport lifecycle. They do not
justify a local byte-cache-and-success substitute, and the byte-0 read alone
does not establish a complete public carrier allocation.

## Local boundary and non-claims

No guessed `roam_off` IOVAR, direct firmware request, private IOCTL, synthetic
callback/completion, explicit scan/reassociation/key mutation, or status
suppression is introduced. The selected Intel owner is narrowly the existing
low-RSSI autonomous background scan and its candidate-selection fence; it is
not the generic coexistence path or an invented firmware command.

At the quarantine checkpoint no direct runtime invocation of this setter was
claimed; its regression boundary was ordinary association and traffic. The
runtime closure below supersedes that historical limitation.

## Runtime closure (2026-08-01)

The quarantine is superseded by a shared Intel host-roam owner. A narrow
Tahoe 25C56 diagnostic build captured the actual WCL sequence around a WPA3
join: `locked=1` immediately before `ASSOCIATE`, followed by `locked=0` after
the SAE/RSN link completed. This agrees with the recovered reference byte-0
boolean and `roam_off` lifecycle.

Intel has no autonomous Broadcom firmware roaming engine. Its equivalent is
the net80211 low-RSSI path that schedules a background candidate scan and may
switch BSS at completion. The public setter now publishes the lock through a
layout-neutral atomic bridge shared by IWN, IWM, and IWX. Locking cancels a
pending autonomous timer and fences an already-active non-WCL background scan
from selecting a replacement BSS. Both the timer callback and RX-side timer
scheduler also consult the lock. Explicit WCL scans, explicit reassociation,
802.11v target scans, and beacon-loss foreground recovery remain available.

The implementation deliberately does not add a guessed Broadcom `roam_off`
IOVAR or alter `ieee80211com`. It returns success because the observed policy
now reaches the actual Intel host-roam consumer, not because a byte was merely
cached. The deterministic report retains its historical filename for callers,
but schema `itlwm-wcl-roam-lock-intel-backend-v2` now requires the live shared
owner and both autonomous-scan gates.

The final IWM/6235 candidate had UUID
`0B6DFCAD-40A1-31AC-A992-EF7CA1AD636B` and binary SHA-256
`cef09452035a15f939fb114f6a46779fe0f2eed4e6a825d498ce08ac58b5566d`.
After removing and recreating only the laboratory `LabAP` profile, Tahoe
issued `WCL_SET_ROAM_LOCK` before `ASSOCIATE`; the final handler returned
`GOOD:0:0x0`. The guest then joined the controlled channel-149 pure-SAE BSS,
obtained `192.168.149.54`, and passed 8/8 ICMP packets. The host station record
showed authenticated, associated, and authorized state plus `MFP=yes`.

With that lock still active, withdrawing exactly the controlled BSS recovered
to the channel-13 WPA3 BSS in about 20 seconds, obtained `172.16.66.120`, and
passed 10/10 ICMP packets. A subsequent radio OFF/ON cycle recovered on the
channel-9 WPA3 BSS in about 40 seconds and again passed 10/10. No panic was
present. This proves the lock does not suppress beacon-loss or radio recovery.
The diagnostic payload sequence proves both `locked=1` and `locked=0`; the
final build directly observed the `locked=1` public status. A final-build
`locked=0` producer call was not observed in these bounded cycles and is not
claimed. `WCL_ROAM_PROFILE_CONFIG` remains unsupported and is a separate,
visible adaptive-roam layer.
