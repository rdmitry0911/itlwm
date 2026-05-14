# Analysis report — 2026-05-14i — IEEE80211_STA_ONLY opt-out build variant

## Layer scope

This Stage 1 layer adds a project-supported opt-out build
variant to `scripts/build_tahoe.sh` so that the AP-side of
`itl80211/openbsd/net80211/ieee80211_node.c` and the
producer-bridge publish call sites compile without modifying
the Xcode project file or the source tree. The header-level
enabler `IEEE80211_OPT_OUT_STA_ONLY` is already present at
`itl80211/openbsd/net80211/ieee80211_var.h:63-77` (committed
in `a49b284 net80211: introduce AP station-event producer
bridge and STA_ONLY opt-out`); what is missing today is a
build-script entry point that defines the macro through
`GCC_PREPROCESSOR_DEFINITIONS` so a contributor can run a
clean opt-out build alongside the existing default STA-only
build.

The change has three parts, all confined to
`scripts/build_tahoe.sh`:

1. A new `--opt-out` command-line flag and equivalent
   `BUILD_OPT_OUT_STA_ONLY=1` environment variable select the
   opt-out variant. Without either, the script behaves
   exactly as before (default STA-only).
2. The opt-out variant uses a separate
   `DerivedData-optout/` Xcode derived-data root and a
   separate `Build/Debug/Tahoe-OptOut/AirportItlwm.kext`
   staged output root, so the two variants never overwrite
   each other and a developer can keep both kexts staged at
   once for diffing or comparison runs.
3. The opt-out variant appends `IEEE80211_OPT_OUT_STA_ONLY`
   to `GCC_PREPROCESSOR_DEFINITIONS`, which the existing
   header-level enabler resolves into `#undef
   IEEE80211_STA_ONLY` for every translation unit that
   includes `ieee80211_var.h`.

The change is structurally inert at runtime. It does not
modify a single C/C++/header source file, does not touch the
Xcode project file, and does not change the default Tahoe
build path in any observable way. The default invocation
`./scripts/build_tahoe.sh [BOOTKC_PATH]` produces the same
`Build/Debug/Tahoe/AirportItlwm.kext` it did before, with
the same Mach-O contents and the same `LC_UUID`/sha256
identity as the build immediately before this layer.

## Why this layer exists as its own Stage 1

The prior Stage 1 layer `2026-05-14h` (CR-477) bound the
host APSTA owner as the single net80211 station-event
consumer and explicitly named "IEEE80211_STA_ONLY opt-out"
as the next coherent layer in dependency order:

> `IEEE80211_STA_ONLY` opt-out: define
> `IEEE80211_OPT_OUT_STA_ONLY` in the build (and reconcile
> any newly exposed BootKC undefined symbols), so the AP
> side of `ieee80211_node.c` and the producer-bridge publish
> call sites compile. With the iwx/iwm preflight guards from
> CR-476 already in place and this consumer binding already
> in place, the opt-out becomes a self-contained build-flag
> flip whose runtime claim is "default-STA boot still safe;
> no AP-only path triggered at runtime because role-7 owner
> is not allocated".

The header enabler, the producer-bridge ABI, the consumer
binding, and the iwx/iwm HOSTAP-panic preflight guards are
all already committed at HEAD `98a9058`. The only remaining
work to make a contributor able to compile the AP path is a
build-script entry point that selects the opt-out variant
without modifying source. That is a single
system-visible-boundary change to `scripts/build_tahoe.sh`,
which makes it the correct unit of work for this Stage 1.

A smaller change is not safe to bundle elsewhere: the
build-flag flip is the seam that activates compile-time
inclusion of the AP/HOSTAP code paths, so it must land as a
self-contained variant before any AP/GO HAL surface, Intel
backend, or role-7 delete-dispatch layer can be exercised.
A larger bundle would conflate the build seam with the
HAL/backend/dispatch layers and would prevent the auditor
from approving them independently.

## Recovered behavior contract for the build seam

The default Tahoe build path uses `xcodebuild -scheme
AirportItlwm-Tahoe -configuration Debug` with
`GCC_PREPROCESSOR_DEFINITIONS='$(inherited)
ITLWM_COMMIT_HASH=<hash>'`, and the Xcode configuration
inherits `IEEE80211_STA_ONLY` from the project's
`AirportItlwm-Tahoe` build settings. The header-level
enabler at `itl80211/openbsd/net80211/ieee80211_var.h:63-77`
is:

```c
#ifdef IEEE80211_OPT_OUT_STA_ONLY
#undef IEEE80211_STA_ONLY
#endif
```

It runs after every other path that could define
`IEEE80211_STA_ONLY` (the project-injected define and the
`SMALL_KERNEL` path) so the opt-out flag is always
authoritative when present and is a no-op when the flag is
unset. This shape preserves the existing default build's
behavior bit-for-bit.

The opt-out variant therefore needs only one additional
preprocessor define on the `xcodebuild` command line:
`IEEE80211_OPT_OUT_STA_ONLY`. There is no Xcode project file
change, no source-tree change, and no build-system rewrite.

Because Xcode's incremental build re-uses the same
`DerivedData/Build/Products/Debug/Tahoe/AirportItlwm.kext`
location for any invocation of the same scheme/configuration,
running an opt-out build into the default `DerivedData/`
would silently overwrite the default-STA staged kext after
the next default rebuild. The opt-out variant therefore
uses a separate `DerivedData-optout/` derived-data path and
a separate `Build/Debug/Tahoe-OptOut/AirportItlwm.kext`
staged root, so the two variants are independent and a
developer can have both staged simultaneously without one
clobbering the other.

## Newly exposed undefined symbols in the opt-out kext

The opt-out kext compiles the AP/HOSTAP code paths in
`itl80211/openbsd/net80211/ieee80211_node.c` (and the
publish call sites at lines 3226 and 3404 inside the
`#ifndef IEEE80211_STA_ONLY` block opening at line 2974 and
closing at line 3557). The newly compiled code references
two additional kernel mbuf APIs that the default build does
not reach:

- `_mbuf_dup`
- `_mbuf_setflags`

`nm -u` count goes from 927 (default) to 929 (opt-out); the
delta is exactly these two symbols. Both are exported by the
Tahoe BootKC at
`/System/Library/KernelCollections/BootKernelExtensions.kc`
and the `scripts/build_tahoe.sh` symbol-resolution step
returns `OK: all 929 undefined symbols resolve against
BootKC` for the opt-out kext.

There are no missing symbols to reconcile. The auditor's
hint in the layer-pointer entry from `2026-05-14h` ("define
`IEEE80211_OPT_OUT_STA_ONLY` in the build (and reconcile any
newly exposed BootKC undefined symbols)") is satisfied by
the empty reconciliation set.

## Build evidence

The candidate build evidence captured for this Stage 1, run
under explicit 420 s outer SSH timeouts on the canonical
guest worktree at HEAD `98a9058`, is:

Default STA build (no flag):

```
$ timeout 420s ssh ... 'cd /Users/devops/Projects/itlwm &&
  ./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc'
... ** BUILD SUCCEEDED ** ...
Build succeeded: /Users/devops/Projects/itlwm/Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm
Verifying symbols...
OK: all 927 undefined symbols resolve against BootKC
```

- staged path: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
- mach-o sha256: `3ddae5d1d92e9c357e917102106dc39e79ef8aebb93ecb3962d83fe25a50db21`
- mach-o LC_UUID: `71C45567-103F-3A30-960D-20B1DCE43469`
- mach-o size: 16374192 bytes
- undefined symbol count: 927
- log artifact:
  `commit-approval/artifacts/CR-478-stage1-build_log_default.txt`

Opt-out build (`--opt-out`):

```
$ timeout 420s ssh ... 'cd /Users/devops/Projects/itlwm &&
  ./scripts/build_tahoe.sh --opt-out /System/Library/KernelCollections/BootKernelExtensions.kc'
... ** BUILD SUCCEEDED ** ...
Build succeeded: /Users/devops/Projects/itlwm/Build/Debug/Tahoe-OptOut/AirportItlwm.kext/Contents/MacOS/AirportItlwm
Verifying symbols...
OK: all 929 undefined symbols resolve against BootKC
```

- staged path:
  `Build/Debug/Tahoe-OptOut/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
- mach-o sha256: `2655865dfae6e4098de36847aac1ed81ab0235507e91507aa8f927c3013e8012`
- mach-o LC_UUID: `E2A99F71-CBA4-3C1E-9E04-96415E9D7102`
- mach-o size: 16442600 bytes
- undefined symbol count: 929 (= 927 default + `_mbuf_dup` + `_mbuf_setflags`)
- log artifact:
  `commit-approval/artifacts/CR-478-stage1-build_log_optout.txt`

The default kext sha256 and `LC_UUID` are byte-identical
across re-runs of `./scripts/build_tahoe.sh` because no
source file changed; the script touches only Info.plist and
re-stages the existing kext binary. This is the same
default-STA artifact identity that was approved as the live
runtime kext for CR-477 Stage 2.

## Default-build no-regression argument

The default invocation of `scripts/build_tahoe.sh` is
behavior-equivalent to the prior version:

- The flag parser only consumes a leading `--opt-out`
  argument; any positional argument (the BootKC path) is
  preserved unchanged. With no `--opt-out` and no
  `BUILD_OPT_OUT_STA_ONLY=1`, `OPT_OUT_STA_ONLY=0`,
  `EXTRA_PP=""`, `VARIANT_LABEL="Tahoe"`,
  `DERIVED_DATA="$PROJECT_DIR/DerivedData"`, and
  `OUTPUT_ROOT="$PROJECT_DIR/Build/Debug/Tahoe"`. These are
  the same paths and the same effective preprocessor define
  list as before.
- The default `xcodebuild` invocation passes the same
  `GCC_PREPROCESSOR_DEFINITIONS='$(inherited)
  ITLWM_COMMIT_HASH=<hash>'` as before. The opt-out variant
  appends ` IEEE80211_OPT_OUT_STA_ONLY` to that string only
  when the flag is set.
- The MacKernelSDK header patches and the BootKC symbol
  verification are unchanged.

The structural identity claim is independently confirmed by
the byte-equal default kext sha256
`3ddae5d1d92e9c357e917102106dc39e79ef8aebb93ecb3962d83fe25a50db21`
and `LC_UUID` `71C45567-103F-3A30-960D-20B1DCE43469` after
both the prior CR-477 default build and the post-CR-478
script-change default build of HEAD `98a9058`.

## Claim scope

This Stage 1 claims:

- `scripts/build_tahoe.sh` exposes a `--opt-out` /
  `BUILD_OPT_OUT_STA_ONLY=1` entry point that defines
  `IEEE80211_OPT_OUT_STA_ONLY` in the kext build's
  `GCC_PREPROCESSOR_DEFINITIONS` and stages the result into
  a separate `Build/Debug/Tahoe-OptOut/` output tree.
- The default invocation produces the same kext identity
  (Mach-O sha256 / `LC_UUID`) as the prior version of the
  script.
- The opt-out kext compiles cleanly against the unchanged
  source tree and resolves all undefined symbols against
  the live Tahoe BootKC.
- The two newly exposed undefined symbols (`_mbuf_dup`,
  `_mbuf_setflags`) are exported by BootKC and require no
  source-tree fix.
- The runtime install/load/test claim is limited to a
  default-STA boot of the unchanged default kext, because
  the live runtime evidence at this layer is the
  default-build no-regression bound.

This Stage 1 explicitly does NOT claim:

- AP/GO HAL surface activation (HAL methods such as
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand` remain absent and will land as a
  separate layer).
- Any Intel iwx/iwm AP/GO firmware backend behavior; the
  CR-476 preflight guards still refuse the MAC-context
  command for non-STA / non-MONITOR `ic_opmode` values.
- AP-mode functional runtime: no scan/association/DHCP
  evidence using the host MT7612U test peer is part of this
  layer.
- Skywalk role-7 delete dispatch wiring; the explicit
  `AirportItlwm::deleteAPSTAOwner` path is reachable only
  from controller release teardown today.
- Legacy V1 controller migration of the V2 owner-routed
  selector wiring.
- Any opt-out-kext runtime claim. The opt-out kext is built
  and symbol-checked as evidence the build seam is
  functional, but is not installed, loaded, or executed
  under this Stage 1.
- DHCP success against `CONTROL_STA_NETWORK` or `FAST_LAB_AP`. The
  pre-existing client-side DHCP/EAPOL failure remains the
  same explicit non-claim it was at the previous layer.
- Project completion.

## Verification basis (decomp/reference)

- Producer bridge and the `IEEE80211_STA_ONLY` block scope
  are reviewer-approved in CR-471 Stage 2 v2 and inherited
  here. The publish call sites
  (`itl80211/openbsd/net80211/ieee80211_node.c:3226,3404`)
  sit inside the `#ifndef IEEE80211_STA_ONLY` block at
  lines 2974..3557.
- The header-level enabler at
  `itl80211/openbsd/net80211/ieee80211_var.h:63-77` was
  added in `a49b284 net80211: introduce AP station-event
  producer bridge and STA_ONLY opt-out` and is unchanged at
  HEAD.
- The host APSTA owner consumer binding lifecycle is
  reviewer-approved in CR-477 Stage 1 + Stage 2 v2 and is
  unchanged at HEAD.
- The iwx/iwm HOSTAP-panic preflight guards from CR-476
  remain in place; the opt-out build does not bypass them.
- The B12/B13 AP control-plane decomp closure remains
  binding for the recovered Apple AP/GO station-event owner
  contract; closure status was
  `AP_CONTROL_PLANE_CLOSURE_STATUS:
  FULL_LAYER_CLOSED_CODER_READY`,
  `REMAINING_DECOMP_TARGETS: EMPTY`,
  `REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`.
  No new decompilation cycle is required for this build seam.

## Scope clarification: per-layer-scope decomp closure flag

The root-level closure recorded above applies to the
recovered Apple AP/GO station-event owner contract that
future code-bearing layers (HAL surface activation,
Intel iwx/iwm AP/GO firmware backend, Skywalk role-7
delete dispatch wiring, legacy V1 controller migration)
will exercise; it is the closure status of *those*
layers' decomp/reference content, not the closure status
of this build-system seam's own diff. Within the exact
diff filed by this Stage 1 (`scripts/build_tahoe.sh` plus
this analysis report), no source-tree
(.c/.h/.cpp/.hpp/.m/.mm) file is touched and no decomp
evidence is recovered, so this seam has no decomp content
to close. The request therefore reports
`decomp_reference_debt_closed_for_scope: NO` for this
layer's scope, with the basis that no decomp scope is
present in the seam's diff. The recovered Apple contract
and the prior CR-471..CR-477 reviewer-approved chain are
preserved as the binding reference for follow-up layers,
as enumerated in the "Next layer pointer" section below.

## Live runtime after-layer plan

After Stage 1 approval the coder will:

1. Rebuild both variants on the canonical guest worktree
   at HEAD `98a9058` with explicit 420 s outer timeouts:
   - Default:
     `timeout 420s ssh -i ~/.ssh/aiam_itlwm_devops -p 3322
     devops@127.0.0.1 'cd /Users/devops/Projects/itlwm &&
     ./scripts/build_tahoe.sh
     /System/Library/KernelCollections/BootKernelExtensions.kc'`
   - Opt-out:
     `timeout 420s ssh ... '... ./scripts/build_tahoe.sh
     --opt-out
     /System/Library/KernelCollections/BootKernelExtensions.kc'`
   Both must report `BUILD SUCCEEDED` and that all
   undefined symbols resolve against BootKC. Default kext
   identity must equal Stage 1 candidate sha256
   `3ddae5d1d92e9c357e917102106dc39e79ef8aebb93ecb3962d83fe25a50db21`
   and `LC_UUID`
   `71C45567-103F-3A30-960D-20B1DCE43469`. Opt-out kext
   identity must equal Stage 1 candidate sha256
   `2655865dfae6e4098de36847aac1ed81ab0235507e91507aa8f927c3013e8012`
   and `LC_UUID` `E2A99F71-CBA4-3C1E-9E04-96415E9D7102`.
2. Install the exact built default
   `Build/Debug/Tahoe/AirportItlwm.kext` to
   `/Library/Extensions/AirportItlwm.kext` with `root:wheel`
   ownership and `go-w`. Do not unload the loaded driver.
   Approve any kext UI prompt through VNC `127.0.0.1:5901`
   and record the approval evidence. The opt-out kext is
   NOT installed under this layer.
3. Reboot the guest with
   `timeout 120s ssh -tt -i ~/.ssh/aiam_itlwm_devops -p 3322
   devops@127.0.0.1 'sudo shutdown -r now' || true`. SSH on
   `127.0.0.1:3322` must return within 120 seconds; if not,
   immediately inspect VNC `127.0.0.1:5901`, record the
   visible state, and follow the AGENTS.md recovery rule
   (rEFInd/macOS picker keystroke, login/desktop SSH
   diagnosis, or no-Wi-Fi panic recovery before any further
   reattach).
4. Verify the loaded kext identity matches the Stage 1
   default candidate: `kextstat | grep AirportItlwm`,
   `shasum -a 256` of
   `/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm`,
   `LC_UUID` via `otool -l`, and the IORegistry attach state
   for `IOService:/IOResources/AirportItlwm`. The loaded
   hash and `LC_UUID` must equal the Stage 1 default
   candidate values. If `kmutil showloaded` reports the
   prior kext identity (because the AuxKC blob is stale
   while the kext bundle id and version are unchanged across
   builds), clear
   `/Library/KernelCollections/AuxiliaryKernelExtensions.kc`
   and reboot once more so `kernelmanagerd` rebuilds AuxKC
   from the on-disk staged kext (precedent: CR-475 Stage 2
   evidence `stage2_runtime_start.txt`).
5. Default-STA boot acceptance evidence:
   - kernel log scan from boot for panic markers (`panic`,
     `Kernel trap`, `Unable to find driver`,
     `unsupported.opmode`, `refusing MAC context cmd`,
     `Stage1Net80211Event` AP marker text). None of these
     must fire on the default boot, because the publish
     call sites in `ieee80211_node.c` are still inside
     `#ifndef IEEE80211_STA_ONLY` for the loaded default
     kext, the role-7 APSTA owner is not allocated by the
     default STA boot, and the consumer-binding register
     call site is not reached at runtime.
   - `networksetup -listallhardwareports` and a scan from
     the Wi-Fi interface (normally `en1`) showing the
     interface is up.
   - `system_profiler SPAirPortDataType` reports a
     `Firmware Version: itlwm: 2.4.0 (...)` string with the
     reviewed-HEAD short hash `98a9058`.
   - `AirportItlwm` is registered in IORegistry; the Wi-Fi
     interface is present in STA mode.
6. Capture a five-minute stability window with no panic, no
   `unsupported.opmode` log, no driver unload, and no
   `refusing MAC context cmd for unsupported ic_opmode`
   marker on the default STA boot path.
7. Client-mode regression coverage:
   - Fast diagnostic loop: start
     `/home/dima/Projects/itlwm/start-fast_lab_ap-ap.sh` on
     the host, run `status-fast_lab_ap-ap.sh` for hostapd /
     dnsmasq / station-dump evidence, drive a scan / join /
     DHCP / IP exchange against the private alias `FAST_LAB_AP`
     (credential redacted; use the runtime-only private
     credential source), record at least a 60 s
     stability window and relevant kernel/airportd logs,
     then stop the lab AP with
     `/home/dima/Projects/itlwm/stop-fast_lab_ap-ap.sh`.
   - Final control verification: scan / join / DHCP / IP
     and a stability window against the private alias `CONTROL_STA_NETWORK`
     (credential redacted; use the runtime-only private
     credential source), plus the panic-marker scan
     from step 5, to confirm the build-seam change has no
     observable regression on the default-STA path. The
     pre-existing DHCP/EAPOL failure is the same explicit
     non-claim it was at the previous layer.
8. Out-of-scope for this layer's Stage 2 claim: AP/GO HAL
   surface activation, Intel iwx/iwm AP/GO firmware backend
   behavior, role-7 delete dispatch wiring, legacy V1
   controller migration, AP scan / association / DHCP from
   the host MT7612U (`Bus 002 Device 003`, `0e8d:7612`)
   test peer, CONTROL_STA_NETWORK/FAST_LAB_AP AP-side evidence, opt-out
   kext install/load/runtime, and project completion. Those
   remain explicit follow-up layers.

## Next layer pointer

After this layer the natural follow-up layers in dependency
order remain (per the AP control-plane closure document):

- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`. With the opt-out variant in
  place, this becomes the next code-bearing layer because
  the bridge / consumer / producer / preflight wiring
  already lands in source.
- Stage 2 Intel iwx/iwm AP/GO firmware backend (the
  `*_mac_ctxt_cmd` wrappers will need their conditional
  branches extended to cover AP/GO once the backend lands;
  today the CR-476 guard still refuses the command for
  anything other than MONITOR/STA, including AP/GO).
- Skywalk role-7 delete dispatch wiring so
  `AirportItlwm::deleteAPSTAOwner` is reached without the
  driver-release teardown, exercising the explicit
  unregister path.
- Legacy V1 controller migration of the V2 owner-routed
  selector wiring once a V1 owner is introduced or the V1
  controller is retired.
- Once all the above are in place and the opt-out kext is
  installed and exercised, AP-mode functional runtime
  against the host MT7612U (`Bus 002 Device 003`,
  `0e8d:7612`) test peer using the controlled
  `CONTROL_AP_MODE_PROFILE` profile.

Each is a separate coherent layer and is not part of this
Stage 1.
