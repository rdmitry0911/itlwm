# Tahoe IWX new-format API-68 MFP runtime gate — 2026-08-01

## User-visible layer

The existing IWX PTK/GTK/IGTK transaction owner was unreachable on the
checked-in API-68 firmware images.  Those images store the complete new-format
version as `0x00000044`; the legacy `IWX_UCODE_API()` extraction reads bits
8..15 and therefore returns zero.  The old runtime predicate compared that
zero to 68 and kept `IEEE80211_C_MFP` disabled even when the firmware exposed
MFP and multi-queue RX.

The loader now retains the validated complete header value separately from
the legacy API byte.  MFP admission requires all of the following:

- complete firmware header version 68;
- `IWX_UCODE_TLV_API_NEW_VERSION`;
- the firmware MFP flag and `MULTI_QUEUE_RX_SUPPORT` capability;
- the existing q0, task, selected-BSS, rollback, and IGTK-v2 owners; and
- one of exactly five audited configuration objects: AX211 GF (normal/long),
  AX210 TY, or AX411 GF4 (normal/long).

SO JF/HR, API-63 SoSnj, AX200/AX201, and future unlisted configurations stay
closed.  Admission is neither filename-derived nor family-wide.

## Reference boundary

The Tahoe 25C56 decompile continues to place WPA3 capability and PMK handling
below WCL: `AppleBCMWLANCore::checkForWPA3SAESupport()` queries lower feature
`0x41`, and `AppleBCMWLANJoinAdapter::programPMK()` programs the lower owner
through selector `0x10c`.  That is why this change repairs the real lower MFP
owner instead of publishing a synthetic upper capability.

The carrier choice itself is Intel-firmware-specific.  The three pinned raw
firmware assets advertise the same new-format API-68, MFP, and MQ-RX facts;
the audited iwlwifi donor selects the 0x34-byte `MGMT_MCAST_KEY` v2 carrier on
the MQ-RX path.  The contract hashes and parses every selected image and
rejects the legacy-byte predicate.

## Verification boundary

The source/model contract proves the corrected loader field, exact whitelist,
firmware prerequisites, and preservation of the existing asynchronous PMF
transaction fences.  A hardware runtime claim still requires a selected IWX
adapter, PMF-required AP, initial traffic, group rekey, and sleep/wake.  It is
not inferred from the contract or from the working IWN laboratory adapter.
