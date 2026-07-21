# OpenWrt mbedTLS SAE group-19 intake

This is a reproducible crypto-backend intake, not a runtime SAE switch.
`AirportItlwmAgent` and the kext do not currently compile, link, load, or
execute mbedTLS.  The intake establishes an independently repeatable Tahoe
test for the exact upstream hostap group-19 HnP and H2E known-answer vectors
through OpenWrt's mbedTLS crypto port.
This intake does not enable SAE in the kext or Agent.

Run the two gates from a Tahoe x86_64 checkout:

```sh
bash scripts/test_tahoe_openwrt_mbedtls_sae_intake_contract.sh
bash scripts/run_tahoe_openwrt_mbedtls_sae_group19_kat.sh
```

The runner creates a new temporary build directory and accepts no source URL,
network target, credential, AP, interface, route, address, load, install, or
reboot option.  It downloads only the public sources pinned by
`third_party/sae/openwrt_mbedtls_group19.lock`, verifies every SHA-256, and
applies the exact ordered OpenWrt patch series with `git am --3way`.

## Pinned provenance and license choice

- hostap source: `b004de0bf1b54d669d358b7f33d6f474bd9719a6` from
  `https://w1.fi/hostap.git` (BSD-3-Clause);
- OpenWrt source: `0f256a0a7adf5741e4a061f59a08cd01c14dc526`;
- mbedTLS source: 3.6.6 release archive, SHA-256
  `8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a`;
- mbedTLS upstream is dual `Apache-2.0 OR GPL-2.0-or-later`.  This project
  explicitly selects `GPL-2.0-or-later` for any future derived integration.

The lock contains the seven patch names and SHA-256 values.  The runner also
checks the final patched hostap content tree
`a7bb37dda84e314ff252d27814007ff6e19de529`; a mutable branch, a patch name,
or a successful download alone is not accepted as provenance.

For this KAT, the executable links static `libmbedcrypto` only.  It checks
that `otool -L` has only `/usr/lib/libSystem.B.dylib` and finds representative
mbedTLS ECP, MPI, HMAC, and SHA-256 symbols in the resulting Mach-O.  TLS and
X.509 libraries are deliberately not linked by the test harness.

## What the successful scenario proves

The checked upstream `sae_tests()` routine passes both of these vectors:

- group-19 HnP Commit, KCK, PMK, and PMKID;
- group-19 H2E PT-to-PWE x/y.

The committed evidence records the source identities, patch digest series,
static-link observation, and those two PASS results without credentials,
SSID/BSSID, MAC addresses, IP addresses, routes, packet capture, or binary
assets.

## Boundaries and next admission condition

This is not an SAE association, not a PMF/IGTK result, and not an AX211
runtime result.  It does not implement a UserClient selector, a relay FSM,
anti-clogging, replay handling, selected-BSS admission, password-id, SAE-PK,
or a group-negotiation policy.  The current V1 relay remains inactive and
fail-closed.

The full OpenWrt suite is not claimed: its broad test Makefile is not a
portable Tahoe verdict and stopped at `mkdir: illegal option -- L`.  The
direct source-verified group-19 KAT is the only recorded result here.

A product change requires a separate atomic layer: a narrowed, reviewed
mbedTLS bridge; controlled RNG and secret-redaction behavior; a producer and
consumer that convey an exact admissible profile; body-boundary and replay
tests; then controlled AP interop for HnP, H2E, and anti-clogging.  Until that
layer passes, the KAT cannot be represented as WPA3 support.
