## Native scan handoff and post-target cancellation — 2026-09-12

Limited alpha for Tahoe, with hardware evidence on IWN/Intel 6235. This is not
full reference parity, lossless roaming, or IWM/IWX radio qualification.

### Changes

Source `c123d132aad014356aa6a8864a731c58ae4f6764` includes two functional fixes
after the preceding `477ab0af` release:

- `092a7479`: a new native network-selection scan survives the asynchronous
  cancellation of the previous IWN roam scan. Its exact requested band/plan
  waits for the old physical terminal, then starts on the normal worker and
  firmware command path. Unrelated Busy cases remain honest rejections.
- `c123d132`: ordinary association cancellation retires the current logical
  roam even after it has selected/replaced the target BSS. Controlled source
  departure and target continuation retain their ownership. Physical scan/TX
  lifetimes and received protocol results are not fabricated or cleared early.

### Exact installed-image evidence

- Controlled held SAE Commit followed by ordinary CoreWLAN leave reaches an
  active post-target phase4 owner, clears that exact owner, and permits later
  successful native roaming. A separate explicit LabAP selection subsequently
  gets the requested BSS and DHCP without off/on or reboot. Its strict traffic
  result is20/20 forward,19/20 reverse, with one forward response above1s.
- The original roam-scan-abort/WPA2 selection overlap executes the actual
  queued handoff, matching old terminal, replay worker and new2GHz command;
  DHCP and20/20 packets each direction pass.
- Native open->WPA2 selections pass DHCP and20/20 each way with no intervening
  forced LabAP selection, radio toggle or reboot. Native WPA3 also passes.
- Saved WPA3 off/on restores its profile and address. Actual S3 is observed,
  then the same boot/image recovers WPA3/DHCP and20/20 bidirectional traffic
  through Wi-Fi alone before diagnostic USB networking is restored.
- After S3, AP open/WPA2/WPA3 each passes real external AX211 negotiation,
  DHCP,20/20 forward and10/10 cold-ARP reverse, exact118-byte HTTP through
  USB/NAT, ordinary stop and10/10 restored STA gateway packets.
- Post-S3 ca->02 native roaming reaches its requested BSS with one SAE
  exchange, retaining249/250 forward and245/250 reverse packets. The reverse
  request fails before target AUTH and stays on02 with250/250 source traffic;
  the driver records its supersession by another WCL request. This is not an
  AP-absence diagnosis or a successful reverse transition. Its exact cause
  remains open. A separate final restored-link check passes20/20 each way.

The common hard-cancellation correction passes production-function execution
on Linux and Tahoe:30 phase/state cancellations,145 carrier/replacement cases,
25 admission/retirement cases, and both complete lifecycle/trace aggregates.
The IWN scan handoff separately executes46 full ingress/worker/terminal cases
and31 command/doorbell cases. Hardware/IOKit boundary doubles in source tests
are not radio evidence. Existing separate IWM/IWX SAE failure-cleanup semantic
tests remain failing and are not relabelled by the passing aggregates.

### Retained limitations

This image still has packet losses and latency excursions. The first loaded
baseline was20/20 forward,19/20 reverse; automatic recovery after one controlled
leave used a different saved BSS/address and produced19/20,20/20. The later
passing controls do not erase those failures or establish same-profile policy.
One controlled AP-enable experiment did not exercise hard cancellation; another
roam request returned Busy while an independently admitted roam was active.
Those complete controllers remain failed even though native leave subsequently
retired the observed owner. The failed reverse roam is retained too; attribution
to the latest patch as a regression has not been established.

Full GUI/profile/security/off-on/sleep combinations, an already active AP across
sleep, simultaneous Wi-Fi STA uplink plus AP, ad hoc, complete SAE/H2E
interoperability and IWM/IWX hardware qualification remain open. Physical
10.90.10.22 was not modified or rebooted for these tests.

### Artifact

The archive contains the installed/tested bundle, without a packaging rebuild.
Installed, build and extracted bundles compare identically; all357 production
source hashes verify.

- Mach-O UUID: `D636A28B-6A9B-3CCE-AF28-5779C5F980C8`.
- Mach-O SHA256: `9e06b1aab08ff2297d2951be20cd26a8fc19f8923aef5cb4700e86576ce5c6d8`.
- ZIP: `AirportItlwm-Tahoe-v2.4.0-alpha.kext.zip`,15,695,042bytes.
- ZIP SHA256: `0ac3048d7df273c298d45efa16ac62056668794999617c10815c9103b618054a`.

[Cancellation implementation and complete positive/negative runtime evidence](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_REASSOC_POST_TARGET_CANCELLATION_20260912.md).
[Scan handoff diagnosis and correction](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_WCL_SCAN_ABORT_HANDOFF_GAP_20260912.md).

### Earlier history

[Previous477ab0af release notes, including their limitations and historical artifact identity](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/releases/V2_4_0_ALPHA_HISTORY_THROUGH_477AB0AF_20260912.md).
The intermediate9B5B handoff-only candidate was not published. Only the artifact
identity above describes this update; older linked notes are historical.
