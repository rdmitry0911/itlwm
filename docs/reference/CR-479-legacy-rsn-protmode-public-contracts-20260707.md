# CR-479 legacy RSN/PROTMODE public contracts

Date: 2026-07-07

Scope: legacy STA dispatcher parity for `PROTMODE`, public `RSN_IE`, and the
internal association RSN IE carrier used by local join paths.

## Reference evidence

- `apple80211getPROTMODE(IO80211SkywalkInterface*, void*)`
  at `0xffffff80021e2869` is a five-byte jump to the common unsupported shim
  `0xffffff80009ff8a0`. It does not write an
  `apple80211_protmode_data` carrier.
- Public `getRSN_IE` wrapper
  `getRSN_IE(IO80211Controller*, IO80211SkywalkInterface*,
  IO80211APIUserClient*, apple80211req*)` at `0xffffff80021fabfa` calls the
  recovered helper `FUN_ffffff80021e30e9`.
- `FUN_ffffff80021e30e9` first calls the interface gate at virtual offset
  `+0xcc8` with selector `0x29`, then owner-lookups the vendor interface, then
  tailcalls the vendor virtual at offset `+0x398`. When no owner exists it
  returns `0xe082280e`.
- `AppleBCMWLANCore::setRSN_IE(apple80211_rsn_ie_data*)`
  at `0xffffff800160433e` returns success immediately.
- The RSN IE producer for hidden Tahoe association is not public
  `setRSN_IE`; hidden join uses the recovered assoc-candidates carrier fields
  `+0xd4` length and `+0xd6` pointer and the JoinAdapter
  `setAssocRSNIE(pointer, length)` path.

## Local alignment

- `getPROTMODE` no longer publishes a synthetic zero carrier or raw `6` when
  not associated; it returns the Apple unsupported status directly.
- Public `setRSN_IE` is now a success no-op.
- Local public and hidden association paths keep the existing RSN IE handoff
  through a private helper that copies only the bounded caller-provided IE
  length into the net80211 override cache. This models the hidden join
  pointer+length carrier without making public `setRSN_IE` a state producer.
- `getRSN_IE` remains owner-backed and returns the cached association RSN IE
  when the local owner has one, matching the wrapper shape where the public
  helper reaches a vendor virtual rather than a direct no-op.

## Non-claims

- No RSN, EAPOL, PMK, key install, DHCP, link, or data success is synthesized.
- No retry, delay, fallback AKM rewrite, or deauth masking is added.
