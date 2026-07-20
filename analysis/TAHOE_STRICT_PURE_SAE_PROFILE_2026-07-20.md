# Tahoe strict pure-SAE profile fact

## Scope

This layer classifies the BSS already selected by net80211 as a strict
normal-SAE profile before legacy RSN negotiation mutates that node. The result
is a one-byte field in the writer-only selected-BSS snapshot. It is not an
association authorization: no Tahoe ingress consumes it, pure WPA3 remains
rejected before Open-System authentication, and there is still no Algorithm 3,
SAE relay selector, PMF/IGTK, or RSN output change.

## Required AP facts

The classifier accepts only a completed census of one RSN protocol (not
WPA+RSN transition), exactly one normal SAE AKM, an infrastructure ESS (never
IBSS), Privacy capability, a pairwise-enabled RSN, exactly one pairwise CCMP
suite, group CCMP, BIP group management, and both MFPC and MFPR. An omitted
final Group Management Cipher Suite remains valid here: RSN semantics and the
existing parser default it to BIP-CMAC-128. It rejects every SAE scan fact
except the census-complete marker, an otherwise plain RSNXE H2E capability,
and an otherwise plain ExtCap marker. That excludes Password Identifier,
SAE-PK, H2E-only, malformed/unmodeled data, ambiguous AKM/cipher lists,
legacy WPA presence, extended-SAE, and all future unmodeled facts.

## Parser fidelity

The RSN parser only preserves an OR-ed pairwise cipher set, so the scan
boundary retains a passive fact when its advertised pairwise count is not one.
It also records a partial optional RSN field or trailing bytes after the final
Group Management Cipher Suite without changing the parser's legacy return
status. A short vendor IE, raw Microsoft WPA type-1 presence, duplicate WPA
IE, and both extended-SAE AKM types (24 and 25) are likewise retained as
fail-closed facts. Finally, `CENSUS_COMPLETE` is set only after all bounded
scan facts have been normalized and finalized; a hand-built or uncensused node
therefore cannot resemble a plain HnP BSS whose optional SAE IEs are absent.

## Lifetime and verification

The fact is calculated only after the actual BSS is copied into `ic_bss` and
before `ieee80211_choose_rsnparams()` intersects fields with local legacy
configuration. A replacement owner token and leaf lock allow that post-copy
writer to publish only if no ordinary cancellation won the race. Portable tests
cover every mandatory field and rejected fact; existing quarantine and Tahoe
BootKC gates prove no active SAE/PMF behavior was added.
