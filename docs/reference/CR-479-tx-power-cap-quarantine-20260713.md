# CR-479: TX-power-cap false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`BYPASS_TX_POWER_CAP` and `DUAL_POWER_MODE`. It does not claim that the Intel
backend implements the Apple TX-power-cap firmware owner.

## Reference contract

Tahoe 25C56 routes `DUAL_POWER_MODE` through public bridge
`0xffffff8001522f42` into `AppleBCMWLANCore::setDUAL_POWER_MODE` at
`0xffffff80016176e2`:

- `NULL -> 0xe00002bc`;
- non-null input stores two signed dwords at Core `+0x4d3c/+0x4d40`;
- the Core immediately calls `0xffffff800160b3e0`, which evaluates the
  TX-power policy and sends firmware `txcapstate` through the Core `+0x1520`
  transport.

The same Core TX-power-cap owner serves `BYPASS_TX_POWER_CAP`: it records the
requested boolean and reevaluates/sends the policy. A successful Apple path is
therefore firmware-owned state transition work, not a caller-visible cache or
a generic command-completion record.

## Local divergence

Before this correction, non-null bypass entered
`TahoeCommanderV2::runSetBypassTxPowerCap`; non-null dual-power writes cached
values and calls `TahoeOwnerRegistry::syncDualPowerMode`. The commander's
transport only begins and completes a local context with status zero. No Intel
firmware `txcapstate` transport exists behind either entry point, so callers
were told the policy was accepted although no radio policy could change.

## Local correction

Both setters retain the recovered null error. Every non-null request now
returns `kIOReturnUnsupported` before cache, registry, or commander mutation.
The three interface-side pseudo-cache fields are removed, and payload parity
records this as a quarantine rather than a firmware-send implementation.

This is a local no-backend quarantine, **not Apple valid-input return-code parity**:
Apple performs real Broadcom firmware work for a valid carrier.

## Direct runtime ingress boundary

The bounded root-only public ioctl probe sent selector 622 with a four-byte
bypass carrier and selector 631 with an eight-byte dual-power carrier. On the
current baseline both requests stop at the outer unsupported gate with errno
`102` before a driver-setter probe can be observed. That proves only that this
public route is not a direct runtime exerciser of the private setter; it is not
used to infer either setter's return semantics. The captured output is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-tx-power-cap-quarantine-20260713/preflight/`.

## Deterministic guard

`scripts/tx_power_cap_quarantine_report.py --check` requires:

- the three 25C56 reference anchors and this no-parity boundary;
- retained null guards plus unsupported non-null returns in both public
  entrypoints;
- no entrypoint call to the synthetic commander/registry path;
- removed interface pseudo-caches;
- the synthetic transport's status-zero-only shape; and
- the retagged payload-parity inventory entry.

## Runtime qualification

Radio OFF/ON is excluded: the A2DF baseline and the old TX-cap candidate both
panic in the same WCL lifecycle chain, so it cannot attribute this isolated
surface. Qualification instead uses a clean Tahoe build, AuxKC identity/load
check, saved-profile rejoin, bounded bidirectional ping/iperf traffic, and
guest/host fault filters. If no private setter ingress is available, that is
reported as regression coverage rather than direct selector execution.
