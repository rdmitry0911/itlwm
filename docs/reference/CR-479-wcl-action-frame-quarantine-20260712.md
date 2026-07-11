# CR-479: WCL action-frame false-success quarantine

Date: 2026-07-12

## Reference contract

AppleBCMWLANInfraProtocol::setWCL_ACTION_FRAME forwards the carrier to
AppleBCMWLANCore::setWCL_ACTION_FRAME. The core selects
AppleBCMWLANNetAdapter::sendActionFrame or
AppleBCMWLANNetAdapter::sendActionFrameV2 by firmware generation and sends
the peer, channel, category, and frame bytes through the firmware actframe
transport.

The reference success result therefore represents a submitted action frame,
not merely a copied input carrier or a locally completed command.

## Local divergence

The Intel port has no local Intel firmware action-frame injector. Before this
correction, TahoeActionFrameOwner::apply and the legacy
TahoeCommander::runSetWCLActionFrame validated and copied the frame into an
owner cache, recorded a synthetic completion, and returned success. The active
V2 commander then accepted that result as permission to report selector 620
as dispatched even though neither an Intel TX ring nor firmware received an
actframe request.

## Local correction

After preserving null and payload-size validation, both local owner/commander
entry points return TahoeErrorMap::kAppleUnsupported (0xe00002c7) before mutating action-frame state
or completing the async context. The Skywalk setter already propagates a
non-success status before copying the owner cache, and the V2 commander already
propagates a non-success owner result before synthetic transport dispatch.

This does not claim that Apple returns 0xe00002c7; Apple has the
real actframe capability. It narrows the local mismatch by removing a
user-visible success that promised a firmware action not performed by this
hardware/backend.

## Deterministic guard

scripts/wcl_action_frame_quarantine_report.py --check requires both local
owners to validate input and return TahoeErrorMap::kAppleUnsupported without action
cache mutation or synthetic completion. It also verifies that the active V2
and Skywalk routes propagate that failure before their dispatch/cache paths.

## Candidate validation

The signed Tahoe candidate loaded after a full AuxKC replacement/reboot as
UUID 4620E796-3501-3928-AA51-92868783D2EF, CDHash
8b8ee12f13f2829d7822956db934f6d726759809, and installed binary SHA-256
454c4f0eebedfee9b9cc96fbc7b896aa5ce59237fea1c624f058a997ee128333.

On the controlled lab AP, its fresh concurrent run completed 240/240 ping
with zero loss and a 240-second iperf3 transfer of 572 MBytes at 20.0
Mbits/sec. The AP ended authorized and associated with tx failed: 0. The
stress-window serial slice contained no panic, backtrace, CoreCapture, or
NoCTL event. An earlier post-reboot network selection delay was recovered by
explicitly selecting the same lab AP and is not counted as the valid run.
