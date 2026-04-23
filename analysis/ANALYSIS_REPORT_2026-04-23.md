# ANALYSIS REPORT 2026-04-23

## FIX_CANDIDATE

- anomaly_id: TAHOE-CR052-CONSOLE-DIAG-001
- class: DIAGNOSTIC_INSTRUMENTATION
- symptom:
  - `CR-052` runtime restores Wi-Fi UI visibility and visible scan results, but
    association still fails.
  - the previous diagnostic-layer attempt was not behavior-neutral: it changed
    the userclient/bind surface and later interacted with boot/init paths,
    producing UI invisibility or init hangs.
- expected system behavior:
  - diagnostics must be reachable after one reboot without changing the
    Apple80211 bind path, boot trigger path, association semantics, scan
    semantics, or data-path return semantics unless an explicit operator-set
    intervention flag is enabled.
- actual behavior:
  - `CR-052` has no stable console-only diagnostic control plane that can
    observe association and data-path ownership splits after the system is
    already booted.
- exact divergence point:
  - missing separate diagnostic-only IOService/userclient independent from:
    - `AirportItlwm` controller `IOUserClientClass`
    - `AirportItlwmBootNub`
    - Apple80211/CoreWiFi bind/userclient flows
    - WCL/Apple80211 active request producers
- evidence from runtime:
  - user installed `restore`/`CR-052` build at commit `1e5c656`
  - after reboot networks are visible in UI
  - association to visible networks fails
- evidence from decomp:
  - not required for this instrumentation package because the package does not
    claim a reference behavior fix and does not change system-facing Wi-Fi
    contracts by default
- fix justification path: DIAGNOSTIC_INSTRUMENTATION
- exact fix package:
  - add a separate `AirportItlwmDiagnosticsService` child IOService created by
    the controller after normal controller registration
  - expose diagnostics through that service's own userclient only
  - do not add `IOUserClientClass` to `NetworkController`
  - do not attach diagnostics to `AirportItlwmBootNub`
  - do not trigger boot, scan, association, Apple80211 IOCTLs, or WCL methods
    from diagnostic `get` commands
  - add passive ring/counter telemetry at natural association and data-path
    seams
  - add explicit `set` commands for trace masks and intervention block masks;
    all intervention masks are off by default
- forbidden alternatives considered and rejected:
  - prefpane, because user requested console-only diagnostics
  - opening the controller service directly, because the previous regression
    already showed that controller userclient shape is part of the bind risk
  - reusing `BootNub` for diagnostics, because that service is already the
    Tahoe async boot trigger and must remain single-purpose on the good base
  - active probe calls inside `get`, because they would contaminate runtime
    evidence by becoming an extra Apple80211/WCL consumer
- verification plan:
  - build Tahoe kext from `restore`
  - build console tool
  - before reboot: verify diagnostics service is not on controller/BootNub
    personality
  - after reboot: use only `get config`, `get snapshot`, `get trace`,
    `get scan-cache` before any `set block ...` intervention
