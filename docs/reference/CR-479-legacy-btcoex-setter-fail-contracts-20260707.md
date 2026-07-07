# CR-479 Legacy BT Coex Setter Fail Contracts

Date: 2026-07-07

Reference inputs:

- `10.7.6.112:~/Projects/ghidra_output/itlwm_btcoex_wrappers_20260706/08_0xffffff8002208613___ZL16setBT_COEX_FLAGSP17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req.c`
- `10.7.6.112:~/Projects/ghidra_output/itlwm_btcoex_ranges_20260706/range_0xffffff80021e82c0_0xffffff80021e8610.asm.txt`
- `10.7.6.112:~/Projects/ghidra_output/itlwm_btcoex_wrappers_helper_20260706/01_0xffffff80009ff310_FUN_ffffff80009ff310.c`

Recovered contracts:

- `setBT_COEX_FLAGS` at `0xffffff8002208613` returns raw `6`.
- `setBT_POWER` requested at `0xffffff80021e85c0` lands in the adjacent fixed
  `0xe082280e` wrapper stub in the recovered setter range.
- The `getBT_COEX_FLAGS` and `getBT_POWER` bodies are list-backed wrapper
  paths through helper `0xffffff80009ff310`; the recovered helper is only a
  list lookup, not the carrier producer. Getter-side carrier semantics are
  therefore intentionally not synthesized here.

Local contract:

- Route only the set side of `APPLE80211_IOC_BT_COEX_FLAGS` and
  `APPLE80211_IOC_BT_POWER` through the Tahoe Skywalk bridge.
- Preserve the exact raw return codes above.
- Leave getter-side dispatch to the inherited IO80211/WCL path until the
  concrete list-backed producer is recovered.
