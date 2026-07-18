# CR-601 — Tahoe public LAST_RX_PKT_DATA BSD SET fixed-stub alignment

## Scope

This layer changes only the canonical normal, non-null Tahoe BSD SIOCSA80211
route for APPLE80211_IOC_LAST_RX_PKT_DATA (IOC 53). It returns the exact
reference fixed status before observing the caller carrier. The independently
recovered GET status remains covered by CR-556 and is not otherwise changed.

## 25C56 reference evidence

The laboratory guest BootKernelExtensions.kc SHA-256 is
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
Within its IO80211Family fileset image,
`apple80211setLAST_RX_PKT_DATA(IO80211SkywalkInterface*, void*)` is nlist
7684 at VM `0xffffff80021c3c5e` (file offset `0x020c3c5e`). The next
external symbol is `apple80211setRADIO_INFO` at
`0xffffff80021c3c69`, so the complete 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It returns `0xe082280e` and reads neither public argument nor carrier. The
direct read-only recovery record and digest are retained in
`artifacts/skywalk-last-rx-pkt-data-set-public-fixed-stub-bootkc-25c56/`.

## Change and boundary

The Tahoe dispatcher accepts only its existing normalized GET and SET
directions for IOC 53 and returns that fixed raw status after the existing
carrier-null guard. This is not a success acknowledgement and does not
construct, read, or activate a local LAST_RX_PKT_DATA carrier contract.

The outer-null and carrier-null boundaries, unknown-command fallback, pre-26
behavior, GET evidence and semantics, last-received-packet behavior, V1,
Virtual IOCTL, controller/card-specific ingress, firmware, association, radio,
traffic, and runtime selector invocation are not changed or claimed. No V1,
Virtual, or card-specific LAST_RX_PKT_DATA route is introduced.

## Verification

`scripts/test_tahoe_last_rx_pkt_data_set_contract.sh` verifies the independent
SET raw record and manifest, Tahoe-only canonical SET branch, existing null
boundaries, absent V1/Virtual/V2/card ingress routes, and the BSD normalization
bridge. The existing GET-only report continues to verify its independent raw
record and GET evidence.
