# CR-604 — Tahoe companion fixed SETs: LDPC quarantine

## Decision

For Tahoe's shared BSD `SIOCSA80211` ingress, preserve the recovered fixed
`0xe082280e` status for the following public selectors:

- `RADIO_INFO`, `MIMO_POWERSAVE`, `RIFS`, `MSDU`, `MPDU`, `BLOCK_ACK`;
- `PLS`, `PSMP`, `PHY_SUB_MODE`;
- `CACHE_THRESH_BCAST`, `CACHE_THRESH_DIRECT`, `40MHZ_INTOLERANT`, and
  `PID_LOCK`.

Do not terminalize `APPLE80211_IOC_LDPC` at that ingress.  The reference's
public leaf wrapper is not sufficient evidence that the shared BSD dispatcher
can return the same status before the normal association path has used the
selector.

## Laboratory isolation

All candidates were signed, built into an explicit AuxKC, and rebooted only in
the Tahoe laboratory guest.  The remote validation machine was not contacted
or changed.

| Candidate | Result after a normal Wi-Fi OFF/ON and keychain join |
| --- | --- |
| First three (`RADIO_INFO`, `MIMO_POWERSAVE`, `RIFS`) | associated; traffic passed |
| `LDPC` only | API calls succeeded but the AP received no AUTH/ASSOC frames |
| `LDPC` plus `MSDU` | same association failure |
| All fourteen companion selectors | same association failure |
| All thirteen above excluding `LDPC` | four strict OFF/ON cycles passed |

The successful thirteen-selector candidate used the 5 GHz laboratory AP on
channel 153 / VHT80.  Each counted cycle had no AP-side cleanup: the peer
disappeared after power-off, then reappeared as `[AUTH][ASSOC][AUTHORIZED]`
after the ordinary join.  Each cycle sent five ICMP packets from `10.77.0.47`
to the laboratory-only `10.77.0.1`; all packets were received.  The default
route remained `en0`, while the laboratory target remained on `en1`.

## Scope

This is a narrow compatibility correction, not a claim about an undocumented
LDPC configuration implementation.  `LDPC` remains quarantined until a
separate ingress-safe implementation is demonstrated.
