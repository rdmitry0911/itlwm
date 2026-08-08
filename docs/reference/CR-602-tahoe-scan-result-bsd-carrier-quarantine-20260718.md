# CR-602 — Tahoe BSD `SCAN_RESULT` carrier quarantine

## Scope

This change restores the Tahoe IOUC/WCL-first ownership boundary for external
`APPLE80211_IOC_SCAN_RESULT` requests and prevents the same nested-carrier
hazard on BSD `APPLE80211_IOC_CURRENT_NETWORK`.  The 2026-08-08 follow-up also
restores the latter request through Apple's safe user-copy boundary.  It does
not change the local scan iterator or fabricate a new result format.

## Evidence

- Darwin's BSD ioctl path copies the outer `apple80211req`, but its nested
  `req_data` pointer remains caller-owned.  It is not a kernel result buffer.
- The Tahoe `apple80211_scan_result` carrier is `0x8d8` bytes.
- The pre-change bridge passed that nested pointer directly to
  `getSCAN_RESULT`, whose serializer begins by zeroing the full carrier.
  A direct BSD request can therefore fault under SMAP before any Wi-Fi state
  is consumed.
- `CURRENT_NETWORK` uses the same carrier and serializer.  The initial fix
  quarantined its BSD callback to the family transport rather than passing a
  nested caller address into that helper.  Runtime subsequently proved that
  the family transport has no Intel current-network producer and returns
  unsupported even while compact STATE, SSID and BSSID are valid.
- Current 25C56 IO80211Family exports
  `IO80211Controller::copyOut(void const *, unsigned long long, unsigned long)`.
  Its KDK body at `0x10f340` calls kernel `_copyout` with those three arguments
  and returns the resulting BSD errno.  That is the recovered safe boundary
  for a controller-produced kernel buffer.
- The historical Tahoe IOUC-first route deliberately returned unsupported for
  this selector so `IO80211InfraProtocol::processBSDCommand()` could own the
  WCL scan transport.  The controller-side Tahoe route table does not own
  selector 11, while the normal scan path publishes `APPLE80211_M_WCL_SCAN_RESULT`.

## Contract

For Tahoe, `processApple80211Ioctl()` returns `kIOReturnUnsupported` for
selector 11 without inspecting `req_data`.  `processBSDCommand()` then
delegates to its superclass.  The local iterator helper remains present for
explicit private uses, but the Tahoe external BSD bridge cannot reach it or
dereference a nested caller pointer.  Tahoe BSD `CURRENT_NETWORK` requires the
exact `0x8d8` length and a non-null destination, builds its result in a
kernel-local carrier under the live controller lifecycle gate, and invokes
`IO80211Controller::copyOut` only after the existing current-network producer
succeeds.  It never gives `req_data` to the serializer.  Its controller
card-specific route remains intact and pre-Tahoe behavior is unchanged.

## Regression coverage

`scripts/test_tahoe_scan_result_bsd_carrier_quarantine.sh` statically asserts
the SCAN_RESULT fallthrough, the absence of a nested-carrier serializer write,
the CURRENT_NETWORK exact-length/null/lifecycle/copyout gates, the Tahoe
carrier-size assertion, and continued WCL scan-result publication.
