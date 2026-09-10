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
