# CR-479 WCL association status and JoinAdapter completion contract

Date: 2026-07-25

## Corrected scope

The earlier version of this note conflated two separate Tahoe WCL messages.
That conclusion is withdrawn.  A successful association has an ordered
two-carrier boundary:

1. `AppleBCMWLANCore::handleAssocEvent(...)` publishes generic association
   status on selector `0x4e`, with an eight-byte `{ status, reason }` payload.
2. `AppleBCMWLANJoinAdapter::handleAssoc(...)` subsequently publishes the
   JoinManager completion on selector `0xd3`, with a `0x1c`-byte,
   candidate-specific auth/association result payload.

`0x4e` is not the `WCLJoinManager` auth/association completion.  It remains a
generic status bulletin and must precede, rather than substitute for, `0xd3`.

## Reference contract

In the Tahoe 25C56 reference, `handleAssocEvent` maps the firmware status and
reason values, then posts `0x4e` with length `0x08`.  It next continues through
the extended-event path and `AppleBCMWLANJoinAdapter::handleAssoc`.  The
JoinAdapter keeps a ledger for the selected candidate and posts `0xd3` only
after its authentication and association records are coherent.

`WCLJoinManager::authAssocCompleteEventHandler` accepts the `0xd3` body only
when it is present and exactly `0x1c` bytes.  Its packed layout is:

| Offset | Field | Success value |
| --- | --- | --- |
| `0x00` | `uint16_t status` | `0` |
| `0x02` | `uint16_t secondary_state` | `0xffff` |
| `0x04` | `uint8_t auth_seen` | `1` |
| `0x05` | selected-candidate BSSID | selected BSSID |
| `0x0b` | reserved byte | `0` |
| `0x0c` | `uint32_t auth_status` | `0` |
| `0x10` | `uint32_t auth_reason` | `0` |
| `0x14` | `uint32_t assoc_status` | `0` |
| `0x18` | `uint32_t assoc_reason` | `0` |

The separate generic `0x4e` body is still exactly two 32-bit values:
`status` at `+0x00` and `reason` at `+0x04`.

## Local implementation boundary

`IEEE80211_EVT_STA_ASSOC_DONE` first sends the zero-success `0x4e` status
bulletin for every ordinary successful local association.  It then attempts
the stricter `0xd3` path.  The latter is deliberately unavailable unless all
of these gates hold under the controller command gate:

- net80211 is still in `S_ASSOC` and has a current BSS;
- the association epoch and copied selected-BSS snapshot still match;
- the request belongs to an active, armed WCL association owner;
- the owner has not already published a completion;
- the selected SSID and BSSID match both the owner and its selected WCL
  candidate.

The gate claims the completion before posting `0xd3`, making the path
one-shot.  Deauthentication and WCL lifecycle replacement/abort/reassociate
edges clear the lease.  Therefore a public association, stale scan epoch,
same-BSS reassociation, alternate candidate, failure, retry, or cancellation
cannot manufacture a JoinManager completion.

## Validation and non-claims

The layout is covered by the standalone payload-builder test, and the source
order/gating contract is covered by the WCL auth/association completion static
test.  Runtime validation must independently show the ordered `0x4e` then
`0xd3` sequence on an active WCL candidate before claiming a completed user
visible join path.

This record makes no claim about WPA3/SAE, roaming, public CoreWLAN identity
surfaces, or an external network identity.  Those remain separate functional
and runtime questions.
