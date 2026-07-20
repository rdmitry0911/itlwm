# Tahoe selected-STA authentication status

## Scope

This layer makes an already received authentication failure observable without
adding SAE, changing Open-System authentication, or publishing a new callback.
It is diagnostic and cancellation hygiene only: the driver still transmits and
accepts only Open-System authentication, and pure SAE remains quarantined.

## Provenance before status

`ieee80211_record_sta_auth_failure()` writes `ic_assoc_status` only when the
frame is the selected STA BSS's expected response to the active Open-System
attempt: STA mode, AUTH state, `ni == ic_bss`, destination equal to the local
MAC, source and BSSID equal to `ic_bss->ni_bssid`, and the Open-System response
sequence number. A foreign management frame, wrong BSSID, wrong destination,
or wrong transaction sequence cannot poison visible association status.

For that exact transaction, a nonzero AP status is preserved verbatim. A
zero-status non-Open algorithm is reported as `IEEE80211_STATUS_ALG`, rather
than looking like authentication success. The normal Open response path still
owns its existing state-machine handling.

## Epoch hygiene

When the non-Open branch recorded such a selected-STA failure, it immediately
revokes the PAE association epoch before returning. This closes the gap where
an unsupported SAE response could otherwise leave the selected-BSS snapshot
valid until a later authentication timeout. No key, PMF, IGTK, RSN-output, or
SAE authentication path is enabled.

## Verification

`scripts/test_net80211_auth_status_contract.sh` checks all provenance guards,
status mapping, no-FSM/no-key helper scope, and a foreign-frame matrix.
`scripts/test_net80211_pae_epoch_contract.sh` verifies record-then-revoke
ordering; the aggregate SAE quarantine gate runs both.
