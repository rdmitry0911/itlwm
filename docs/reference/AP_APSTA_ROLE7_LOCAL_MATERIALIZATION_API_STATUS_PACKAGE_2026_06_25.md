# AP/APSTA Role-7 Local Materialization API And Status Package, 2026-06-25

## Purpose

This package is the bounded local/reference recovery step requested after
`AP-APSTA-role7-admission-materialization-recovery-20260625`. It preserves the
accepted role-7 create/delete `errno=102` runtime custody as negative evidence
only and recovers the direct local APIs and reference anchors needed before any
`IMPLEMENT_LOCAL` APSTA materialization patch can be reviewed.

No source semantic patch, build, install, reboot, kext load/unload, AP/client
runtime, OpenCore mutation, evidence-index mutation, validator mutation, or
commit was performed for this package.

## Direct Local Entry Points

The current Tahoe/Skywalk public dispatch is direct and bounded:

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:1943` routes
  `APPLE80211_IOC_VIRTUAL_IF_CREATE` only for `SIOCSA80211` into
  `setVIRTUAL_IF_CREATE`.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:1946` routes
  `APPLE80211_IOC_VIRTUAL_IF_DELETE` only for `SIOCSA80211` into
  `setVIRTUAL_IF_DELETE`.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5472` implements the role
  split. Null create carriers return `0xe00002bc`; roles `8..10` return
  `0xe00002c7`; role `6` returns `0xe00002bd`; unknown roles return
  `0xe0000001`; role `7` enters the host APSTA owner path.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5522` calls
  `AirportItlwm::ensureAPSTAOwner(data)`, then
  `AirportItlwmAPSTAOwner::startLowerIfReady()` at line `5528`. On non-success
  it calls `deleteAPSTAOwner()` at line `5530` before returning the lower
  failure.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5540` implements delete as a
  switch-only handler. Null carriers return `0xe00002bc`; absent controller
  returns `kIOReturnNotReady`; otherwise it calls
  `AirportItlwm::deleteAPSTAOwnerForBSDName(data->bsd_name)`.

The local role-7 owner path is also explicit:

- `AirportItlwm/AirportItlwmV2.cpp:6710` creates `AirportItlwmAPSTAOwner` only
  when no owner exists, stores it at `fAPSTAOwner`, and returns the existing
  owner on duplicate local create entry.
- `AirportItlwm/AirportItlwmAPSTAOwner.cpp:28` initializes the owner from the
  create carrier, accepts only `APPLE80211_VIF_SOFT_AP`, copies the carrier MAC,
  stores the carrier BSD name, and defaults an empty name to `apsta0`.
- `AirportItlwm/AirportItlwmAPSTAOwner.cpp:99` maps absent owner/HAL to
  `kIOReturnNotReady`, `supportsAPMode()==false` to `kIOReturnUnsupported`, and
  otherwise returns the lower `startAPMode()` status. It writes AP-up state only
  on lower success.
- `AirportItlwm/AirportItlwmAPSTAOwner.cpp:132` and `:144` reset local AP state
  during stop/teardown. `:152` rejects null or empty delete names and matches
  the stored BSD name before allowing delete.
- `AirportItlwm/AirportItlwmV2.cpp:6769` unregisters the net80211 APSTA event
  callback under the same opt-out gate before releasing the owner. `:6793`
  returns `kIOReturnUnsupported` for absent or name-mismatched owners and
  `kIOReturnSuccess` only after a matching delete.

These local edges prove the current code owns carrier parsing, owner lifetime,
lower-start gating, and fail-closed delete. They do not materialize a published
APSTA child interface.

## Local IO80211/Skywalk Publication API

The only complete local Tahoe/Skywalk interface publication sequence is the
primary STA path in `AirportItlwm::start`:

- `AirportItlwm/AirportItlwmV2.cpp:3763` binds the `AirportItlwmSkywalkInterface`
  to the controller through `bindController(this)`.
- `AirportItlwm/AirportItlwmV2.cpp:3790` attaches the interface IOService to the
  controller; `:3800` calls `attachInterface(fNetIf, this)`.
- `AirportItlwm/AirportItlwmV2.cpp:3822` zeroes
  `IOSkywalkEthernetInterface::RegistrationInfo`; `:3823` calls
  `initRegistrationInfo(&registInfo, 1, sizeof(registInfo))`.
- `AirportItlwm/AirportItlwmV2.cpp:3856..4039` creates TX/RX packet pools,
  TX submission, TX completion, RX completion, multicast queue, and attaches the
  queue event sources to the controller workloop with rollback on partial
  failure.
- `AirportItlwm/AirportItlwmV2.cpp:4111` calls
  `IO80211InfraInterface::registerInfraEthernetInterface(...)` on Tahoe, passing
  the registration info, queues, queue count, TX pool, and RX pool. The pre-Tahoe
  fallback at `:4120` is `registerEthernetInterface(...)`.
- `AirportItlwm/AirportItlwmV2.cpp:4162` starts the Skywalk interface.
- `AirportItlwm/AirportItlwmV2.cpp:4172` calls `deferBSDAttach(false)`. The
  local comment records the asynchronous chain:
  `deferBSDAttach(false) -> registerService on fNetIf -> IOSkywalkNetworkBSDClient
  -> prepareNexusCallback -> gatedPrepareNexus -> registerBSDInterface ->
  setBSDName -> BSD ifnet appears`.
- `AirportItlwm/AirportItlwmV2.cpp:4220` calls controller `registerService()`
  only after the interface registration and BSD attach path are set up.

The local headers expose the APIs used by that sequence:

- `include/Airport/IO80211InfraInterface.h:27` declares
  `registerInfraEthernetInterface(...)` for Tahoe.
- `include/Airport/IOSkywalkEthernetInterface.h:72` declares
  `initRegistrationInfo(...)`; `:77` declares `registerEthernetInterface(...)`.
- `include/Airport/IOSkywalkNetworkInterface.h:82..89` declares
  `registerNetworkInterfaceWithLogicalLink`, `deregisterLogicalLink`,
  `setBSDName`, and `getBSDName`; `:68` documents `deferBSDAttach` in the
  vtable order.
- `include/Airport/IO80211Controller.h:229` and
  `include/Airport/IO80211ControllerV2.h:225` expose the Tahoe-era
  `attachInterface` surfaces for Skywalk interfaces.
- `include/Airport/IO80211SkywalkInterface.h:333` exposes
  `setInterfaceRole(UInt)`, with `getInterfaceRole()` at `:335`.

The implementation consequence is narrow: a future role-7 local patch must add a
real APSTA child-interface object/lifetime that uses the same Skywalk
registration family as the primary STA path, not merely mark
`AirportItlwmAPSTAOwner` running. Returning success before that child interface
is registered and BSD-published would repeat the current false AP-success risk.

## BSD Name And Role Publication

The recovered Apple APSTA identity contract remains the reference target:

- `analysis/ANALYSIS_REPORT_2026-04-23.md` records the role-7 branch of
  `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280`: role is
  read at `data+0x0c`, MAC at `data+0x04`, BSD-name carrier at `data+0x10`, the
  APSTA factory is called, and the APSTA owner is stored at core expansion
  `+0x2c30`.
- The same analysis records the APSTA registration-info assembly range
  `0xffffff80015fc628..0xffffff80015fc73b`: `RegistrationInfo` size `0x130`,
  subfamily `3`, BSD prefix `ap`, unit `1`, options/power fields, APSTA hardware
  address at `info+0x108`, and `AppleBCMWLANIO80211APSTAInterface::start(core,
  info)`.
- `AirportItlwm/AirportItlwmSkywalkInterface.hpp:73..101` records the local
  primary interface subfamily mechanism: `getInterfaceSubFamily()` returns `3`,
  and the framework stores that value into the BSD interface parameters. This is
  a local proof of the identity callback family, not APSTA publication by itself.
- `AirportItlwm/AirportItlwm.cpp:1051` shows the older virtual-interface helper
  creates only P2P/AWDL-style `IO80211VirtualInterface` objects for local roles
  `1..4`, using prefix `awdl` or `p2p`; it is not the Tahoe APSTA role-7
  materialization path.

Current local APSTA owner state stores a name string and role byte, but it never
calls `setInterfaceRole`, `setInterfaceId`, `setBSDName`,
`deferBSDAttach(false)`, `registerInfraEthernetInterface`, or interface
`registerService()` for a distinct APSTA child. Therefore role/name publication
is still absent even when the owner and lower-start code are present.

## Successful-Materialization Rollback And Delete Cleanup

The existing fail-before-publication path is closed: if lower start fails, the
owner is deleted before the create return. That is insufficient for a future
success path. Once APSTA child materialization is added, rollback must be
extended through every published leaf:

- stop lower AP mode only after preserving idempotent no-start cleanup;
- unregister the net80211 APSTA event callback before releasing owner storage;
- stop/remove/release TX submission queues, TX completion queue, RX completion
  queue, and multicast queue in the reverse order recorded by
  `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`;
- release TX/RX packet pools and clear APSTA state fields before owner release;
- deregister or stop the Skywalk child interface before clearing the owner
  pointer;
- keep delete name-only: no allocation, retry, forced success, or AP-up
  transition on a missing or mismatched owner.

`docs/reference/AppleBCMWLAN_APSTA_teardown_2026_04_27.md` records the reference
cleanup of APSTA resources, queues, work sources, multicast queue, and super
stop. Any local implementation must map that cleanup to the local Skywalk
objects it creates; the current owner-only cleanup is not enough after public
interface registration succeeds.

## Role-7 IOReturn To User Status Ledger

Direct local return edges for role-7 create/delete are now bounded:

- create null carrier: `0xe00002bc`;
- create absent controller: `kIOReturnNotReady`;
- create owner allocation/init failure: raw `0x16`;
- create lower unsupported: `kIOReturnUnsupported` from
  `AirportItlwmAPSTAOwner::startLowerIfReady()`;
- create lower start failure: exact lower `startAPMode()` status after owner
  cleanup;
- create success: currently lower success only, but this is incomplete because
  APSTA child-interface publication is still absent;
- delete null carrier: `0xe00002bc`;
- delete absent controller: `kIOReturnNotReady`;
- delete absent/mismatched owner: `kIOReturnUnsupported`;
- delete matching owner: `kIOReturnSuccess` after cleanup.

The accepted runtime evidence records `ioctl_ret=-1` and `errno=102` for both
role-7 create and delete. The Tahoe SDK used by this guest defines kernel/
UNIX03 `EOPNOTSUPP` as `102` in `sys/errno.h`, while `ENOTSUP` remains `45`.
The local IO80211 headers and `docs/reference/AppleBCMWLAN_skywalk_helpers_2026_04_28.md`
identify `IO80211SkywalkInterface::errnoFromReturn(int)` as an inherited
`IOService::errnoFromReturn` virtual, with BootKC symbol address
`0xffffff800227862e` deferred from the local header batch.

This closes the observable status ledger to this level: the current negative
runtime maps a fail-closed role-7 IOReturn to user `EOPNOTSUPP` (`errno=102`). It
does not prove that every future APSTA registration or rollback failure should
use that same public status. A semantic implementation request must either keep
unimplemented/publication failure edges on the existing fail-closed unsupported
path or include direct decompilation of the inherited `errnoFromReturn`/caller
bridge before claiming a different public errno.

## Implementation Gate

The next semantic patch may be reviewed only if it implements one coherent
local APSTA materialization layer with these system-visible boundaries:

- APSTA child-interface object lifetime and controller ownership;
- APSTA `RegistrationInfo` production with reference identity fields;
- Skywalk queue/pool/workloop setup or a documented no-data-path fail-closed
  boundary that cannot report role-7 success;
- `registerInfraEthernetInterface`/BSD attach publication for the APSTA child;
- BSD name and role publication for the APSTA interface;
- rollback from every partial materialization leaf;
- delete cleanup for both pre-publication and post-publication APSTA owners;
- public status mapping that does not relabel `errno=102` as AP success.

Anything smaller is a new research slice only if it names a newly discovered
reference or local API gap. Another AP/client runtime run remains unjustified
until this materialization layer exists and has Stage 1 approval.
