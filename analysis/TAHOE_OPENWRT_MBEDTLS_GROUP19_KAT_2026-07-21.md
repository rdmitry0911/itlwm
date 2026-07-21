# Tahoe OpenWrt/mbedTLS group-19 KAT — 2026-07-21

## Executed test-only result

The pinned Tahoe `25C56` QEMU guest executed the exact locked
OpenWrt-patched hostap/mbedTLS group-19 KAT from an isolated temporary source
copy.  The source copy was removed after launch; the KAT work directory was
retained temporarily on the guest only for inspection.

Command on the guest:

```sh
ITLWM_SAE_KAT_JOBS=2 \
  bash scripts/run_tahoe_openwrt_mbedtls_sae_group19_kat.sh \
  --workdir /private/tmp/itlwm-openwrt-mbedtls-sae-kat-work-20260721-r1
```

Both source-verified vectors passed:

- group-19 HnP Commit/KCK/PMK/PMKID: `PASS`;
- group-19 H2E PT/PWE x/y: `PASS`.

The resulting test executable SHA-256 was
`7d925d0fd6d4e0a9e51b2b70127b3a9927fb3a48b95df308638ee5430c04f84d`.
`otool -L` reported only `/usr/lib/libSystem.B.dylib`; representative mbedTLS
symbols were present, so `libmbedcrypto` was statically linked only into this
temporary test executable.

## Scope boundary

This is an executed crypto KAT for the test intake described in
`docs/TAHOE_OPENWRT_MBEDTLS_SAE_GROUP19_INTAKE.md`.  It does not link mbedTLS
into AirportItlwm or AirportItlwmAgent, and it does not prove SAE association,
PMF/IGTK, AX211 behavior, or any Wi-Fi data-plane result.  No kext or Agent
was installed, loaded, or unloaded; no reboot, radio/network mutation, or
physical `10.90.10.22` access occurred.

The local-only guest evidence directory is
`/private/tmp/itlwm-openwrt-mbedtls-sae-kat-work-20260721-r1/group19-kat/`.
It is intentionally not committed and contains no association credentials or
wireless identity record.
