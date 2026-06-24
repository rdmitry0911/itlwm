# itlwm-rm-01 lineage and Tahoe substrate evidence package

work_item_id: itlwm-rm-01
closure_id: itlwm-rm-01
collection_role: coder
collection_date_utc: 2026-06-24
canonical_guest_repository: /Users/devops/Projects/itlwm
canonical_guest_head: f0b491390b2224514af49f5adf54ac7d600b54ee
canonical_guest_parent: 2b261c1567c1a96104cea691bba3fb095031fd44
evidence_scope: guest-repository lineage, Tahoe build/install/load substrate, and approval-custody anchors

## Source Boundary

All evidence in this package was collected from the canonical guest repository at
`/Users/devops/Projects/itlwm` over SSH. No host checkout, host mirror, `/tmp`
copy, cloned replacement, copied `.git` directory, rsync output, or terminal-only
claim is used as source evidence.

The guest worktree already contained unrelated dirty/staged CR479 and AP/APSTA
approval artifacts before this package was written. This package does not reset,
hide, move, stage, commit, or fold any of that work into the `itlwm-rm-01`
evidence claim.

No build, install, reboot, kext load/unload, runtime probe, OpenCore mutation,
driver source patch, or commit was performed for this evidence package.

## Repository And Open-Source Lineage

Committed guest history and remotes establish the source lineage without any
host-source substitution:

- `git rev-parse HEAD` in `/Users/devops/Projects/itlwm` returned
  `f0b491390b2224514af49f5adf54ac7d600b54ee`.
- `git rev-list --count HEAD` returned `1018` reachable commits.
- `git rev-list --max-parents=0 HEAD` returned root commit
  `7764d8b6fcc28bf75db159f50803aec891db4339`.
- `git show --no-patch` for that root records author `zhongxianyao`, date
  `2020-02-19T08:43:43+08:00`, subject `Initial Commit`.
- `git remote -v` records `origin` as `https://github.com/rdmitry0911/itlwm`
  for fetch and push.
- `git log --oneline --decorate -n 8` records `origin/master` at
  `9d50a3d7f72d9bbbe4fbb4a2c617aa30b6770c53` and the local stack through
  `5f39f9a26e698458152e509dd1eeb96fb62e8290`,
  `2b261c1567c1a96104cea691bba3fb095031fd44`, and current HEAD
  `f0b491390b2224514af49f5adf54ac7d600b54ee`.

Committed repository metadata anchors the open-source itlwm/AirportItlwm
identity:

- `README.md` blob `b0200361a5426f8d8e580332cad6b6515fa7f1ce` identifies the
  project as `itlwm` and as an Intel Wi-Fi Adapter kernel extension for macOS
  based on OpenBSD.
- The same committed README links project documentation and releases under
  `OpenIntelWireless/itlwm`, credits OpenBSD `net80211`, `iwn`, `iwm`, and
  `iwx`, and credits Intel/Linux `iwlwifi` lineage.
- `LICENSE` blob `d159169d1050894d3ea3b98e1c965c4058208fe1` is the GNU GPL
  version 2 license text.

## Committed Tahoe Build Substrate

The requested Tahoe build script blob is present at the current committed HEAD:

- `scripts/build_tahoe.sh`: mode `100755`, blob
  `7cab7d414ab2197d3e4b4d29485ce0e8f23269c2`.

The committed script defines the Tahoe build envelope:

- target: `AirportItlwm-Tahoe`
- configuration: `Debug`
- default output: `Build/Debug/Tahoe/AirportItlwm.kext`
- opt-out output: `Build/Debug/Tahoe-OptOut/AirportItlwm.kext`
- Xcode build output source:
  `DerivedData/Build/Products/Debug/Tahoe/AirportItlwm.kext`
- default BootKC path:
  `/Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc`
- effective preprocessor gate check: the script fails if the Tahoe target does
  not include `USE_APPLE_SUPPLICANT`.
- opt-out build macros: `IEEE80211_OPT_OUT_STA_ONLY` and
  `IEEE80211_APSTA_STATION_EVENT_OPT_OUT` are injected only for the opt-out
  envelope.
- staging: the built `AirportItlwm.kext` is copied into the deterministic
  `Build/Debug/<variant>/AirportItlwm.kext` output root.
- symbol check: the script runs `nm -u` over the staged kext binary, compares
  the undefined symbols with exported BootKC symbols from `nm -g`, fails on any
  unresolved symbol, and otherwise prints the resolved undefined-symbol count.

The committed Xcode and kext identity files are present at current HEAD:

- `itlwm.xcodeproj/project.pbxproj`: blob
  `68814cacfa35c7ad0f89cb93e5cd096dac12377b`.
- `AirportItlwm/AirportItlwm-Tahoe-Info.plist`: blob
  `f1ccc092a2544c4ab2c19a342938947ebabfc92d`.
- `AirportItlwm/Info.plist`: blob
  `6286d0a8a858a143b4c5e903d8b42cb65b15b980`.

The committed Xcode project defines the Tahoe kext target and build settings:

- PBX native target name: `AirportItlwm-Tahoe`.
- product reference: `AirportItlwm.kext`.
- product type: `com.apple.product-type.kernel-extension`.
- Debug/Release `CONFIGURATION_BUILD_DIR`: `$(SYMROOT)/$(CONFIGURATION)/Tahoe`.
- Tahoe `INFOPLIST_FILE`: `AirportItlwm/AirportItlwm-Tahoe-Info.plist`.
- Tahoe `PRODUCT_BUNDLE_IDENTIFIER`: `com.zxystd.AirportItlwm`.
- Tahoe preprocessor definitions include `AIRPORT`, `USE_APPLE_SUPPLICANT`,
  `__IO80211_TARGET=__MAC_26_0`, `__PRIVATE_SPI__`, and `IO80211FAMILY_V3`.
- Tahoe `KERNEL_EXTENSION_HEADER_SEARCH_PATHS` points to
  `$(PROJECT_DIR)/MacKernelSDK/Headers`.

The committed Tahoe Info.plist defines the load/match substrate:

- bundle identifier: `com.zxystd.AirportItlwm`.
- `NetworkController` personality uses `IOClass` `AirportItlwm`,
  `IOMatchCategory` `WiFiDriver`, `IONetworkRootType` `airport`, and
  provider `IOPCIEDeviceWrapper`.
- `itlwm` personality uses `IOClass` `IOPCIEDeviceWrapper`, provider
  `IOPCIDevice`, and Intel PCI match IDs.
- `AirportItlwmBootNub` personality binds below `AirportItlwm`.
- OSBundle libraries include IO80211Family, IOSkywalkFamily,
  IONetworkingFamily, IOPCIFamily, and Apple KPIs.
- `OSBundleRequired` is `Network-Root`.

## Existing Build, Install, And Load Custody

This package does not create new build or runtime evidence. Build, install, and
load facts are tied only to existing approval/custody evidence:

- `commit-approval/requests/COMMIT_REQUEST_AP-APSTA-intel-apgo-firmware-backend-stage1-20260623.md`
  records the accepted Stage 1 request at reviewed head
  `2b261c1567c1a96104cea691bba3fb095031fd44`, patch sha256
  `bb22eb6771506bd66f451ec78c638f813e78005bcc5da2c56be06081163dd10c`,
  default build evidence sha256
  `d4ff783445b2976025a6c27515285ccc61ab012c64710ef247e4c01f6f80aea3`, and
  opt-out build evidence sha256
  `ccc28406d1ef16a3b06a2f59ac06d92df47db97e60381bc7fdf68287386f8469`.
- `commit-approval/stage2_evidence/AP-APSTA-intel-apgo-firmware-backend-stage2-runtime-20260624/STAGE2_EVIDENCE_AP-APSTA-intel-apgo-firmware-backend-runtime-20260624.md`
  has sha256 `ab53fde3249e23d145e03210c995662de9a6a7ecef047c4eb4de2bae9d21fc3a` and
  records loaded identity `com.zxystd.AirportItlwm (2.4.0)`, loaded UUID
  `868AFA9D-AD9E-373E-BDB6-1200B7B8C89D`, and matching installed/staged binary
  sha256 `9f7cb96c2ae114efe995a189a2a209c0978bfe8ef1fe459f609e7a861951f9cf`.
- `commit-approval/decisions/COMMIT_DECISION_AP-APSTA-intel-apgo-firmware-backend-stage1-20260623.md`
  records Stage 2 `APPROVED`, `allow_commit_now: YES`, the same request/patch
  hashes, default and opt-out build hashes, Stage 2 evidence hash, loaded/staged
  kext hash, and the bounded role-7 ioctl probe result `ioctl_ret=-1 errno=102`.
- `commit-approval/status/AP_APSTA_EXACT_DIFF_POST_COMMIT_CUSTODY_20260624.md`
  records the final guest commit
  `f0b491390b2224514af49f5adf54ac7d600b54ee`, parent
  `2b261c1567c1a96104cea691bba3fb095031fd44`, accepted patch sha256
  `bb22eb6771506bd66f451ec78c638f813e78005bcc5da2c56be06081163dd10c`, commit
  patch-id `cd1eb8a0279f8a1358eba70008bfac9c6fb140f5`, the same accepted build
  and Stage 2 runtime evidence hashes, and loaded/staged kext hash
  `9f7cb96c2ae114efe995a189a2a209c0978bfe8ef1fe459f609e7a861951f9cf`.

## Non-Claims

- This package does not claim AP/GO functional success, role-7 create success,
  AP-up state, beacon emission, AP client association, DHCP, AP traffic,
  station firmware commands, AP key install, CSA, CR479 closure, OpenCore
  mutation, terminal project completion, or any new runtime result.
- This package does not authorize or perform another build, install, reboot,
  kext load/unload, runtime probe, OpenCore mutation, source patch, or commit.
- This package does not inspect, clone, copy, mirror, or replace the guest `.git`
  directory with a host path.

## No-Workaround Proof

This result is not a shortcut, backfill, renamed surrogate, status-only proof,
schema-only proof, post-factum artifact repair, semantic degradation of
previously accepted behavior, or no-workaround violation. It narrows
`itlwm-rm-01` to auditable, guest-repository evidence: committed git history,
committed remotes, committed README/LICENSE lineage, committed Tahoe build/kext
blob IDs, and already accepted approval/build/load custody artifacts. It does
not manufacture runtime proof, does not substitute host source for the canonical
guest repository, does not weaken any AP/APSTA or CR479 non-claim, and does not
increase runnable implementation scope.

## Verification

- package_path:
  `commit-approval/evidence/ITLWM_RM_01_LINEAGE_TAHOE_SUBSTRATE_20260624.md`
- package_written_in: `/Users/devops/Projects/itlwm`
- package_status: submitted for auditor use
- build_performed_in_this_cycle: NO
- install_performed_in_this_cycle: NO
- runtime_performed_in_this_cycle: NO
- commit_performed_in_this_cycle: NO
