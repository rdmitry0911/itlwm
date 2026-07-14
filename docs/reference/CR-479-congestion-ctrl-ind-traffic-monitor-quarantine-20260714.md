# CR-479 — CONGESTION_CTRL_IND traffic-monitor quarantine

Date: 2026-07-14

## Scope

This correction covers only AirportItlwmSkywalkInterface::setCONGESTION_CTRL_IND.
The former local implementation decoded a one-byte local carrier, wrote a
registry field, and returned success. That field had no local reader or owner.

This is a standalone no-read quarantine. It adds no private ioctl, direct
setter invocation, radio transition, deployment, association, or traffic.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN,
SHA-256
4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab.

- Infra setCONGESTION_CTRL_IND at 0x1000192fc dispatches directly to
  AppleBCMWLANCore::setCONGESTION_CTRL_IND at 0x1001429f4.
- Core has no NULL return contract. It logs through virtual +0x7a0, reads
  effective carrier byte +0, writes the Boolean result to
  (Core + 0x48) + 0x89d2, and returns success.
- The byte is live traffic-monitor state, not an isolated QoS cache:
  collectRealTimeAppCongestionState() at 0x10013d482 returns it at
  0x10013d5a9. trafficMonitorCallback() at 0x10013d5d6 calls the collector
  at 0x10013d747 and 0x10013d85b, including the WMM-reset decision path.

## Local boundary and non-claims

apple80211_congestion_control_indication remains forward-only locally. There
is no APPLE80211_IOC_CONGESTION_CTRL_IND route, no local
collectRealTimeAppCongestionState() or trafficMonitorCallback() backend, and
no safe carrier ABI to inspect.

The local setter retains its existing kIOReturnBadArgumentTahoe NULL safety
boundary and returns kIOReturnUnsupported for every non-null carrier before
reading it. The synthetic carrier, registry field, sync helper, Boolean helper,
and obsolete offset constant are removed. This no-owner quarantine does not
claim Apple NULL or valid-input status parity. The virtual slot remains for ABI
completeness.

## Deterministic guard

scripts/congestion_ctrl_ind_traffic_monitor_quarantine_report.py --check
verifies the reference anchors, retained slot and local NULL safety gate,
absence of the synthetic carrier/state and scoped traffic backend, forward-only
local scope, and corrected historical documentation. Runtime deployment remains
blocked by the guest forced-off Wi-Fi lifecycle state.
