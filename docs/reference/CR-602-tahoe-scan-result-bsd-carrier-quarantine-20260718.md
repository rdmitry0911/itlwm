# CR-602 — Tahoe BSD `SCAN_RESULT` carrier quarantine

## Scope

This change restores the Tahoe IOUC/WCL-first ownership boundary for external
`APPLE80211_IOC_SCAN_RESULT` requests and prevents the same nested-carrier
hazard on BSD `APPLE80211_IOC_CURRENT_NETWORK`.  It does not change the local
scan iterator or fabricate a new result format.

## Evidence

- Darwin's BSD ioctl path copies the outer `apple80211req`, but its nested
  `req_data` pointer remains caller-owned.  It is not a kernel result buffer.
- The Tahoe `apple80211_scan_result` carrier is `0x8d8` bytes.
- The pre-change bridge passed that nested pointer directly to
  `getSCAN_RESULT`, whose serializer begins by zeroing the full carrier.
  A direct BSD request can therefore fault under SMAP before any Wi-Fi state
  is consumed.
- `CURRENT_NETWORK` uses the same carrier and serializer.  Its Tahoe
  controller card-specific route remains kernel-owned; the BSD callback is
  quarantined to the family transport rather than passing a nested caller
  address into that helper.  This is a safety boundary, not a claim that the
  family route has the same producer contract.
- The historical Tahoe IOUC-first route deliberately returned unsupported for
  this selector so `IO80211InfraProtocol::processBSDCommand()` could own the
  WCL scan transport.  The controller-side Tahoe route table does not own
  selector 11, while the normal scan path publishes `APPLE80211_M_WCL_SCAN_RESULT`.

## Contract

For Tahoe, `processApple80211Ioctl()` returns `kIOReturnUnsupported` for
selector 11 without inspecting `req_data`.  `processBSDCommand()` then
delegates to its superclass.  The local iterator helper remains present for
explicit private uses, but the Tahoe external BSD bridge cannot reach it or
dereference a nested caller pointer.  Tahoe BSD `CURRENT_NETWORK` likewise
delegates before the local dispatcher, while its controller card-specific
route remains intact.  Pre-Tahoe behavior is unchanged.

## Regression coverage

`scripts/test_tahoe_scan_result_bsd_carrier_quarantine.sh` statically asserts
the fallthrough, the absence of a nested-carrier dereference, the Tahoe
carrier-size assertion, and continued WCL scan-result publication.
