# CR-479: Raw BPF TX false-success quarantine

Date: 2026-07-11

## Scope

This closure covers only the local raw BPF output callbacks. It does not
claim that the Intel backend implements Apple action-frame injection.

## Reference boundary

The recovered 25C56 Apple WCL owner is
`AppleBCMWLANNetAdapter::sendActionFrame` /
`AppleBCMWLANNetAdapter::sendActionFrameV2`. The core calls one of those
owners with the supplied peer address, channel, category, and payload through
the `actframe` transport. That is a Broadcom firmware-owner operation, not a
cache update or a generic success result.

The local Intel port has no local Intel firmware action-frame injector for
these BPF callbacks. It must therefore not acknowledge a frame after freeing
the only mbuf that carries it.

## Local correction

Before this correction, `AirportItlwm::outputActionFrame` and
`AirportItlwm::bpfOutput80211Radio` both logged their input, called
`mbuf_freem(m)`, and returned `0`. The caller observed a successful injection
even though no frame reached `ic_mgtq`, an Intel TX ring, or firmware.

Both callbacks now return `kIOReturnOutputDropped` after freeing the mbuf.
This matches the existing local `outputRaw80211Packet` disposition and makes
the absence of an injection backend observable to the caller. It does not
fabricate an `actframe` completion, action-frame progress, off-channel
operation, peer state, or a Broadcom firmware-generation branch.

## Deterministic guard

`scripts/raw_bpf_tx_quarantine_report.py --check` verifies that all three
local raw TX entry points free the mbuf and report `kIOReturnOutputDropped`,
and that the BPF dispatcher still routes the action/radio kinds explicitly.

## Candidate validation

The exact code candidate was built against Tahoe BootKC and loaded after a
full AuxKC replacement/reboot as UUID
`A76B77AD-F7D7-3263-9F3C-7AEAD979651F` (CDHash
`84030df17d7141385b3bbf97669d8cf479347078`). Its signed installed binary
SHA-256 was
`ee4738040e39247b034c991b8020c3816f385a95b11e1d4734dae491ca7dce26`.

The post-reboot concurrent validation completed `240/240` ping with zero
loss and a 240-second, 20.0 Mbit/s iperf3 transfer. The AP reported the guest
station authorized and associated with `tx failed: 0`; the new serial slice
