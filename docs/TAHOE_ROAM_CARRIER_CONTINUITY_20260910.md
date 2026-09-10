# Same-ESS roaming carrier continuity — 2026-09-10

## FIX_CANDIDATE

The published image reproducibly reports media inactive and withdraws IPv4
on each successful intra-ESS roam. Four consecutive current-boot transitions
and a persistent-socket control establish the observable defect; see
[the data-path investigation](TAHOE_ROAM_POLICY_DATA_PATH_20260910.md).
The earlier permanent ARP/DHCP failure is a separate unclosed observation.

Route: REUSE_REFERENCE_DECOMP for the observable carrier lifetime, with local
net80211 association-epoch ownership. This is not an attempt to reproduce
Broadcom firmware storage or bypass Intel authentication and key admission.

The exact 25C56 WCLRoamManager FSM has six states, ten events and eleven
actions. The complete static matrix at `0xffffff80023dc600`, action table at
`0xffffff80023dc678`, names and CommonFsmManager dispatcher were read from the
matching saved BootKC. The dispatcher commits the next state, then invokes
the selected action. Roam scan/preparation/reassociation/completion do not
enter LINK_DOWN; an actual LINK_DOWN event does, invoking `linkDown` from
every connected, roaming or sleep state. Protection timeout returns roaming
states to LINK_UP without manufacturing a link-loss action. This WCL timer
is not a replacement for net80211 authentication/key-response timeouts.

The additional 165-function batch used 40 actual decompiler interfaces.
Its manifest SHA-256 is
`6c29fdeca30ec9ff6e09911f8d3bdc082654bd0f74b4928b09f0978d288ff4f3`;
the complete 60-cell matrix dump SHA-256 is
`ecf279ed19214d5b2403b2fea2fe81eebe1df139cff5cc46e2e162ae7a3cd0e4`.
False no-return annotations for IOMallocTypeImpl and IOLog were corrected in
the read-only session; the saved project and original binary were unchanged.
The incomplete parent Infra C output remains excluded from completeness
claims. The explicit WCL link-update producer/consumer and local completion
path retain their independent current-BSS refresh contract.

### Implementation contract

- Only an admitted direct replacement from an already live STA to a distinct
  BSSID with the same nonempty SSID and compatible security can reserve the
  logical link. Protected source ports must already be authorized.
- Bind that reservation to the immediate replacement association epoch.
  Preflight/backend failure, epoch replacement and INIT/SCAN/retry transitions
  cannot carry it forward.
- Omit only the common state machine's synthetic down edge during the exact
  RUN-to-AUTH-to-ASSOC-to-RUN progression. Explicit real link-down calls are
  unchanged. Packet authorization, key teardown and management timers remain
  unchanged; retaining a logical network does not authorize target data.
- A port-valid edge before asynchronous IWX RUN must not retire continuity
  early. Successful RUN/open or RUN/authorized-port completes the reservation.
- Existing WCL authentication, association, key and current-BSS completion
  publications remain active. Runtime must independently verify the new BSSID
  in Apple's consumer as well as stable address and traffic; retained old-BSS
  metadata is not a pass.

### Qualification gates

Compile actual production eligibility/ownership helpers and the complete BSS
replacement function under ASan/UBSan, with an unchanged-source negative
control. Cover open/RSN, security mismatch, same/different ESS and BSSID,
unauthorized ports, epoch wrap/replacement, preflight/backend failure,
retries, INIT/SCAN, real link down and pre-RUN port validity. Run the full
payload suite and three-family adjacent contracts, build with Tahoe symbol
verification, then load only in the owned disposable guest.

Repeat directed and unrestricted real WPA3 roams under persistent TCP/UDP,
requiring no media-inactive/address-removal event, a refreshed actual BSSID,
and continued traffic. Exercise genuine failure/disconnect and GUI profile
switch/off-on, followed by sleep and AP regressions. Publication requires
loaded-image evidence; source tests alone do not close this defect or grant
IWM/IWX hardware qualification.

## Implemented candidate and source verification

The common replacement path now captures eligible source identity before old
TX/key/node teardown and binds continuity to only the immediately following
nonzero epoch, including wrap. The common state-machine down edge is omitted
only for that lease's three forward transitions. Explicit link-state calls
remain effective, and a successful pre-RUN port-valid callback does not retire
the lease before IWX commits RUN. Candidate-binding and preflight rejection
retire it immediately. IWN synchronous AUTH/RUN failures and both asynchronous
IWM/IWX worker failures also terminate their exact lease before ordinary
recovery proceeds. No WCL carrier, security gate, timer or packet owner was
replaced or suppressed.

The focused ASan/UBSan test compiles the complete production eligibility,
reservation, failure, progression, terminal and link-state bridge functions,
plus the complete production `ieee80211_node_join_bss`. Seventy-one cases
cover the declared boundaries. Backend/state/crypto fixtures model calls and
field values, not hardware or the full kernel ABI. The production common
newstate wiring and all three backend failure sites are separately checked.
The unchanged `cc319af2` BSS replacement function compiles with the same
fixture and fails the retained-epoch assertion (exit 134), not compilation.
The existing complete TX-teardown regression still passes.

The full payload suite and adjacent SAE request, association comeback,
initial-BSSID, WCL reassoc, IWN/IWM/IWX beacon-loss, IWN BTM, link-context and
three-family SAE ownership contracts passed. Build, exact-image activation,
current-BSS refresh, continuous-IP runtime, genuine-failure and sleep/AP
regressions remain required. This candidate is not a published runtime fix.

## Loaded candidate and first continuous-address transitions

Source `c19ab0db` built with all 1085 external symbols resolved. Private
five-member AuxKC preflight and transactional activation preserved the four
companion members. A normal disposable-guest reboot at 10:27:04 UTC returned
SSH by 10:27:44. Boot session `9BC5E960-E1B4-4A2E-9BD2-D4B4986CDF7B`
loaded UUID `74F8A32B-57C3-3D50-8DA9-E795D9D35EBF`, matching Mach-O
SHA-256 `d28297b766394c40b286f3f91d575145034ad3cc8f8afdef50bf21727366eb61`.
The initial pure-WPA3/required-PMF association on channel 13 reached link up
at 10:27:39 and DHCP BOUND/address publication at 10:27:41.

Airportd's automatic best-connected transition completed on the other BSS,
channel 9, at 10:28:14. Four subsequent unrestricted framework requests
alternated channel 13/9/13/9, with actual ROAMED events at 10:28:54,
10:29:19, 10:29:49 and 10:30:14. Each explicit transition passed its separate
three-packet, 1400-byte check. No radio toggle or explicit fresh join
intervened. Exact-current-boot airportd/configd logs contain no media-inactive,
IPv4 withdrawal or repeated DHCP BOUND/address publication across these five
completed transitions. Initial boot's pre-association inactive events are not
misclassified as roam events.

Native airportd received both ROAMED and BSSID_CHANGED and updated its
associated-network channel, security, RSSI and per-BSS association record
at every transition. The original network association timestamp remained
unchanged. Lower-driver BSSID readback matched the two actual channel-specific
targets. Thus continuity did not simply leave Apple's consumer describing the
old AP. Privacy-redacted native BSSID strings and absent direct scutil BSSID
keys are not used as independent raw-BSSID proofs.

One 150-second client opened exactly one connected TCP socket and one UDP
socket, bound to the STA address; neither was reopened or reconnected. Its
10:28:34--10:31:04 interval covered all four explicit transitions. TCP sent
and received exactly 6,665,000 bytes with no pending output. The peer accepted
TCP once and saw EOF only at normal client completion. UDP sent 6,720,000
bytes and received 6,347,000 echo bytes. There were 24 ENOBUFS UDP sends and
79 EAGAIN TCP sends, but no EADDRNOTAVAIL. Both processes ended normally.
The roughly 5.55% missing UDP echo bytes and queue errors remain failures of
lossless service, not a seamless/latency/throughput pass. This path contains
both the guest radio and the host's separate Wi-Fi hop.

The complete candidate serial interval through this run has no matched
firmware fatal, device timeout, driver panic, unset-key diagnostic or AP TX
gate error. This is first loaded evidence for continuous logical address,
not closure of the earlier persistent post-roam ARP/DHCP failure or IWM/IWX
hardware qualification. The published image remains `964a90b3`.

## Real GUI off/on negative control

The current-boot GUI was independently visible and usable. A System Settings
Wi-Fi off click at 10:38:24 UTC was followed by power Off, inactive carrier
and absent IPv4 at 10:38:27. Thus a real radio-off operation still withdraws
the logical network; the roam reservation does not suppress ordinary down.
GUI on at 10:38:44 restored the saved WPA3 link and its address by 10:38:57.
The subsequent 1400-byte forward check passed 20/20. A separately awaited
host-Wi-Fi-to-guest check returned 18/20, not a bidirectional zero-loss pass.
No second off/on was used. Exact event correlation and the iperf reproduction
remain pending at this checkpoint, followed by genuine failed-target, profile,
sleep and AP regressions before publication.

## Iperf offered-stream reproduction and scan boundaries

A first 150-second, 2-Mbit/s iperf UDP run at 10:40:14 UTC offered all
31,250 datagrams; the receiver lost 47 (0.15%). Its directed request at
10:40:35 reached a physical scan but found no eligible target. The next
request named the still-current source BSSID and was rejected. Its controller
stopped, as required. A later unrestricted request at 10:42:42 completed the
transition at 10:42:48, after that stream had ended. Therefore this run is
not used as an iperf-across-roam qualification.

The second independent 150-second run started at 10:43:36. Four unrestricted
requests were accepted. The first completed without an eligible target and
the second was superseded by another foreground WCL request. The third and
fourth produced real ROAMED/BSSID_CHANGED at 10:45:03 and 10:45:23, respectively,
with native associated-network channels changing from 9 to 13 and back to 9.
The third request's fixed-delay packet check started before completion and
returned only 1/3; the fourth's post-transition check passed 3/3. Request
acceptance and ping's zero process exit are not substituted for those counts.

Iperf continued offering traffic in every five-second interval through both
actual transitions and to its normal terminal. It offered 31,250 datagrams
and the receiver lost 893 (2.9%). Both processes ended successfully. Unlike
the published-image reproduction, the sender did not stop offering after
the first transition. Native IPConfiguration recorded no media-inactive,
address withdrawal or DHCP BOUND during these two completed transitions.
This qualifies the offered-stream/address boundary, not lossless roaming;
the no-target/superseded scans and packet losses remain separately visible.

## Saved-profile security changes through GUI

System Settings selected the saved external OpenWrt WPA2 profile at
10:47:57 UTC, without another radio toggle. The old WPA3 address was removed
at 10:48:00.783; native logs identify the new association as WPA2-PSK/CCMP,
without PMF, on channel 161 at about -33 dBm. A fixed 12-second readback
preceded DHCP completion and failed to obtain an address; that early probe
is retained as failed. DHCP BOUND followed at 10:48:10.982. A later actual
lease readback and independently awaited 1400-byte forward/reverse checks
passed 20/20 each, without reselection or recovery operations.

GUI selection of the saved pure-WPA3 profile at 10:52:20 removed the WPA2
address at 10:52:21.108, associated on channel 13 and reached DHCP BOUND at
10:52:26.222. The actual restored lease and separate 20/20 forward and 20/20
reverse checks passed. Thus the continuity correction preserves ordinary
address teardown for a different network/security policy in both directions;
it does not carry the old profile's address or authorization into that join.
This does not close the new-profile/open, failed-target, S3 or AP gates for
the candidate, nor last-selected-profile preference after sleep/off-on.
