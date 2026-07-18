# CR-589 — Tahoe public FRAG_THRESHOLD BSD SET fixed-stub alignment (2026-07-18)

## Scope

This layer changes only the canonical normal, non-null Tahoe BSD `SIOCSA80211`
route for `APPLE80211_IOC_FRAG_THRESHOLD` (selector 31). It returns the exact
reference fixed status before inspecting the caller carrier. The independently
recovered GET status remains evidenced by CR-544 and is not otherwise changed.

## 25C56 reference evidence

The pinned guest BootKC has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
Within its `com.apple.iokit.IO80211Family` fileset image, the public setter
`apple80211setFRAG_THRESHOLD(IO80211SkywalkInterface*, void*)` is nlist 7536
at VM `0xffffff80021c3a7f` (file offset `0x020c3a7f`). Its whole 11-byte body
ends at the adjacent `apple80211setRATE_SET` symbol at
`0xffffff80021c3a8a`:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It returns `0xe082280e` and reads neither the public interface nor the carrier.
The direct raw record and its digest are retained in
`artifacts/skywalk-frag-threshold-set-public-fixed-stub-bootkc-25c56/raw.txt`.

## Change and boundary

The Tahoe BSD dispatcher now accepts only its existing normalized GET and SET
directions for IOC 31 and returns that fixed raw status after the existing
carrier-null guard. This is not a success acknowledgement and does not
construct, read, or activate the historical `apple80211_frag_threshold_data`
carrier declaration.

The `req == NULL` and carrier-null boundaries, unknown command fallback,
pre-26 behavior, GET evidence and semantics, V1, Virtual IOCTL,
controller/card-specific ingress, fragmentation-threshold state, firmware,
association, radio, traffic, and runtime selector invocation are not claimed
or changed.

## Verification

`scripts/test_tahoe_frag_threshold_set_contract.sh` verifies the independent
SET raw record and manifest, Tahoe-only canonical SET branch, existing null
boundaries, absent local carrier producer and alternate ingress routes, the
unconsumed historical header carrier, and the BSD normalization bridge. The
existing GET-only report continues to verify its own independent raw record
and GET behavior.
