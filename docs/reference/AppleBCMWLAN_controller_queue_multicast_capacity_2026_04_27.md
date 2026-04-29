# AppleBCMWLAN controller queue, capacity, promiscuous, and multicast contracts

Date: 2026-04-27

Scope: Tahoe controller-facing contracts adjacent to the recovered WCL action
frame layer. This note records decompiled Apple behavior used for local
structural alignment. It does not claim primary STA association, DHCP, RSN/EAPOL,
or AP/SoftAP runtime success.

## Queue size and timeout

Function:

- `AppleBCMWLANCore::requestQueueSizeAndTimeout(unsigned short*, unsigned short*)`
- Address: `0xffffff8001583018`

Observed behavior:

- Reads DT parameter `wlan.coalesce.qsize` from the `IOService` plane.
- Reads DT parameter `wlan.coalesce.timeout` from the `IOService` plane.
- If either low 16-bit value is zero, returns `0xe00002c7`.
- If both low 16-bit values are nonzero, writes them to the caller-provided
  queue and timeout pointers, then returns `0`.

Local deviation before this batch:

- `AirportItlwm::requestQueueSizeAndTimeout` returned success unconditionally
  and did not write either output pointer.

## Data queue depth

Functions:

- `AppleBCMWLANCore::fetchAndUpdateRingParameters()`
- Address: `0xffffff800159418a`
- `AppleBCMWLANCore::getDataQueueDepth(OSObject*)`
- Address: `0xffffff8001634388`
- `IO80211SkywalkInterface::getDataQueueDepth()`
- Address: `0xffffff8002276f66`

Observed behavior:

- `fetchAndUpdateRingParameters` initializes core-private `+0x1154` to `0x200`.
- The same field can be updated from a recovered ring-parameter property path.
- `getDataQueueDepth(OSObject*)` returns the 16-bit value at core-private
  `+0x1154`.
- `IO80211SkywalkInterface::getDataQueueDepth()` dispatches through the bound
  controller vtable slot for `getDataQueueDepth(OSObject*)`.

IO80211 default:

- `IO80211Controller::getDataQueueDepth(OSObject*) @ 0xffffff8002216c70`
  returns `0x400`.

Local deviation before this batch:

- Tahoe local controller did not override `getDataQueueDepth`, so the family
  default `0x400` contract could be used instead of the AppleBCMWLANCore ring
  depth default `0x200`.

## Action-frame pool capacity

Function:

- `IO80211Controller::getActionFramePoolCapacity()`
- Address: `0xffffff800221a26e`

Observed behavior:

- Returns `0x100`.

Local alignment:

- Tahoe local controller now exposes the same explicit capacity locally.

## Promiscuous mode

Function:

- `AppleBCMWLANCore::setPromiscuousMode(bool)`
- Address: `0xffffff80015e07cc`

Observed behavior:

- Loads core-private state from `this+0x128`.
- Stores the requested bool byte at core-private `+0x4778`.
- Returns `0`.

Local deviation before this batch:

- Tahoe local controller returned success but had no owner-state witness for
  the requested promiscuous mode.

## Multicast mode

Function:

- `AppleBCMWLANCore::setMulticastMode(bool)`
- Address: `0xffffff80015e07ec`

Observed behavior:

- If core-private `+0x2891` has bit `0x80` set, returns `0xe0823804`.
- Otherwise, rejecting-commands state returns success without issuing the
  firmware update.
- Enabling multicast mode returns success without the disable-path IOVAR.
- Disabling multicast mode calls `setAllMulticast(false)` and, if successful,
  sends IOVAR `mcast_list` with a 12-byte empty payload.

Local alignment in this batch:

- Records the requested multicast mode as controller owner state.
- Does not issue the Apple firmware IOVAR yet because the Broadcom commander
  owner path is not locally recovered.

## Multicast list

Function:

- `AppleBCMWLANCore::setMulticastList(ether_addr const*, unsigned int)`
- Address: `0xffffff80015e0930`

Observed behavior:

- If core-private `+0x2891` has bit `0x80` set, returns `0xe0823804`.
- Otherwise, rejecting-commands state returns success.
- Rejects caller count above `0x20` with `0xe00002bc`.
- Caches caller multicast count at core-private `+0x234`.
- Caches caller addresses starting at core-private `+0x238`, 6-byte stride.
- Uses a `0xca`-byte stack payload prefilled with `0xaa`.
- Encodes IOVAR payload length as `4 + count * 6`, with IOVAR name
  `mcast_list`.
- If the APSTA/proximity owner is present, Apple may append the AWDL Bonjour
  multicast address and may use the virtual IOVAR path.
- If the effective count reaches the capacity boundary, Apple switches to
  `setAllMulticast(true)`.

Local alignment in this batch:

- Adds exact limit, status, payload, string, and cache constants.
- Caches the caller list in Tahoe controller owner state.
- Rejects counts above `0x20` with `0xe00002bc`.
- Leaves the existing Intel HAL multicast update as the active backend path
  until the Apple commander/virtual-interface multicast owner path is fully
  recovered.

## Base-family defaults

Relevant IO80211Family defaults:

- `IO80211Controller::hardwareOutputQueueDepth() @ 0xffffff8002219b96` returns
  `0`.
- `IO80211Controller::requiresExplicitMBufRelease() @ 0xffffff8002219bca`
  returns `false`.
- `IO80211Controller::requestQueueSizeAndTimeout(...) @ 0xffffff8002219cb0`
  returns `0xe00002c7`.
- Base `setPromiscuousMode`, `setMulticastMode`, and `setMulticastList` each
  return `0xe00002c7`.

## Non-claims

- This note does not claim the final association/data blocker is fixed.
- This note does not enable AP/SoftAP runtime behavior.
- This note does not implement Broadcom multicast IOVAR dispatch locally.
- This note does not add retry, polling, fallback, forced state, or synthetic
  queue parameters.
