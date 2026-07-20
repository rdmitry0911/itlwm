# Tahoe 25C56 AuxKC boot-thread-call compatibility boundary

## Scope

This record covers only boot-time admission of the Tahoe SAE/PMF candidate on
the pinned laboratory guest. It does not claim that SAE, PMF/IGTK, or the
AirportItlwm data path is implemented or equivalent to an Apple driver.

## Observed blocker

The exact SAE-layer candidate at `62dbfac` was rejected before AuxKC
materialization on the macOS 26.2 / build `25C56` laboratory guest because its
Mach-O imported `_thread_call_cancel_wait`. Both the temporary collection
creation attempt and the bounded load admission probe reported that no eligible
kext export could bind that symbol. No replacement collection was created and
the canonical collection was not swapped. The retained evidence is
`/home/dima/Projects/aiam/runtime-captures/itlwm-sae-layer-auxkc-abi-20260720/assessment.md`.

This is an auxiliary-kext admission boundary, not evidence that the symbol is
absent from every raw image inside BootKC: the project build script's ordinary
`nm` comparison is weaker than `kmutil` eligibility. A raw symbol listing must
not be treated as an AuxKC admission proof.

## Root cause and required contract

`AirportItlwm::stopTahoeBootThreadCallAndDrain()` used the private synchronous
cancel interface to wait for a single boot callback before freeing the
`thread_call_t`. Replacing it with only `thread_call_cancel()` followed by
`thread_call_free()` is unsafe: a false cancellation result means that the
callback may already be in the dispatch gap or running while its raw `this`
parameter is still in use.

The compatible contract uses only the public `thread_call_cancel`,
`thread_call_free`, `IOLockSleep`, and `IOLockWakeup` APIs:

1. Under `fTahoeBootCallLock`, the one-shot scheduler sets both the permanent
   submit latch and the outstanding-callback latch before its sole
   `thread_call_enter`.
2. The scheduler retains the controller before submission. Exactly one terminal
   path drops that retain: the callback's literal final action, or a successful
   pending cancellation after the call is freed and unpublished.
3. Teardown first claims `fTahoeBootStopping`. A second drain returns before it
   can free the same call.
4. If `thread_call_cancel` succeeds, the outstanding latch is cleared under the
   same lock. If it fails, teardown waits in a predicate loop until the callback
   clears and wakes that latch after all gate work is finished.
5. The callback records `current_thread()` as its owner before its first
   liveness decision. A direct callback self-drain fails closed rather than
   sleeping forever on its own outstanding latch.
6. `free()` can release the callback lock only after the call pointer is null,
   no callback is outstanding, no schedule retain remains, and no callback owner
   is published.

The source is intentionally one-shot: no resubmission and no second
`thread_call_t` are allowed on this state record. A future rearming design must
use a generation/call identity rather than reuse this latch.

## Primary reference analogue

Apple XNU's bridge delayed-call implementation provides the same shape:

- `bsd/net/if_bridge.c` sets `BDCF_OUTSTANDING` before scheduling;
- cancellation clears it only when dequeue succeeds, otherwise sleeps under
  the same bridge lock until the callback clears and wakes it;
- cleanup calls `thread_call_free` only after the outstanding state is gone.

The local XNU checkout records this at
`/home/dima/Projects/apple-oss-distributions/xnu/bsd/net/if_bridge.c:4287`.
The corresponding XNU `thread_call` source documents that a false public
cancel may be in flight and that normal dynamically allocated calls retain
runtime storage while completing; it does not pin the caller's raw callback
parameter. The explicit controller retain above supplies that missing object
lifetime boundary.

## Changed surface

- `AirportItlwm/AirportItlwmV2.cpp` and `.hpp`: explicit outstanding/owner/
  retain lifecycle for the boot callback, with no private synchronous-cancel
  import.
- `scripts/build_tahoe.sh`: rejects a Tahoe candidate importing
  `_thread_call_cancel_wait` before the weaker raw BootKC symbol comparison.
- `scripts/test_tahoe_boot_thread_call_auxkc_contract.sh`: source-contract plus
  deterministic interleaving model for pending cancellation, dispatch/running
  completion, one-shot completion, self-drain, and double-drain cases.
- `scripts/tahoe_auxkc_admission_preflight.sh` and its contract test: a
  fresh-private-path exact-five-member materialization check. After it captures
  canonical before-witnesses, its exit path always records the after-witnesses,
  including a failed materialization, so an unchanged canonical bundle/AuxKC
  is proved on both result paths. It is link/materialization evidence only,
  never a direct-load or next-boot action.
- `docs/tahoe_lineage_build_reproducibility.md`: separates that private
  preflight from the separately approved full-repository activation envelope.
- Existing q0/lifecycle and SAE build gates assert the new ordering.

## Verification performed before runtime

- shell syntax checks pass for the affected scripts;
- `scripts/test_tahoe_boot_thread_call_auxkc_contract.sh` passes;
- `scripts/test_iwx_q0_serialization_contract.sh` passes;
- `scripts/test_tahoe_sae_quarantine_contract.sh` passes;
- `scripts/test_tahoe_auxkc_admission_preflight_contract.sh` passes;
- the boot-thread-call contract also runs on the pinned guest's Python 3.9.6;
  its model intentionally avoids Python 3.10-only PEP 604 union annotations;
- a clean temporary worktree passed the static SAE/PMF gate;
- a separately clean hardened-source snapshot passed the full isolated
  build-only guest gate: it built the kext, Agent, and RegDiag; its candidate
  Mach-O had no `_thread_call_cancel_wait` import; and all 958 ordinary
  undefined symbols resolved against the selected BootKC. This proves neither
  actual AuxKC admission nor runtime behavior.

No kext was installed, loaded, released, or rebooted while collecting this
evidence. Only a separate approved **private-path** AuxKC materialization can
prove that the rejected private import is gone from the actual candidate
link/materialization path. It must pass the candidate plus the four retained
auxiliary bundles explicitly, with no repository scan that could select the
canonical AirportItlwm bundle.

The historical `capture_tahoe_auxkc_*load*` workflows are not that preflight:
they stage the canonical bundle and invoke direct load and/or reboot. A change
to `/Library/Extensions/AirportItlwm.kext` can itself wake `kernelmanagerd`
collection work. The project-owned private preflight instead builds its output
under a private directory, verifies the original bundle and canonical AuxKC
hashes/member inventory before and after, and makes no canonical-path write.

## CR-242 postflight finding and CR-243 FIX_CANDIDATE

- anomaly id: `TAHOE-AUXKC-PREFLIGHT-ORDERING-20260720`
- status before this correction: `CONFIRMED_DEVIATION`
- symptom: the one approved CR-242 private materialization completed
  successfully, but the harness returned `FAIL` despite unchanged canonical
  state.
- runtime evidence: the one permitted guest attempt is retained at
  `runtime-captures/itlwm-cr242-private-auxkc-20260720T104147Z/`. It built
  candidate UUID `7F663657-0080-3146-890E-003206DDB34B` with Mach-O SHA-256
  `2027c71a38d30df467cd4ec6ce4f48cac6007f28bbd53bdd5f74a5f15f87442a`.
  `kmutil create`, private inspect, and private-plus-BootKC inspect all
  returned zero. The canonical AirportItlwm SHA-256 remained
  `c096a4ab97358a9ac75d493fa5c1d9353dcb733864622f03dc351d21f2f567e7`;
  canonical AuxKC SHA-256 remained
  `dd181435a0fb02cf96052308c3540929b86f090367dcc54661ca7711e4ceaf96`.
- exact divergence point: `record_canonical_postflight()` compared raw
  `kmutil inspect` member TSV files with `cmp -s`.
- confirmed root cause: on the same unchanged canonical AuxKC, the two
  `kmutil inspect` readings emitted the same five complete rows in different
  display orders. The raw-byte comparison interpreted that permutation as a
  canonical mutation. The original extractor also discarded malformed and
  non-`com.*` rows before validation, so it could not prove that its retained
  inventory was a complete exact multiset.

### FIX_CANDIDATE — `TAHOE-AUXKC-PREFLIGHT-ORDERING-20260720`

- expected system behavior: a private-only admission harness must preserve raw
  `kmutil inspect` evidence, reject missing, duplicate, malformed, and unknown
  member rows, and treat only a semantic change in the complete validated
  member multiset as a canonical-state difference. A display-order permutation
  must not be called a mutation.
- actual behavior: raw TSV byte order was used as the mutation predicate.
- exact semantic mismatch removed: raw presentation order was treated as the
  system-state identity instead of the complete identity/version/path/UUID
  multiset that the observer actually reports.
- justification path: `SYSTEM_CONTRACT_FIX`.
- system-facing touchpoints and contracts:
  1. canonical AirportItlwm and AuxKC remain read-only SHA witnesses; their
     before/after bytes must still match exactly;
  2. `kmutil inspect` is a read-only, order-unspecified rendering, so raw rows
     are retained as evidence but compared only after exact-multiset validation
     and deterministic full-row ordering;
  3. private AuxKC membership must contain exactly the required five bundles,
     with the private candidate UUID and unchanged four non-Airport rows;
  4. the correction writes only additional files below the already fresh
     private evidence directory and never adds a load, install, reboot, radio,
     network, or external-host operation.
- why no relevant touchpoint is omitted: the private helper's only observable
  decisions are candidate construction/inspection, canonical witnesses, and
  evidence verdicts; activation is explicitly outside this scope.
- why no extra system-visible side effect is added: sorting and validation run
  only over already captured local TSV evidence. They do not invoke `kmutil`,
  change the collection shape, or affect candidate bytes.
- files/functions to modify:
  - `scripts/tahoe_auxkc_admission_preflight.sh`:
    raw-member extraction, exact multiset validation, normalized comparison,
    and private-member validation;
  - `scripts/test_tahoe_auxkc_admission_preflight_contract.sh`: executable
    permutation/change/missing/duplicate/unknown/malformed fixtures;
  - this analysis record and the Tahoe reproducibility documentation.
- forbidden alternatives considered and rejected:
  - accept the raw order difference as a real mutation;
  - suppress canonical postflight validation;
  - deduplicate with a set-style sort that could hide a duplicate;
  - retry the consumed CR-242 guest attempt;
  - install, load, reboot, or otherwise activate the private candidate.
- verification plan:
  1. shell parse, local production-function fixtures, and all prior static
     Tahoe contracts;
  2. immutable CR-243 Stage-1 review of the exact source-only diff;
  3. only if approved, one new private candidate build and one fresh private
     AuxKC preflight on the lab guest;
  4. Stage-2 review before any commit, push, release, activation, or SAE work.

The CR-242 attempt is consumed evidence and is not retried. Its Stage-2 result
is `PRIVATE_MATERIALIZATION_SUBSTANCE=PASS` but
`NOT_APPROVED_FOR_COMMIT — HARNESS_FALSE_POSITIVE_ORDERING`; it does not
authorize an activation or next-boot experiment.

### CR-243 rejection and FIX_CANDIDATE — exact member-row schema

- anomaly id: `TAHOE-AUXKC-PREFLIGHT-MEMBER-SCHEMA-20260720`
- status: `CONFIRMED_DEVIATION`
- first visible manifestation: independent CR-243 Stage-1 review, retained in
  `commit-approval/decisions/COMMIT_DECISION_CR-243.md`.
- symptom: the new multiset validator rejected too-short rows but accepted a
  required four-field row with a fifth tab-separated field. It could therefore
  label an unchanged pair of malformed manifests as valid.
- direct evidence: the production extracted validator returned `status=PASS`
  for the five required rows when the AirportItlwm record was
  `identity<TAB>version<TAB>path<TAB>(UUID)<TAB>extra-field`.
- expected system contract: the observed 25C56 `Extension Information` schema
  is exact, not minimum-width: every required row has exactly four
  tab-separated fields. AirportItlwm, RemoteVirtualInterface, HighPointIOP,
  and HighPointRR use nonempty `identity`, `version`, absolute bundle `path`,
  and parenthesized UUID; AppleMobileDevice is the observed legitimate
  no-UUID exception with the same first three nonempty fields and an empty
  fourth field. No other field count, empty required field, unknown identity,
  duplicate, or missing identity is valid.
- root cause: CR-243 used `NF < 3` as a generic malformed predicate instead of
  encoding the exact accepted schema before normalization.

#### FIX_CANDIDATE — `TAHOE-AUXKC-PREFLIGHT-MEMBER-SCHEMA-20260720`

- justification path: `SYSTEM_CONTRACT_FIX`.
- exact semantic correction: validate the per-identity field count and every
  required identity/version/path value before sorting; require a parenthesized
  UUID only for the four identities that supplied one in the retained 25C56
  canonical/private evidence.
- relevant touchpoints:
  1. read-only `kmutil inspect` row rendering;
  2. local raw/validation/normalized evidence files under the fresh private
     output;
  3. existing canonical SHA and exact-five-member witnesses.
- why no extra observable side effect is added: this only changes a local
  evidence verdict before `sort`; it makes no additional system call and does
  not change the private collection shape, candidate, canonical paths, radio,
  or network.
- forbidden alternatives: accepting an extra field as harmless, silently
  truncating it, dynamically guessing a schema, deduplicating rows, retrying
  CR-242, or activating the candidate.
- verification plan:
  1. production-function fixtures for valid canonical/private rows, raw-order
     permutation, changed row, missing, duplicate, unknown, too-short,
     extra-field, empty-version, empty-path, and malformed UUID rows;
  2. complete static Tahoe contracts and clean exact-diff layer gate;
  3. a new immutable Stage-1 review; only then one new private build/preflight.

## SAE relationship

This compatibility fix is a prerequisite for the existing two-epoch SAE
diagnostic batch, not a SAE functional fix. Once a compatible candidate is
materialized and booted, the batch must capture pure SAE and transition/PSK
epochs together. It must continue to distinguish the current intentional pure
SAE quarantine from PMF/IGTK, JoinAdapter RSNXE, WNM/BTM/MBO, profile, and
raw-scan-panic questions.
