# CR-479 — WCL_ROAM_PROFILE_CONFIG quarantine and functional closure

Dates: 2026-07-14, 2026-08-01

## Historical correction

The 2026-07-14 correction removed a false-success implementation of
`AirportItlwmSkywalkInterface::setWCL_ROAM_PROFILE_CONFIG`. The old code copied
an inferred 0x23c-byte opaque carrier to an unread cache and returned success.
At that point the local port had no matching policy consumer, so non-null input
was quarantined as unsupported while the reference carrier lifecycle was
recovered.

The 2026-08-01 closure replaces that quarantine with a host-side Intel roaming
owner. It does not emulate Broadcom Commander: it decodes the recovered public
policy fields and applies their user-visible effects to the shared net80211
autonomous STA scan and candidate-selection paths used by IWN, IWM, and IWX.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_ROAM_PROFILE_CONFIG` at `0x100018b74`
  dispatches through virtual `+0x6d8` to Core `0x100141e10`.
- The null route reaches `0x1001a01a0`, which stores `0xe00002bc`.
- A non-null request selects RoamAdapter at `+0x15c0` and tail-jumps to
  `setROAM_PROFILE_CONFIG` at `0x10001c3f8`.
- The owner reads three 0xb8-byte per-band records beginning at `+0x0`,
  `+0xb8`, and `+0x170`, then calls `setRoamingProfileV6` `0x10001bfca` with
  band identities 2, 1, and 4.
- Each band begins with a u32 valid/version word followed by three 0x3c-byte
  RSSI brackets. Recovered bracket consumers are flags `+0x0`, signed trigger
  `+0x4`, signed lower bound `+0x6`, backoff `+0x8`, full-scan period `+0xa`,
  initial-scan period `+0xc`, NF-scan count `+0xe`, maximum-scan period
  `+0x10`, roam delta `+0x24`, and 2/5/6 GHz boost threshold/delta pairs at
  `+0x26/+0x28`, `+0x2e/+0x30`, and `+0x36/+0x38`.
- The tail contains Multi-AP environment at `+0x230` and join-preference flags
  at `+0x238`. Bit 1 drives `disable6GForRoamScans` `0x10001c5b0`, including
  the `join_pref` Commander route and `disable6GForRoamScansCallback`
  `0x10001de02` or synchronous `runIOVarSet` `0x10017b6e6` path.
- The same lifecycle calls `applyRoamingCandidateBoost` `0x10001c6ba` and
  `configureMultiAPBit` `0x10001c322`. `setRoamingProfileV6` builds the
  `roam_prof` request, uses `sendIOVarSet` `0x10017b900`, and installs
  `handleRoamProfileAsyncCallBack` `0x10001bd9a`.

The recovered consumer map establishes every field used by the local
functional bridge. Bytes without a recovered consumer remain reserved; this is
not a claim that every byte of the 0x23c carrier has a semantic name.

## Intel policy mapping

The setter retains the direct null error, rejects malformed active brackets,
and reports not-ready until an Intel controller exists. A valid carrier is
published atomically through a small generation seqlock. The common STA path
then applies:

- the current band and normalized RSSI to choose the active 2/5/6 GHz bracket;
- trigger/lower bounds to arm or suppress autonomous background scanning;
- initial period, backoff multiplier, and maximum period to schedule retries;
- roam delta plus the recovered target-band threshold/delta pairs to accept or
  reject an autonomous candidate;
- the legacy fixed OpenBSD RSSI threshold only when no modern policy exists.

IWN, IWM, and IWX all normalize received RSSI as `dBm + 100`; the host policy
keeps that bias explicit. WNM BSS-transition steering remains protected by its
own exact target and is intentionally not rejected by the autonomous-candidate
delta gate.

The carrier's full-scan period and NF-scan count are retained in the policy,
but Intel exposes one full background-scan primitive rather than Broadcom's
firmware `roam_prof` scan classes. Consequently they do not claim distinct
firmware scheduling semantics. Multi-AP and join-preference words are likewise
retained for later Intel-specific consumers. Physical IWN hardware has no 6 GHz
band; the shared mapping is active for a 6 GHz-capable backend by frequency.

## Runtime evidence

The Release candidate loaded in the Tahoe guest as
`com.zxystd.AirportItlwm 2.4.0`, UUID
`8CA541DE-EE9F-351A-B6C5-BC509304E9A5`, binary SHA-256
`082e92f707f8618d407e264eb932fdcc073c1ca98cae9753e15afa0ee084a915`.
The generated AuxiliaryKernelExtensions collection SHA-256 was
`6fe8d1d331561d7402f7c9bd99bc820dd5c279b74d089798441b944dea184c9a`.

Focused DTrace observed Tahoe call the modern setter repeatedly and publish a
three-band policy with valid mask 7, bracket counts 2/2/1, RSSI bias 100,
Multi-AP environment 11, and join flags 4. The live 2.4 GHz bracket decoded to
trigger -10 dBm, lower bound -75 dBm, backoff 2, full period 120 seconds,
initial period 20 seconds, NF-scan count 2, maximum period 90 seconds, and roam
delta 16 dB. The common receive path then returned the configured 20000 ms
scan delay 1288 times.

During the observation the guest remained associated on channel 13 using WPA3
Personal at -43/-44 dBm, retained DHCP address `172.16.66.120`, passed 35/35
source-bound packets before the final rebuild, then 20/20 after reboot and
25/25 after a final radio OFF/ON cycle, and reported no Wi-Fi fault or recovery.
The separately implemented roam lock was observed returning unlocked. This is
direct runtime proof of carrier admission, policy publication, scan-delay
consumption, and non-regression of the encrypted data path; it does not claim
that a better candidate was present during that bounded interval.

## Remaining boundary

No Broadcom `roam_prof`, `join_pref`, multi-AP IOVAR, Commander transport, or
synthetic callback is introduced. The host-side mapping provides the matching
user-visible Intel policy owner, not binary or transport identity with the
Broadcom backend. Legacy `setWCL_LEGACY_ROAM_PROFILE_CONFIG` remains a separate
unsupported path, as do generic STA `ROAM_PROFILE`, unrelated reassociation,
key, link, WCL-event, and adaptive-roaming property surfaces.
