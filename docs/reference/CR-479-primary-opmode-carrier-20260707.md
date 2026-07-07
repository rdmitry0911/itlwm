# CR-479 Primary OP_MODE Carrier

Reference body: `AppleBCMWLANCore::getOP_MODE(apple80211_opmode_data*)`
at `0xffffff80015e564a`.

Source material on `10.7.6.112`:

- `~/Projects/ghidra_output/itlwm_full_sta_parity_decomp_20260514T160154/07_xrefs/BootKC_full_STA/0982_0xffffff80015e564a___ZN16AppleBCMWLANCore10getOP_MODEEP22apple80211_opmode_data.asm.txt`
- `~/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke2_20260516T1252/09_static_slices/BootKC_memory_safe/01043_ffffff80015e564a___ZN16AppleBCMWLANCore10getOP_MODEEP22apple80211_opmode_data.pcode.tsv`

Recovered visible contract:

- `NULL` output returns raw `0x16`.
- The primary carrier starts with one qword store of `1`, which is
  `version = 1` and `op_mode = 0`.
- APSTA owner state may OR mode bits into caller `+0x04`.
- The hidden SIB/proximity owner path may OR additional mode bits into caller
  `+0x04`.
- Core-private byte `+0x4779` bit 0 ORs `APPLE80211_M_MONITOR` (`0x10`) into
  caller `+0x04`.
- The primary body does not synthesize `APPLE80211_M_STA` (`0x01`).

Local closure:

- Tahoe and legacy primary STA `getOP_MODE` now share
  `TahoeOpModeContracts::initializePrimaryCarrier(...)`.
- APSTA-specific `getOP_MODE` remains separate and continues to publish
  SoftAP mode `8` only through the APSTA owner path.
