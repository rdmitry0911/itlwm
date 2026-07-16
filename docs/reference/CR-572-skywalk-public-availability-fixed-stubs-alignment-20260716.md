# CR-572: Tahoe public AVAILABILITY fixed-stub alignment

## Scope

The 25C56 reference exposes selector 105, APPLE80211_IOC_AVAILABILITY, as
two separate public Skywalk wrappers.  This layer changes only normal
non-null Tahoe BSD GET and SET dispatch for that selector.

The evidence is
docs/reference/artifacts/skywalk-availability-public-fixed-stubs-bootkc-current/raw.txt.
It identifies these exact 11-byte leaves:

| Direction | nlist | VM range | Returned status |
| --- | ---: | --- | --- |
| GET | 7366 | 0xffffff80021bf39c..0xffffff80021bf3a7 | 0xe082280e |
| SET | 7393 | 0xffffff80021c421d..0xffffff80021c4228 | 0xe082280e |

Each body is the same unread sequence: it establishes a frame, loads
0xe082280e into the return register, then returns.  Neither body reads its
public interface nor carrier argument.

## Source contract

AirportItlwmSkywalkInterface::processApple80211Ioctl now returns that exact
status for GET and SET of APPLE80211_IOC_AVAILABILITY behind the Tahoe target
guard.  It does not allocate, inspect, or synthesize an availability carrier.

The dispatcher's existing outer request and carrier-null fallbacks remain
outside this narrow normal non-null route.  No legacy V1, virtual-interface,
card-specific, radio, association, firmware, deployment, or runtime-selector
claim is made by this layer.  Unknown commands and pre-Tahoe builds retain
their unsupported fallback.

## Verification

scripts/skywalk_public_availability_fixed_stubs_alignment_report.py verifies
the raw artifact digest, both symbol boundaries, both fixed bodies, source
guard, command directions, null-boundary placement, and absence of a
card-specific AVAILABILITY route.  The layer is then covered by the ordinary
static suite and Tahoe build gates; it does not invoke the selector at runtime.
