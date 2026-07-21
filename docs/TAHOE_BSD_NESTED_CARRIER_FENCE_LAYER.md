# Tahoe BSD nested-carrier fence layer

This document records the static regression scenario for Tahoe BSD
Apple80211 ingress.  A BSD callback has only the outer `apple80211req`
marshalled.  It must not hand an unadmitted nested `req_data` carrier to a
local helper which reads or writes a full legacy structure.

Run the whole closed layer with:

```sh
scripts/run_tahoe_bsd_nested_carrier_fence_layer.sh --static-only
```

The runner is source-only.  It makes no Apple80211 ioctl, radio, association,
profile, address, route, DHCP, or packet operation.  A PASS therefore proves
the listed source contracts together; it is not a raw-BSD runtime experiment.

## Covered boundary classes

The one-pass layer covers the dynamic scan/current-network result carriers,
fixed and variable GET result carriers, public GET-to-legacy-size adapters,
and SET carriers whose local helper would otherwise consume a nested input.
It also covers the specifically unsupported IE and WOW_TEST SET helpers.
Each underlying contract asserts its selector, Tahoe guard, ordering before
the local dispatcher, and the appropriate family delegation or local
preflight rule.

The scan-result contract is a regression fence for the previously observed
SMAP signature: an obsolete non-canonical binary reached the local
scan-result serializer with an unadmitted nested BSD carrier.  Current Tahoe
source rejects that local path before it accesses the nested carrier and lets
the family transport own the raw request.  The adjacent current-network
result carrier is likewise delegated before its local serializer.

## Successful static scenario

The complete 17-contract layer passed on the clean Tahoe source tree before
this runner was added, and is required to pass again after every change to a
covered bridge or helper.  Individual PASS output is intentionally
categorical and contains no wireless identity, credential, address, route,
or raw packet data.

## Deliberate non-claims

This layer does not execute a raw setter or getter and does not prove generic
user-pointer rejection, Apple return-code parity, scan functionality,
association, SAE, PMF, EAPOL, IGTK, link publication, or physical-host
behavior.  It does not classify unrelated selectors such as SCAN_REQ; such a
selector needs a separate ownership analysis before any routing change.
