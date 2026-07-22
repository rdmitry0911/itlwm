# Agent handoff — Tahoe PMF/BIP runtime gate

Date: 2026-07-21

## Exact repository state

- Workspace: `/home/dima/Projects/aiam/scratch/sae-runtime-stage-20260721`
- Code checkpoint before this documentation commit:
  `e909f3d06c4b100f6e314c884363b9f8617a1c11`
- The handoff documentation itself is a later docs-only commit.  Begin by
  checking the actual `git rev-parse HEAD` and `git status --short`.
- The worktree is intentionally detached.  The same commit is pushed to
  `itlwm/tahoe-ax211-sae-pmf-20260719`; push future commits explicitly with:

  ```sh
  git push itlwm HEAD:tahoe-ax211-sae-pmf-20260719
  ```

- Do not create a release for this checkpoint.  `v2.4.0-alpha` remains the
  sole mutable asset until a whole version-level feature layer has passed its
  exact-candidate runtime evidence.

## Persistent prioritization rule (user directive, 2026-07-22)

When choosing the next layer that reduces surface mismatch with the reference,
first choose the eligible layer that unblocks the most frequently used
user-facing function. Do not prefer a lower-frequency evidence, diagnostic,
or convenience layer merely because it is smaller or easier to verify.

Safety prerequisites and explicit external blockers still apply. A lower-level
layer may precede the highest-frequency user path only when it is an actual
hard prerequisite for that path or prevents a false success claim about it;
the candidate report must state that dependency explicitly. For the current
project this means a real saved-profile association/WPA3 path and its
driver/Agent bridge outrank further standalone PMF diagnostic polish once the
in-flight bounded verification work is complete.

## Commits delivered in this checkpoint

1. `efd1c7a test(tahoe): build trace producers before auditing them`
2. `459ef19 net80211: harden BIP IGTK publication and PMF handoff`
3. `269096e test(sae): add dormant group19 relay FSM foundation`
4. `e909f3d test(runtime): record A2DF control and mbedTLS SAE KAT`

The BIP/PMF commit makes IGTK slots 4/5 safe to publish, preserves old-slot
RX lifetime during a rekey, restricts IGTK use to multicast management frames,
and fences HAL/user GTK paths away from those slots.  It is not yet a physical
PMF-required association claim.

The group-19 mbedTLS material is a pinned OpenWrt-compatible **test intake**.
Neither the AirportItlwm kext nor AirportItlwmAgent links mbedTLS yet.

## Verified evidence

### Exact clean source build

`bash scripts/run_tahoe_sae_quarantine_layer.sh` passed for source identity
`e909f3d` in the pinned Tahoe guest.  It covered static/model contracts,
AirportItlwm Tahoe build, all 959 BootKC symbol resolutions, trace-producer
audit, Agent build, and RegDiag build.

Its isolated guest directory was:

```text
/tmp/aiam-tahoe-sae-layer-gate.FnqOMP
```

This gate did **not** install, load, publish, release, or reboot a kext.

### Private AuxKC admission

The clean candidate passed a private-only AuxKC preflight:

```text
candidate UUID: 823A3EBC-C8B9-3327-945B-BA397B4208B8
candidate SHA-256: cb987e25f7bf5497fc9b4dae12878e50f2e690c46c85278ac3e84e4e77118b05
private admission: PASS
canonical mutation: none
exact AuxKC members: 5
```

Guest-private preflight material was placed below
`/private/tmp/aiam-bip-pmf-e909f3d-auxkc.FR7fQp/`.  It is disposable evidence,
not a release artifact.  The canonical kext and AuxKC were unchanged.

### Existing control and crypto evidence

- Four A2DF radio OFF/ON cycles of saved control candidate `034BABDD` passed
  with 20/20 bounded lab pings and preserved management/default-route
  invariants.  See `analysis/TAHOE_A2DF_CONTROL_034BABDD_2026-07-21.md`.
  This is a control data-plane baseline only, not PMF/BIP/SAE proof.
- The OpenWrt mbedTLS SAE group-19 KAT passed in the Tahoe guest.  See
  `analysis/TAHOE_OPENWRT_MBEDTLS_GROUP19_KAT_2026-07-21.md`.  It is not kext,
  Agent, PMF, association, or traffic evidence.

## Current hard boundary

Do **not** activate the new candidate merely to collect ordinary WPA2 traffic.
The project currently lacks an IWX PMF/BIP runtime evaluator capable of
attributing an initial IGTK install and a slot-4/5 rekey to the candidate.

Existing `run_tahoe_sae_lab_profiles.sh` is intentionally insufficient:

- it needs three pre-existing profiles;
- it treats IWX as ordered-trace unsupported;
- it observes only the default route;
- it has no IGTK publication/rekey witness.

`run_tahoe_lab_radio_gates_no_route_mutation.sh` is still required as a
recovery/data-plane gate after activation, but it is not PMF/BIP proof.

## Laboratory state and prohibitions

- Only the pinned QEMU Tahoe guest may be changed or rebooted.  Do not touch
  physical `10.90.10.22`.
- Do not reboot the host.  A guest reboot is permitted only after a clean
  candidate, private AuxKC preflight, an explicit transactional activation,
  and a disposable-overlay plan exist.
- Never direct-load or unload AirportItlwm.
- The live 5 GHz lab AP is 80 MHz on channel 153 and uses the optional-PMF
  transition configuration
  `/home/dima/Projects/ax211-5g-ap/hostapd-5g.conf`.
- A staged PMF-required PSK configuration exists at
  `/home/dima/Projects/ax211-5g-ap/hostapd-5g-wpa2-pmf.conf`, but it is not
  live.  It may be used only through a new one-at-a-time switchover helper
  with mandatory rollback.  Do not reuse helpers that mutate host IP, NAT, or
  routes.
- Before any join-capable test, use saved-profile/Keychain-only preflight.
  Do not put passphrases into scripts, logs, process arguments, commits, or
  evidence.
- Preserve the guest default route on `en0`; `en1` may have only the direct
  laboratory route.  Do not use route, address, DHCP, raw-scan, or arbitrary
  join helpers outside a bounded runner.

## Next implementation, in order

1. Read `analysis/TAHOE_PMF_RUNTIME_GATE_PLAN_2026-07-21.md`.
2. Implement the new safe categorical IWX PMF/BIP trace/evaluator and its
   complete deterministic fixture matrix before any candidate activation.
3. Add a credential-safe PMF-required PSK runner that enforces the A2DF
   route/address invariants and records only sanitized attestations.
4. Build and test the new layer in the isolated Tahoe guest; commit and push
   it.  Still do not release.
5. Create a fresh disposable QEMU overlay, then run private preflight,
   transactional AuxKC activation, guest-only reboot, and exact loaded-identity
   binding.
6. Run A2DF recovery first.  Only then use the controlled PMF-required AP
   switchover, initial PMF association/traffic, short group-rekey observation,
   rollback the AP, and run A2DF recovery again.

Any missing saved profile, disposable-overlay mechanism, PMF trace integrity,
or rollback witness is an explicit prerequisite failure—not a reason to infer
PMF/BIP success from a generic association.
