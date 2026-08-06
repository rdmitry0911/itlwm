# IWM/IWX per-PHY channel geometry isolation (25C56, 2026-08-06)

## User-visible failure

After a Wi-Fi off/on transition, the AX211 Tahoe guest could reach a primary
STA `RUN` epoch on `LabAP` channel 153/80 and then receive a standard Internet
Sharing request for an AP on channel 11.  The old IWX helper accepted the AP
channel argument but derived both channel width and control position from the
global primary `ic_bss`.  The secondary AP PHY was therefore encoded as the
impossible channel 11/80 MHz combination.  API-68 firmware raised
`NMI_INTERRUPT_UMAC_FATAL` as soon as the AP worker submitted that PHY epoch.

The pre-fix physical trace contained the exact boundary:

```text
IWX AP start channel=11 ic_state=4 flags=0x79 same_phy=0 phy=1 mac=1
NMI_INTERRUPT_UMAC_FATAL
```

## Reference contract

Linux MVM supplies a channel definition to each individual
`PHY_CONTEXT_CMD`.  Current OpenBSD IWX likewise keeps secondary-channel
offset/width state in each `iwx_phy_ctxt`; it does not borrow the associated
STA geometry for another PHY.  The older local IWM/IWX ABI has no per-PHY
wide-channel AP carrier yet, so its safe equivalent is:

- use the associated BSS width only when the command's target channel is the
  associated BSS channel;
- otherwise program the secondary context as HT20;
- derive the control-position offset from the target channel itself.

The same rule is applied to IWM and IWX even though the live reproducer is
AX211/IWX.  This keeps the paired modern Intel implementations from
diverging and prevents the same global-controller leak when IWM gains a
different-channel runtime fixture.

## Candidate identity and admission

The disposable Tahoe 26.2 / 25C56 guest loaded the dirty-diff candidate with
source identity `1f4190a6832b`:

- Mach-O UUID: `3FAFB388-4F3D-3CC9-8219-DF03F12FB06C`;
- binary SHA-256:
  `e2a8a30b64fccd8ae16c1d779b14ec84fea63ca3e19764c52cc28aaf285863b7`;
- Debug/OptOut build completed against the guest's exact BootKC;
- all 1074 undefined symbols resolved against that BootKC;
- a private exact five-member AuxKC preflight passed before transactional
  activation.

The candidate is an unsigned laboratory build, not a signed artifact.

## Physical runtime result

The post-fix driver was exercised through the real CoreWLAN and standard
Internet Sharing producers after Wi-Fi off/on.  It repeatedly observed the
primary channel 153/80 epoch and the separate AP channel-11 carrier.  A lower
start which reached the stable primary `RUN` boundary completed the entire
secondary resource transaction:

```text
IWX AP start channel=11 ic_state=4 flags=0x79 same_phy=0 phy=1 mac=1
IWX AP stage phy_update error=0
IWX AP stage beacon error=0
IWX AP stage mac_add error=0
IWX AP stage binding error=0
IWX AP stage multicast error=0 queue=1
IWX AP stage broadcast error=0 queue=2
IWX AP stage quotas error=0
IWX AP start complete multicast_queue=1 broadcast_queue=2
```

The same candidate also completed that resource sequence after a bounded
foreground-scan handoff.  In both cases the PHY update itself produced no
`NMI_INTERRUPT_UMAC_FATAL`, `ADVANCED_SYSASSERT`, or firmware error dump.
This closes the different-channel geometry/assert layer.

## Residual transition layer

This result does not claim the whole concurrent data path.  The saved pure-SAE
STA profile currently reaches lower `RUN` but the public CoreWLAN association
later returns an error and falls back to `SCAN`.  One primary queue can remain
with a pending STA TX descriptor; its independent 15-second watchdog then
resets the device even after HostAP resource creation.  A later trace also
raised a TX-command (`last cmd Id 0x010A001C`, queue 1 pending) UMAC fatal.
Neither event is a `PHY_CONTEXT_CMD` failure, but both prevent a durable
STA+AP traffic claim and form the next high-use reconnect/UI epoch to repair.

## Contract verification

- `scripts/test_iwm_iwx_ap_phy_geometry_isolation_contract.sh`
- `scripts/test_iwm_iwx_ap_ht20_contract.sh`
- `scripts/test_iwn_iwm_iwx_ap_csa_runtime_contract.sh`
- `scripts/test_tahoe_iwx_hostap_lower_epoch_retry_contract.sh`
