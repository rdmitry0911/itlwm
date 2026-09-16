# Tahoe 802.11ax (HE) + 6E re-enablement — decompile-grounded design (2026-09-16)

Prove-before-implement design for closing the last identified contact-surface code
non-identity (itlwm caps 11ac; the Apple reference does 11ax per fw cap). Branch
`tahoe-iwn-sae-bridge-runtime`. Reference logic from `~/Projects/ghidra_output/
AppleBCMWLANCoreMac_decompiled.c` on 10.7.6.112 (logic-only; addresses build-specific).

## Headline: HE is DORMANT, not missing
Since commit 696a1215 the net80211 HE stack was rewritten and substantially completed.
The HE datapath is present and only gated on `IEEE80211_NODE_HE` ← `IEEE80211_F_HEON`.
Already present + correct today (do NOT re-implement):
- `ic_modecaps |= 11AX` unconditional — `ieee80211.c:827`
- HE cap IE parse (probe+assoc), corrected `+3` ext-offset — `ieee80211_input.c:2107, 3349`
- HE operation parse → `ni_he_oper_params`/`ni_he_optional` — `ieee80211_node.c:3615-3625`
- Operation-element-driven HE width negotiation — `ieee80211_proto.c:5178-5302`
- HE cap IE emission in assoc-req (HEON-gated), builder `ieee80211_add_hecaps` — `ieee80211_output.c:1848-1849,1382`
- HE TLC config flags (STBC/LDPC/DCM), gated on NODE_HE — `ItlIwx.cpp:17708-17750`
  **(the memory's claim that this is still commented is WRONG — it was re-enabled)**
- HE MCS→rate — `iwx_rs_fw_he_set_enabled_rates ItlIwx.cpp:17828-17869`
- HE AMPDU agg size — `ItlIwx.cpp:13201-13232`
- active-phy-mode can emit kMode11AX=0x100 (NODE_HE-gated) — `TahoePhyModeContracts.hpp:60-77`
- supported-phy-mode already reports 11AX on an HE card — `AirportSTAIOCTL.cpp:124-151`

## Root cause of the 696a1215 "HE regressed 160MHz" incident
`git show 696a1215` did two things: (a) FIXED real parse bugs shipped 2 days earlier in
002c1ca5 (ext-ID read `frm[0]`→`frm[2]`; payload `hecap+2`→`hecap+3`; guarded
`ieee80211_he_negotiate` with `hecap!=NULL && heopmode!=NULL`; added probe-resp parse);
and (b) as a safety net, disabled HEON (`ItlIwx.cpp:8543`, still commented).
- Pre-fix: HEON on + the ext-ID parse bug → node flagged NODE_HE with EMPTY HE caps →
  `iwx_rs_init` picked the HE rate branch over VHT → garbage HE MCS table replaced the
  good VHT-160 table → link degraded. **That parse bug is now FIXED.**
- **Still-live smoking gun:** `iwx_setup_he_rates` (`ItlIwx.cpp:8538-8626`) sets
  `mac_cap_info[0..5]`, `phy_cap_info[1..9]` but **never `phy_cap_info[0]`** (HE Channel
  Width Set; `ieee80211.h:1788-1794`). Zero ⇒ STA advertises 20MHz-only HE ⇒ an HE AP
  grants HE-20 ⇒ STA drops VHT-160→HE-20. `ieee80211_add_hecaps` copies `ic_he_cap_elem`
  verbatim into assoc-req. This is THE block-level root cause; a blind re-enable repeats it.
- Reference cross-check: 160MHz is an ORTHOGONAL axis in the reference
  (`checkFor160MHzSupport=featureFlagIsBitSet(0x79)`, distinct from 11ax flag 0x43),
  resolved as `bw_cap ∩ AP-chanspec` independent of VHT-vs-HE. itlwm analog: advertise 160
  in BOTH the VHT cap (already) AND HE `phy_cap_info[0]`.

## Re-enablement plan (block-by-block)
- **Block A (REQUIRED, first):** `ItlIwx.cpp:~8545` set `phy_cap_info[0] = 40MHZ_IN_2G |
  40MHZ_80MHZ_IN_5G | 160MHZ_IN_5G` (2-stream AX211; no 80+80). Borrow exact bytes from
  linux iwlwifi `mvm/mac80211.c` `iwl_he_capa` 5GHz table. Must match the VHT-160 already advertised.
- **Block B (HEON gate):** enable `IEEE80211_F_HEON` ONLY when `sc->sc_nvm.sku_cap_11ax_enable`
  (same gate already guarding `iwx_setup_he_rates` at `ItlIwx.cpp:19388,23376`), placed inside
  `iwx_setup_he_rates` so iwn/iwm never get HEON. Mirrors reference `checkFor11axEnabled` =
  supported ∧ ¬NVM-disabled. NOTE two HEON sources exist (commented driver line 8543 +
  live media path `ieee80211.c:1453`) — reconcile at runtime (read `ic_flags` on the AX211).
- **Block C (crux, anti-regression):** in `ieee80211_he_negotiate` (`proto.c:5267-5299`) the
  switch OVERWRITES `ni_chw`. Change to `max(existing, HE-derived)` — never narrow the
  VHT-negotiated width. No-op for well-formed HE APs; guard against malformed/asymmetric ones.
- **Blocks D/E (no change):** HE TLC/rate + active-phy-mode are live, activate once NODE_HE set.
- **Block F (low-pri gap):** `ieee80211_get_probe_req` (`output.c:1496-1530`) emits HT+VHT but
  not HE; optionally add HEON-gated `ieee80211_add_hecaps`. Association works without it.
- **Block G (6E, deferred):** `support_6e=0` hardcoded (`Skywalk:11017`); needs 6GHz channel
  population + `ieee80211_he_6ghz_oper` consumption + a 6E AP. Keep 0 (no crutch) until then.

## Verification
- **Lab-verifiable NOW (the critical no-regression proof):** the lab AP advertises no HE ⇒
  `hecap==NULL` ⇒ `ieee80211_he_negotiate` skipped ⇒ NODE_HE stays clear ⇒ HE datapath dormant
  EVEN WITH HEON ON. After Blocks A–C, associate the lab VHT-160 AP and confirm: HEON set;
  NODE_HE clear; `ni_chw==160` unchanged; TLC mode==VHT; active_phy==11AC; throughput within
  noise; association still succeeds (STA now emits HE cap IE in assoc-req — proves it doesn't
  break VHT/legacy APs); iwn/iwm HEON stays clear. Proves the re-enable is INERT on the working path.
- **Needs an 802.11ax AP (cannot verify in current lab):** actual HE association, NODE_HE path,
  HE MCS/rate table, the Block-C clamp under a real HE-160 AP, HE throughput. + all of 6E.
- **Minimum hardware:** one Wi-Fi 6 (802.11ax) AP with 160MHz in 5GHz (retail, or a Linux box
  with a 2nd AX radio in hostapd `ieee80211ax=1`). For 6E: a Wi-Fi 6E AP (mind lab Country=ZW).

## Risks (all mitigated by the plan)
1. 20MHz-only HE → regress 160 — Block A. 2. HE narrows working VHT-160 — Block C max-clamp.
3. Ambiguous HEON source — centralize on sku_cap_11ax_enable, confirm at runtime.
4. HE IE in assoc-req to all APs — covered by the lab VHT no-regression association run.
5. HEON on non-HE card (iwn/iwm) — gate inside iwx_setup_he_rates. 6. 6E fabrication — keep support_6e=0.

## Status
DESIGN ONLY (per user deferral of HE/6E + no HE AP for HE-active verification). Blocks A–C
are reference-grounded + the 696a1215 root cause is fixed; the enable is de-risked and the
no-regression is lab-verifiable. To close: user go-ahead + one external 802.11ax AP.
(design: subagent a4058a00, 2026-09-16.)
