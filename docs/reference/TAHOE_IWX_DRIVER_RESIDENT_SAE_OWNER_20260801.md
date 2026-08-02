# Tahoe IWX driver-resident SAE owner — 2026-08-01

## Reference boundary

Tahoe 25C56's `AppleBCMWLANCore::checkForWPA3SAESupport()` queries lower
feature `0x41`. The recovered
`AppleBCMWLANJoinAdapter::programPMK(wsec_pmk&)` then sends BCM firmware ioctl
`0x10c` with a 264-byte `wsec_pmk` value. The local reference material is in:

- `ghidra_output/AppleBCMWLANCoreMac_decompiled.c`, functions
  `checkForWPA3SAESupport` and `isSaeH2eEnabled`;
- `ghidra_output/CR-479-pmk-delivery-channel-20260515/`
  `AppleBCMWLANJoinAdapter_programPMK_decompiled.c` and `MANIFEST.txt`.

The ioctl is a BCM firmware ABI and must not be fabricated on Intel. Its IWX
equivalent is a lower, driver-resident owner which places a verified SAE PMK
in net80211's host-side `ic_psk`, binds it to the selected BSS and permits the
ordinary Association Request doorbell only while the same public completion
identity remains current.

## Implemented owner

The selected API-68 IWX runtime now owns the entire lower transaction:

1. A bounded WCL `CIPHER_PWD` record is staged in a scrubbed private slot.
2. The selected-BSS `S_AUTH` hook consumes it into the existing in-kext
   HNP/H2E SAE engine.
3. Commit and Confirm use IWX's real management descriptor and native TX_DONE
   path. Driver tickets occupy an independent high-bit cancellation domain.
4. Peer Confirm validation claims the SAE PMK and scalar-derived PMKID under
   the selected-BSS and engine leaves; it does not route through an Agent,
   external-PMK installer or fake BCM ioctl.
5. The queued Association Request is checked before header trim and again at
   the real IWX `qid << 16 | cur` WRPTR write. A stale PMK/BSS owner cannot
   cross that doorbell.
6. Stop, detach and sleep/reset close callbacks, cancel the owner, scrub
   transient credentials and drain the engine task before private locks are
   released.

Feature `0x41` is now published through a stable HAL capability only when the
attached backend has the complete driver-resident path. The diagnostic direct
SAE stimulus remains IWN-only.

## Deliberate scope

Admission remains limited to the five already audited new-format API-68 MFP
configurations (AX211 GF normal/long, AX210 TY, AX411 GF4 normal/long). Other
IWX firmware/config objects stay closed. This layer implements initial pure
SAE/transition association. Targeted multi-AP SAE credential replay hooks are
still null and are the next reconnect/roam layer.

The laboratory VM has physical IWN, not an IWX device. Its whole-kext runtime
can prove IWN regression safety and sleep/wake recovery, but it cannot be
reported as physical AX210/AX211/AX411 on-air evidence. Physical IWX validation
therefore remains required before calling this hardware-proven.
