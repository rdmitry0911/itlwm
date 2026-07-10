# CR-479 WCL auth/assoc-complete publication

Date: 2026-07-10

## Scope

This batch restores the Tahoe WCL auth/assoc-complete bulletin that follows a
successful STA association edge.

It does not reintroduce legacy public `APPLE80211_M_ASSOC_DONE`, does not add a
CoreWLAN fallback gate, and does not synthesize Dynamic Store or
`networksetup` answers.

## Reference Evidence

Tahoe 25C56 `AppleBCMWLANCore::handleAssocEvent(wl_event_msg_t *)`:

- reads firmware status from `wl_event_msg_t +0x08`;
- maps nonzero status values below `0x100` to `status | 0xe0820400`;
- maps status values at or above `0x100` to `0xe3ff8100`;
- reads firmware reason from `wl_event_msg_t +0x0c`;
- maps nonzero reason values below `0x45` to `reason | 0xe0821000`;
- maps reason values at or above `0x45` to `0xe3ff8100`;
- posts message `0x4e` with length `0x08`, async flag `1`, and the two mapped
  dwords through `IO80211Controller::postMessage(...)`;
- then calls extended-event handling and `AppleBCMWLANJoinAdapter::handleAssoc`.

The recovered instruction window shows `MOV EDX,0x4e`, `MOV R8D,0x8`, and
`MOV R9D,0x1` at both infra-interface publication call sites.

On the consumer side, `WCLJoinManager::associationStatusHandler` accepts only a
present payload of length `0x08`. `WCLJoinManager::handleJoinAssocComplete`
then consumes the auth/assoc status through
`WCLJoinRequest::updateAuthAssocStatus(...)` and advances the join FSM through
the `AUTH_ASSOC_COMPLETE` layer.

The separate high-value enum string `M_WCL_AUTH_ASSOC_COMPLETE` in the corpus is
not the producer used by `handleAssocEvent`; the driver-facing producer evidence
for this edge is the `0x4e` postMessage selector above.

## Local Closure

The local Tahoe `IEEE80211_EVT_STA_ASSOC_DONE` path previously returned without
publishing any message. It now builds the Apple-shaped 8-byte WCL auth/assoc
carrier and publishes message `0x4e` with `{status = 0, reason = 0}` for the
accepted local association edge.

The legacy non-Tahoe path remains unchanged and still publishes
`APPLE80211_M_ASSOC_DONE`.

## Runtime Validation

Validated on Tahoe 25C56 after AuxKC install and reboot:

- loaded kext UUID `AE04EF54-A240-3B1F-A22A-75A6F73D92C2`;
- installed/staged binary SHA-256
  `a5d7b05639ed31c0848f6c0cfd04443f2f10d10da67fa68d05ca3306a0ec346a`;
- `scripts/test_payload_builders.sh` passed on host and guest;
- `scripts/payload_parity_report.py --write/--check` passed with zero
  mismatches;
- `scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  passed on the Tahoe guest and reported all 949 undefined symbols resolved
  against BootKC.

The controlled join to `ITLWM-Lab-3c95c7` reached DHCP `10.77.0.157`.
Raw Tahoe and legacy Apple80211 probes returned SSID `ITLWM-Lab-3c95c7`,
BSSID `80:e4:ba:20:ef:f9`, state `4`, channel `6`, flags `0x8a`, and a
populated `CURRENT_NETWORK` record.

The required paced 240-second stress pass completed with `PING_RC=0` and
`IPERF_RC=0`:

- ping to `10.77.0.1` reported `240 packets transmitted, 240 packets received,
  0.0% packet loss`, RTT `0.601/16.968/146.453/18.794 ms`;
- `/usr/local/bin/iperf3 -c 10.77.0.1 -t 240 -b 20M` transferred `572 MBytes`
  at `20.0 Mbits/sec` sender and receiver;
- post-stress `en1` remained active at DHCP `10.77.0.157`;
- the stress-window fault filter found no panic, CoreCapture, missed beacon,
  deauth, disassoc, `driver not available`, `0xe0822403`, or
  `IO80211QueueCall` signatures.

## Non-Claims

This closes only the missing WCL auth/assoc-complete publication layer. It does
not claim that public `CWInterface.ssid`, `CWInterface.bssid`, or
`networksetup -getairportnetwork` are fixed. On this validated build,
`CWInterface.ssid` and `CWInterface.bssid` remain `nil`, and
`networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.`
