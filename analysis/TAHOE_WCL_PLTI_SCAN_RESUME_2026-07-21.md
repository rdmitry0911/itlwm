# Tahoe WCL external-PMK scan-resume test record

Date: 2026-07-21

## Scope

This layer repairs one bounded ordering gap in the Tahoe hidden WCL
association path.  A WCL association can configure its selected ESS after the
scan which populated the BSS tree has already completed.  Once the paired PLTI
wait has observed external-PMK readiness, the driver now requests the ordinary
`SCAN -> SCAN` net80211 edge.  It does not select a BSS, enter authentication,
enqueue a management frame, or fabricate an association result.

The resume predicate is deliberately pure and requires all of the following:

1. the existing exact local PSK/PMK policy allows the carrier;
2. association configuration returned success;
3. the paired PLTI wait observed PMK readiness;
4. net80211 is still in `SCAN`;
5. `IEEE80211_F_PSK` remains set; and
6. external PMK ownership has been released.

The readiness observation is not represented as a generation-atomic proof at
the later state request.  A concurrent lifecycle reset can therefore at most
cause a normal extra scan; the rechecked state/PSK/ownership facts prevent this
layer from directly creating authentication or management traffic.  A
generation-atomic acknowledgement, if needed, remains a separate future
layer.

## Successful checked scenarios

The committed host unit and source-contract checks passed the following
scenarios without using an SSID, BSSID, PMK, password, address, route, or raw
runtime log:

- one synthetic complete audited PSK handoff admits exactly one normal
  scan-resume request;
- each individual predicate failure rejects the request: policy, association
  result, PMK-ready observation, net80211 state, PSK eligibility, and retained
  external ownership;
- pure SAE remains reject-only, while the pre-existing audited
  SAE-plus-WPA2-PSK transition carrier remains subject to its exact existing
  policy;
- `associateSSID` resets the optional caller-local readiness output before all
  early exits and writes it only from `waitForExternalPmkReady`;
- the repair block has exactly one `ieee80211_new_state(..., SCAN, -1)` and
  contains no direct BSS selection, AUTH state request, IWX AUTH call, or
  management-frame send;
- IWX retains its existing `SCAN -> SCAN` behavior: an active scan is
  preserved and an inactive scan follows the normal next-scan path; and
- the Apple AUTO_JOIN empty-ESS hold remains before ordinary BSS selection.

The full isolated Tahoe build-admission run also passed.  It built the kext,
Agent, and RegDiag client against the pinned guest BootKC, without installing,
loading, rebooting, publishing, or releasing a bundle.  The guest was an
external disposable overlay; no physical validation host was contacted.

## Verification boundary

The reproducible commands that passed for this staged candidate are:

1. `scripts/test_payload_builders.sh`;
2. `scripts/test_tahoe_wcl_plti_scan_resume_contract.sh`;
3. `scripts/test_tahoe_sae_quarantine_contract.sh`; and
4. `scripts/run_tahoe_sae_quarantine_layer.sh`.

Runtime candidate activation and the four-cycle A2DF baseline-control scenario
remain pending after this source/build record.  This document does not claim a
working connection, data-plane result, pure SAE/PMF support, a physical-host
test, or a release activation.  Any successful runtime scenario will be added
as a separate sanitized, committed record with only candidate hashes and
aggregate verdicts.
