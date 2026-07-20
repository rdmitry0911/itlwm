# Tahoe SAE quarantine pinned transport gate — 2026-07-20

## Successful, bounded scenario

At `2026-07-20T23:12:39Z`, the complete Tahoe SAE/PMF quarantine gate passed
for source commit `4945db13844b46da68769ef0ff66d272e5db79fa` on the pinned
QEMU laboratory guest.  Before staging source, the runner created a private
mode-0600 `known_hosts` file from its source-controlled ED25519 pin and
verified the pin fingerprint
`SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY`.

Both SSH command execution and the separate `rsync` process used that private
pin with strict host-key checking, no global known-hosts trust, no host-key
updates, and `-F /dev/null`; caller SSH configuration therefore could not
weaken the transport.  The gate also verified the guest build `25C56` and the
exact BootKC SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`
before its isolated build.

The static SAE/PMF, link-handoff, thread-call, and AuxKC contracts passed.
The isolated guest then built the Tahoe debug `AirportItlwm.kext`, resolved
all 958 undefined symbols against the pinned BootKC, clean-built the Agent,
and built the matching RegDiag client.  This is a successful
transport-pinned, ABI-provenance-pinned **build-admission** scenario.

## Explicit limits

The run did not install, load, unload, publish, release, activate, or reboot
the kext.  It did not associate to a network, change radio state, request
DHCP, change addresses or routes, ping, or transfer Wi-Fi traffic.  It did
not touch the physical host or `10.90.10.22`.  It is consequently not a
functional Wi-Fi, SAE, PMF, association, or data-path pass claim.

The machine-checkable, credential-free record is
`evidence/runtime/tahoe_sae_quarantine_transport_pin_4945db1.json`.  The
SAE quarantine contract binds both this record and the present document to
the exact committed source identity and stated non-claims.
