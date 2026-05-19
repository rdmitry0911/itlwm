# iwx 802.11 Open-System auth-ACK boundary diagnostic

Bounded diagnostic-instrumentation report for the iwx 802.11
Open-System authentication ACK boundary after PMK delivery.

## Anomaly

After the project-owned PLTI PMK producer (helper-side delivery
proven by `commit-approval/runtime_evidence/CR-479-stage1-
credential-acquisition-project-keychain-contract-rev11-20260518/
30_join/01_pipeline_summary.txt`, six generations of
AgentLookupProjectPSK FOUND -> AgentDerivePMK_PBKDF2 OK ->
DeliverPMK OK), `networksetup -setairportnetwork` still returns
`Error -3912` (`com.apple.wifi.apple80211API.error error -3912`)
on every attempt. The host-AP hostapd log captured at
`commit-approval/runtime_evidence/CR-479-stage1-project-owned-
plti-pmk-producer-kext-trigger-rev5-20260518/30_join/
07_hostapd_evidence.txt` records `STA <sta-private-mac> IEEE
802.11: did not acknowledge authentication response`, followed
by `STA <sta-private-mac> IEEE 802.11: deauthenticated due to
inactivity (timer DEAUTH/REMOVE)`.

802.11 Open-System auth completes BEFORE the 4-way handshake
where the PMK would be consumed, so a valid PMK delivered by
the helper cannot rescue an STA that never finishes auth. The
PMK producer (the project-owned keychain path) is laboratory-
only PMK-delivery evidence (per active operator instruction
`op_20260518_refcred_external-analysis-agent`) and the credential UX
question is orthogonal to this auth-ACK question. The auth-ACK
gap is downstream of any credential source and must be
investigated independently.

## Four observation layers and what they actually measure

The auth-ACK boundary spans four observation layers. Each
layer is measured by a different probe. The layers do NOT
all see the same event; conflating them leads to incorrect
conclusions, so this section pins each layer to its precise
semantics.

  Layer (a): STA-side iwx tx-completion for AUTH(seq=1).
    Probe: `iwx_rx_tx_cmd_single` MGT-subtype probe with
    AUTH(seq=1) identity (subtype + peer + auth_seq).
    What it measures: whether the firmware reported a
    TX_RESP / REPLY_TX for the STA-originated AUTH(seq=1)
    management frame, and what status / rate / retry count
    that response carried. A SUCCESS status means the
    firmware believed the AP L2-ACKed the STA's AUTH(seq=1)
    frame (the firmware's rate-control gets feedback from
    the radio that an ACK was received).
    What it does NOT measure: whether the AP actually
    transmitted AUTH(seq=2) in response. The AP's TX of
    AUTH(seq=2) is a downstream causal event observable
    only at the AP side or at the STA's RX side.

  Layer (b): STA firmware RX delivery to the host driver.
    Probe: `iwx_rx_frame` MGT-subtype probe (filters
    AUTH, ASSOC_RESP, REASSOC_RESP, DEAUTH, DISASSOC).
    What it measures: whether the iwx firmware received a
    management frame on the air, decided it was addressed
    to this STA (MAC filter accept), and passed it up the
    RX ring to the host driver. The probe fires once per
    frame the firmware decides to deliver.
    What it does NOT measure: whether the firmware also
    emitted a L2 ACK to the AP for the same frame. L2 ACK
    emission is hardware/firmware-controlled at the MAC
    layer and is independent of RX-to-host delivery; for
    well-formed unicast frames the firmware ACKs at MAC
    layer regardless of whether the host driver later
    consumes the frame, BUT a firmware that has not
    properly programmed its station table for the AP's
    BSSID may neither L2-ACK nor deliver upward.

  Layer (c): Host net80211 mgmt RX delivery for AUTH.
    Probe: `ieee80211_recv_auth` entry probe with algo /
    seq / status / source MAC, plus a short-frame reject
    probe.
    What it measures: whether net80211 parsed an AUTH
    frame's fixed fields and entered the auth state
    machine dispatcher. A successful entry log means the
    frame got past the iwx -> net80211 hand-off, passed
    duplicate detection, frame-length validation, and node
    lookup.
    What it does NOT measure: whether the AP-side L2 ACK
    was actually emitted. The host has no visibility into
    that lower-layer behavior.

  Layer (d): AP-side L2 ACK observation captured by hostapd.
    Probe: external; hostapd's log line `STA ... did not
    acknowledge authentication response`.
    What it measures: the AP's MAC waited for an ACK frame
    in the SIFS window after transmitting AUTH(seq=2) and
    did not see one. This is the AP's radio receiver
    failing to detect a STA-originated ACK frame within
    the standard ACK timeout.
    What it does NOT measure: whether the STA firmware
    received the AUTH(seq=2) frame and chose not to ACK
    it, versus whether the STA firmware never received
    AUTH(seq=2) on its radio, versus whether the STA
    firmware did ACK but the ACK was lost in transit to
    the AP, versus whether the STA was on a different
    channel at the moment AUTH(seq=2) was on the air.
    The AP-side observation alone cannot discriminate
    these four sub-cases.

## What the existing rev5 hostapd evidence proves and does not

`commit-approval/runtime_evidence/CR-479-stage1-project-owned-
plti-pmk-producer-kext-trigger-rev5-20260518/30_join/
07_hostapd_evidence.txt` records layer (d) and nothing else.
Without layer (a), (b), and (c) probes, the AP-side observation
is consistent with all of the following independent failure
modes, which require different fixes:

  1. STA never sent AUTH(seq=1). (Layer (a) tx-completion
     would not fire; ruled out by tx-completion probe.)
  2. STA sent AUTH(seq=1), firmware reported TX FAIL (so AP
     never saw the request). (Layer (a) tx-completion FAIL
     status; ruled out by tx-completion probe.)
  3. STA sent AUTH(seq=1) with TX SUCCESS, AP responded
     with AUTH(seq=2), STA firmware received AUTH(seq=2)
     and L2-ACKed it, but the STA's L2 ACK was lost in
     transit. (Layer (b) iwx_rx_frame fires AND layer (c)
     ieee80211_recv_auth fires; hostapd still reports
     did-not-ACK. This is "true ACK lost in air" -- rare,
     usually environmental; not a software defect.)
  4. STA sent AUTH(seq=1) with TX SUCCESS, AP responded
     with AUTH(seq=2), STA firmware received AUTH(seq=2)
     but did NOT L2-ACK it (e.g., MAC filter rejected the
     frame after RX accept but before ACK), and the
     firmware delivered the frame to iwx anyway. (Layer
     (b) iwx_rx_frame fires AND layer (c)
     ieee80211_recv_auth may or may not fire depending on
     the path; hostapd reports did-not-ACK. Firmware bug
     scenario.)
  5. STA sent AUTH(seq=1) with TX SUCCESS, AP responded
     with AUTH(seq=2), STA firmware never received
     AUTH(seq=2) (wrong channel, MAC filter rejected
     before ACK, RX subsystem disabled, station table
     misprogrammed). (Layer (b) iwx_rx_frame does NOT
     fire, layer (c) ieee80211_recv_auth does NOT fire;
     hostapd reports did-not-ACK. STA-side firmware
     programming bug scenario.)
  6. STA sent AUTH(seq=1) with TX SUCCESS, AP responded
     with AUTH(seq=2), STA firmware received AUTH(seq=2)
     and L2-ACKed it, delivered to iwx, but iwx dropped
     before net80211. (Layer (b) iwx_rx_frame fires but
     layer (c) ieee80211_recv_auth does NOT fire; hostapd
     reports did-not-ACK ONLY IF the STA's L2 ACK was
     also lost separately -- otherwise hostapd would
     report success at L2 and the failure would surface
     later. Combined-failure scenario; lowest prior.)
  7. iwx_auth() itself returned an error before net80211
     could send AUTH(seq=1). (Layer (a) tx-completion
     never fires; ruled out by iwx_auth step probes.)

The four-layer probe set in this patch discriminates
between sub-cases 1-7 by combining the four independent
layer observations. The probe interpretation matrix in the
next section maps each (a)/(b)/(c)/(d) combination to a
unique sub-case so the next CR can target the right
divergence.

## Insufficiency of existing non-invasive evidence

The rev3, rev5, rev7, rev10, and rev11 evidence packages
all provide observations at layer (d) only (hostapd output)
plus the post-fact `setairportnetwork` API return code.
None of them contain iwx kext-internal markers for layer
(a), layer (b), or layer (c). Specifically:

  - `iwx_auth()` currently has only failure-path XYLogs
    (the `XYLog("%s: could not add MAC context...")` style)
    and no success-path step boundary markers. The
    pre-condition leaves (phy_ctxt_update, mac_ctxt_cmd,
    binding_cmd, add_sta_cmd, enable_mgmt_queue,
    clear_statistics, protect_session) cannot be
    distinguished without success-path probes.
  - `iwx_rx_tx_cmd_single()` currently logs only on
    `txfail` (non-success status) and has no MGT-subtype
    or AUTH(seq=1) identity for the logged completion.
    A SUCCESS TX for AUTH(seq=1) currently produces no
    log line, so layer (a) is invisible.
  - `iwx_rx_frame()` currently has no XYLog for incoming
    management frames. Layer (b) is invisible.
  - `ieee80211_recv_auth()` currently logs only via
    `DPRINTF` (compile-disabled in production builds).
    Layer (c) is invisible.

No combination of unified-log, hostapd, airportd, CoreWiFi,
IO80211, panic, or Apple framework decompilation evidence
can answer the question without these probes, because the
question is internal to the iwx firmware-host interface and
the net80211 RX path -- not visible at any Apple framework
boundary.

Apple does not ship Intel iwlwifi-style PCIe Wi-Fi cards on
any current Mac, so Apple reference decompilation is
structurally inapplicable. The reference contract for the
iwx firmware-host interface is OpenBSD `iwx(4)` and Linux
`iwlwifi` (both source-available; both already covered at
iwx initial-port time, with the project's iwx_tx_resp /
IWX_TX_STATUS_* / IWX_ADD_STA / IWX_BINDING_CONTEXT_CMD /
IWX_MAC_CONTEXT_CMD / IWX_TIME_EVENT_CMD definitions
captured in `itlwm/hal_iwx/if_iwxreg.h`). The reference
contract for the 802.11 auth state machine is OpenBSD
`net80211` (also source-available; the project's net80211
port is the canonical implementation).

## Complete leaf-level probe set in this patch

  itlwm/hal_iwx/ItlIwx.cpp, in iwx_auth() success path
  (preconditioning leaves, ordered):
    - iwx_auth: phy_ctxt_update {MONITOR|STA} OK chan=N generation=N
    - iwx_auth: mac_ctxt_cmd ADD OK generation=N
    - iwx_auth: binding_cmd ADD OK generation=N
    - iwx_auth: add_sta_cmd OK generation=N sta_id=N
    - iwx_auth: enable_txq MONITOR OK generation=N  (monitor branch only)
    - iwx_auth: enable_mgmt_queue OK generation=N
    - iwx_auth: clear_statistics OK generation=N
    - iwx_auth: schedule_protect_session SESSION_PROT_CMD duration_tu=N err=N generation=N
        OR iwx_auth: protect_session legacy duration_tu=N min_duration_tu=N generation=N
    - iwx_auth: returning err=N generation=N

  itlwm/hal_iwx/ItlIwx.cpp, in iwx_rx_tx_cmd_single()
  (layer (a), MGT-only):
    - iwx_rx_tx_cmd_single: MGT subtype=0xNN peer=NN:NN:NN:NN:NN:NN
      auth_seq=N status=0xNN (SUCCESS|FAIL) frame_count=N
      initial_rate=0xN failure_frame=N wireless_media_time=N
      (subtype, peer, and auth_seq are read at TX completion
      from per-tx-buffer identity fields populated in
      `iwx_tx()` BEFORE the existing `mbuf_adj(m, hdrlen)`
      call. `iwx_tx()` copies the 802.11 header into the
      firmware TX command and then trims that header from the
      mbuf before storing the post-trim mbuf on `data->m`,
      so a TX-completion-time mbuf peek cannot recover the
      management subtype, receiver address, or AUTH transaction
      sequence; the stored-identity fields preserve them across
      the trim. The capture sites are the existing `wh =
      mtod(m, struct ieee80211_frame *)` reads in the tx_gen2
      and tx_gen3 paths, immediately before the matching
      mbuf_adj call. Identity is recorded in three new fields
      on `struct iwx_tx_data`
      (`itlwm/hal_iwx/if_iwxvar.h`): `diag_subtype` (uint8_t,
      sentinel 0xff = not captured), `diag_auth_seq` (uint16_t,
      sentinel 0xffff = not an AUTH frame or AUTH body too
      short), and `diag_peer[6]` (uint8_t MAC). For AUTH
      frames the trace shows auth_seq=1 for STA-originated
      AUTH(1) requests and auth_seq=N for any other auth
      sequence. Non-AUTH MGT subtypes log auth_seq=0xffff
      (sentinel). Non-MGT TX completions log subtype=0xff and
      auth_seq=0xffff and the MGT branch is skipped, so DATA
      and CTRL TX completions are not affected.)

  itlwm/hal_iwx/ItlIwx.cpp, in iwx_rx_frame() (layer (b),
  non-beacon MGT):
    - iwx_rx_frame: MGT subtype=0xNN from=NN:NN:NN:NN:NN:NN
      rssi=N len=N chanidx=N
      (filters AUTH, ASSOC_RESP, REASSOC_RESP, DEAUTH,
      DISASSOC. Beacons and probe responses are excluded
      to avoid oslog flooding.)

  itl80211/openbsd/net80211/ieee80211_input.c, in
  ieee80211_recv_auth() (layer (c)):
    - ieee80211_recv_auth: frame too short len=N (rejecting)
        (short-frame reject branch)
    - ieee80211_recv_auth: algo=N seq=N status=N from=NN:NN:NN:NN:NN:NN
        (entry after short-frame check)

  itlwm/hal_iwx/ItlIwx.cpp, in iwx_attach() (deterministic
  carrier-visibility smoke marker):
    - smoke_marker iwx_attach OK
        (emitted exactly once per successful iwx_attach via
        the same project-owned os_log carrier as the four
        leaves above; see "Diagnostic output carrier" below)

## Diagnostic output carrier

The four auth-ACK kext-side leaves above plus the
iwx_attach smoke marker route through a project-owned
diagnostic output carrier defined at
`itl80211/linux/iwx_diag_log.h`. The carrier uses
`os_log_create("com.zxystd.AirportItlwm", "iwx.auth_ack")`
and emits each record via the `IWX_AUTH_DIAG` macro. The
carrier is bounded: the project-wide `XYLog` macro defined
at `itl80211/linux/types.h` (`IOLog + kprintf` with prefix
`itlwm:`) is NOT globally redirected. The carrier scope is
exactly the four auth-ACK leaf probes plus the smoke
marker; every other XYLog call site in the codebase
continues to use the original IOLog+kprintf channel.

Why a project-owned os_log carrier exists separately:
  The prior Stage 2 after-fix runtime cycle (commit-approval/
  runtime_evidence/CR-479-stage2-after-fix-runtime-iwx-auth-
  ack-boundary-after-pmk-delivery-rev4-20260518/) demonstrated
  that on macOS Tahoe 26.2 (build 25C56) the XYLog
  IOLog+kprintf delivery channel is not retrievable post-hoc
  for third-party-kext callers:
    1. `kprintf -> kernel msgbuf` (kern.msgbuf=131072 bytes,
       128 KiB) is dominated by QEMU isa-applesmc emulation
       producing AppleSMCFamily error kprintf at ~4-5 Hz;
       1211 of 1213 dmesg lines were AppleSMCFamily after
       a brief trigger window, and the iwx-side kprintf was
       overwritten before `sudo dmesg` could read it.
    2. `IOLog -> unified log (kernel sender)` does not
       surface third-party kext IOLog output through any
       observed log-show predicate on this build; only
       kernel-internal subsystems (com.apple.Sandbox)
       reach the unified log via `processIdentifier == 0`.
  The os_log firehose channel is independent of both
  delivery paths: it does not share the 128 KiB msgbuf
  ring with kprintf, and a project-owned subsystem +
  category give the unified-log query a precise filter.
  Routing the four narrow auth-ACK leaves and one smoke
  marker through a project-owned os_log handle is the
  minimal-blast-radius way to make the diagnostic
  observable on this platform without touching any
  non-auth-ACK code path.

Carrier object lifecycle:
  - `iwx_auth_diag_log` is a single global `os_log_t`
    declared `extern "C"` in `itl80211/linux/iwx_diag_log.h`
    and defined in `itlwm/hal_iwx/ItlIwx.cpp`. It is
    created exactly once by `iwx_auth_diag_init()`, an
    idempotent guarded initializer called from the
    `iwx_attach()` success path immediately before the
    existing `return true;` and immediately after the
    rev4 `XYLog("DEBUG iwx_attach: iwx_preinit done OK\n")`
    marker.
  - After init it is never released; the project diagnostic
    carrier lives for the lifetime of the loaded kext,
    which matches the lifetime of the call sites and
    avoids any os_log_t lifetime race between the smoke
    marker and the four leaf probes.
  - Producer: `iwx_auth_diag_init()` in ItlIwx.cpp.
  - Consumers: every `IWX_AUTH_DIAG(...)` call site (15
    converted rev4 XYLog probes plus 1 smoke marker).
  - Fall-back: if `IWX_AUTH_DIAG` is invoked before
    `iwx_auth_diag_init()` has completed (e.g., if a
    future refactor moves the init later or if an MGT
    TX completes before iwx_attach returns), the macro
    routes the record through `OS_LOG_DEFAULT` so output
    is still observable via the default kernel logger;
    this is a correctness safety net, not the primary
    path.

Carrier observability contract:
  Output is intended to be visible on macOS Tahoe 26.2 via
    sudo log show --info --debug \
      --predicate 'subsystem == "com.zxystd.AirportItlwm" \
                   AND category == "iwx.auth_ack"'
  regardless of the kernel msgbuf state. The next
  approved Stage 2 runtime cycle MUST follow this
  verification order:

    Step 1: After reboot, BEFORE firing any auth-ACK
      trigger, verify the smoke marker is visible via
      the predicate above. The smoke marker fires
      exactly once per successful `iwx_attach()`, so a
      boot that loads the rev4 carrier kext (UUID
      1ADAF722-CE2E-37E6-86C2-079426E0F99A, sha256
      df0b19d06471789c60f3de895ad381c4a6a8f0f54dc67b91b11c46e8601e61d3)
      should show
      exactly one
        itlwm: smoke_marker iwx_attach OK
      line in the unified-log output (multiple lines mean
      iwx re-attached during firmware restart, which is
      itself diagnostic information). If the smoke marker
      is NOT visible, the carrier itself is broken (e.g.,
      Tahoe filtered the project subsystem, or os_log
      from third-party kexts is obstructed in a way
      analogous to IOLog). In that case Stage 2 files a
      new precise blocker and does NOT proceed to the
      auth-ACK trigger.

    Step 2: Only if Step 1 passes, fire the single
      auth-ACK trigger
      (`networksetup -setairportnetwork`) exactly once.

    Step 3: Capture the unified-log output for the same
      subsystem/category predicate during the trigger
      window. Confirm the four auth-ACK leaves (iwx_auth
      step-boundary probes, iwx_rx_tx_cmd_single MGT,
      iwx_rx_frame MGT, ieee80211_recv_auth entry +
      short-frame reject) all surface via the carrier.

    Step 4: Map the captured leaves to a Case A-F
      classification per the matrix in "Probe
      interpretation matrix" below, or file a precise
      blocker explaining which leaf was missing.

Smoke-marker semantics:
  The smoke marker is a presence-only probe. It carries no
  generation, sta_id, channel, or other field-value
  payload; it just emits the string
  `itlwm: smoke_marker iwx_attach OK`. The marker proves
  three things at once: (a) the global os_log_t handle
  was created via os_log_create, (b) the macro expanded
  without crashing, and (c) the carrier output is
  reaching the unified-log firehose under the project
  subsystem/category. If the marker fires N times in a
  single boot it means `iwx_attach()` ran N times (e.g.,
  during firmware restart / re-attach cycles), which is
  itself diagnostic information for the next runtime to
  record.

## Probe interpretation matrix

After Stage 1 approval and a Stage 2 run that drives one
`networksetup -setairportnetwork` cycle, the unified log
will contain a deterministic trace of the four observation
layers. The next CR will choose its target based on which
case fires:

  Case A: any `iwx_auth: returning err=N` with N != 0, or
    one of the preconditioning step probes never fires.
    Discriminator: iwx_auth preconditioning failed at the
    named leaf. Next CR target: the failed firmware command.

  Case B: iwx_auth: returning err=0 fires; tx-completion
    probe fires for MGT with subtype=AUTH, peer=AP-BSSID,
    auth_seq=1, status=FAIL.
    Discriminator: STA's AUTH(seq=1) TX failed at firmware
    rate-control layer (radio could not get the AP's L2 ACK
    for AUTH(seq=1)). Next CR target: figure out why
    AUTH(seq=1) wasn't ACKed by the AP. The hostapd-side
    observation in the rev5 evidence is about AUTH(seq=2)
    being unacknowledged; if Case B fires, the hostapd
    evidence interpretation needs revisiting (possibly the
    AP was logging a STAGE-1 ACK failure, not a stage-2
    one).

  Case C: iwx_auth returning err=0; tx-completion for
    AUTH(seq=1) status=SUCCESS; layer (b) iwx_rx_frame
    AUTH subtype probe does NOT fire within a typical
    auth-response window (~500 ms after TX SUCCESS).
    Discriminator: the firmware never received AUTH(seq=2),
    OR received it but never delivered to the host driver
    AND did not L2-ACK either. Maps to sub-case 5 from
    section "What the existing rev5 hostapd evidence
    proves". Next CR target: firmware programming of the
    station table, MAC filter, or channel context at the
    moment AUTH(seq=2) was expected.

  Case D: iwx_auth returning err=0; tx-completion for
    AUTH(seq=1) status=SUCCESS; layer (b) iwx_rx_frame
    fires with subtype=AUTH from AP-BSSID; layer (c)
    ieee80211_recv_auth entry probe does NOT fire.
    Discriminator: firmware received and delivered
    AUTH(seq=2) to the host, but net80211 dropped it
    before the auth state machine. Next CR target:
    net80211 RX classification (duplicate detection,
    node lookup, frame-type dispatch) between
    iwx_rx_frame and ieee80211_recv_auth.

  Case E: iwx_auth returning err=0; tx-completion for
    AUTH(seq=1) status=SUCCESS; layer (b) iwx_rx_frame
    fires; layer (c) ieee80211_recv_auth fires with
    algo=0 (OPEN), seq=2, status=0.
    Discriminator: all STA-side layers (a)/(b)/(c)
    succeeded. The hostapd "did not ACK" observation at
    layer (d) is then attributable to one of two
    independent sub-cases: (E1) the STA firmware did NOT
    emit a L2 ACK to the AP even though it delivered the
    RX frame up to the host (firmware MAC filter / ACK
    behavior bug -- sub-case 4), or (E2) the L2 ACK was
    emitted but lost in transit (sub-case 3, usually
    environmental). Discriminating E1 from E2 requires
    AP-side packet capture or STA-side firmware-level
    instrumentation beyond this kext-host boundary.

  Case F: ieee80211_recv_auth fires with the short-frame
    reject probe.
    Discriminator: a malformed AUTH frame reached
    net80211 (fragmentation or radiotap decap bug).

The probe set discriminates Cases A-F without producing a
shallow marker that could not differentiate the leaves.
Each probe is a pure IWX_AUTH_DIAG (project-owned os_log
carrier) statement that reads existing struct fields; no
probe creates a fake success, masks a failure, retries an
operation, or modifies any behavioral path.

## Scope

This Stage 1 patch contains:
  - 1 new header `itl80211/linux/iwx_diag_log.h` defining
    the project-owned diagnostic output carrier
    (`iwx_auth_diag_log`, `iwx_auth_diag_init`, and the
    `IWX_AUTH_DIAG(fmt, ...)` macro)
  - Carrier definition + idempotent initializer in
    `itlwm/hal_iwx/ItlIwx.cpp` (extern "C" block); init
    call + deterministic same-carrier smoke marker
    (`smoke_marker iwx_attach OK`) emitted from the
    `iwx_attach()` success path immediately before the
    existing `return true;`
  - 9 IWX_AUTH_DIAG probes in iwx_auth() at success-path
    step boundaries (phy_ctxt_update MONITOR or STA
    branch, plus mac_ctxt_cmd, binding_cmd, add_sta_cmd,
    enable_txq MONITOR (monitor branch only),
    enable_mgmt_queue, clear_statistics,
    schedule_protect_session OR protect_session, return)
  - 1 IWX_AUTH_DIAG probe in iwx_rx_tx_cmd_single() for
    MGT-type TX completion (success and failure) reading
    subtype, peer, and auth_seq identity from
    per-tx-buffer fields on `struct iwx_tx_data`;
    identity is captured in `iwx_tx()` before the
    existing `mbuf_adj` trim
  - 3 new sentinel-initialized identity fields
    (diag_subtype uint8_t = 0xff, diag_auth_seq uint16_t =
    0xffff, diag_peer[6] uint8_t = 0) added to
    `struct iwx_tx_data` in `itlwm/hal_iwx/if_iwxvar.h`
  - 1 IWX_AUTH_DIAG probe in iwx_rx_frame() for
    non-beacon mgmt RX (AUTH, ASSOC_RESP, REASSOC_RESP,
    DEAUTH, DISASSOC)
  - 2 IWX_AUTH_DIAG probes in ieee80211_recv_auth()
    (short-frame reject branch + post-check entry)
  - 1 documentation file (this report) at the stable path
    `analysis/iwx_auth_ack_boundary.md`
  - Removal of the prior unstable filename
    `analysis/ANALYSIS_REPORT_CR-479-iwx-auth-ack-boundary
    -20260518.md` (it was an earlier draft and is replaced
    by the stable filename above).

No other behavior change. The kext rebuilds cleanly under
`scripts/build_tahoe.sh` against the Tahoe SDK. The
AirportItlwmAgent helper binary is byte-identical to the
basis commit because no helper code is touched.

## Security and privacy

Probes log: MAC addresses (as six numeric
`%02x:%02x:%02x:%02x:%02x:%02x` byte fields for the AP's
BSSID and the source address; see "os_log MAC privacy
contract" below), management subtype byte, auth-body algo
/ sequence / status numeric fields, RSSI signal-strength
value, channel index, frame length, TX rate index,
firmware TX status code, firmware error counters
(frame_count / failure_frame / wireless_media_time).
No PSK, PMK, passphrase, EAPOL-key material, SSID
passphrase, or per-user keying material flows through any
new probe. The probes are on the management-frame and
auth-frame paths only; data-frame contents never reach a
new probe.

### os_log MAC privacy contract

macOS unified-log `os_log` treats dynamic `%s` arguments
as PRIVATE by default and renders them as the literal
string `<private>` in `sudo log show` output unless the
format specifier is explicitly annotated public. Numeric
arguments (`%d`, `%u`, `%02x`, `%zu`, etc.) are collected
as PUBLIC values without further annotation.

The rev4 layer-(a)/(b)/(c) probes rendered the MAC
identity fields via `ether_sprintf(addr)` -> `%s`. Under
os_log that dynamic-string form would surface as
`<private>` and break the Case A-F discriminator, which
needs the actual peer/source MAC bytes to attribute each
record to a specific 802.11 frame on the air. The rev1
carrier patch therefore renders every MAC field as six
numeric byte arguments
(`%02x:%02x:%02x:%02x:%02x:%02x`) so the bytes are
collected as public integers and visible to `log show`
without further annotation. The captured content is
exactly the same MAC bytes the prior rev4 patch already
exposed via `ether_sprintf`; only the rendering function
moved from a dynamic-string conversion to a numeric
field-by-field byte format.

Durable redaction policy for committed evidence remains
unchanged: any committed evidence file that captures the
auth-ACK trigger MUST redact the AP-side SSID via the
private alias FAST_LAB_AP (and the passphrase is never
captured at all); MAC bytes themselves are not redacted
because the auth-ACK Case A-F discriminator requires the
runtime evidence to record which 802.11 peer the iwx
firmware was attempting to authenticate against.

## Forbidden-change compliance

Per the auditor's bounded-coder-work instruction for this
work item, the following constraints hold for this patch:
  - Project keychain code, AirportItlwmAgent helper code,
    PLTI PMK producer code, and credential-acquisition
    source are NOT touched (verified by `git diff --stat`
    against the basis commit).
  - CWWiFiClient / CoreWLAN trigger path code is NOT
    touched.
  - No host-side `.git` clone / rsync / mirror / worktree
    move is involved; all source and build work is on the
    canonical guest checkout under
    `/Users/devops/Projects/itlwm`.
  - No timing retry, fake success, suppression, masking,
    fallback, or behavior modification is added; every
    probe is observation-only.
  - No guest install or runtime is exercised by this
    Stage 1; the `scripts/build_tahoe.sh` invocation that
    proves the kext rebuilds cleanly is a build-only
    check, not a runtime test.
  - Per active operator instruction
    `op_20260518_refcred_external-analysis-agent`, the
    project-owned keychain PMK delivery is laboratory PMK
    evidence only and not final Wi-Fi credential UX. This
    Stage 1 is not credential UX work; it is downstream of
    any credential source and addresses an independent
    802.11 boundary.

## Port to the iwn HAL (devId 0x088E reachability)

The iwx-only carrier rev4 Stage 2 after-fix runtime cycle
proved that the rev4 iwx-side probes do not fire on the
current Tahoe 26.2 VM: post-install kmutil/kextstat snapshots
show `com.zxystd.AirportItlwm` loaded with the rev4 UUID,
but the unified-log predicate
  `subsystem == "com.zxystd.AirportItlwm" AND
   category == "iwx.auth_ack"`
returned zero records across the entire post-install window
(`AirportItlwm::start`, `networksetup -setairportnetwork`
attempt, and follow-up steady state). The driver attaches to
PCI vendor:device 0x8086:0x088E (Intel Centrino Advanced-N
6235), which is routed by the project's HAL dispatcher to the
iwn HAL (`itlwm/hal_iwn/`), not the iwx HAL where the rev4
probes live. The iwx_attach / iwx_tx / iwx_tx_done / iwx_rx
leaves cannot execute on this VM because the iwx HAL is not
the active HAL for this device.

The iwn HAL exposes structurally equivalent boundary points,
so the same auth-ACK boundary diagnostic is implementable on
the iwn side:

  - `iwn_attach` is the iwn HAL attach entry point. A
    `smoke_marker iwn_attach OK` record at the success
    return of `iwn_attach` (next to the existing
    `task_set(&sc->init_task, ..., "iwn_init_task")`)
    proves the carrier is reachable on devId 0x088E.
  - `iwn_auth` is the iwn equivalent of `iwx_auth` (auth-
    preconditioning before the net80211 layer sends the
    STA AUTH(seq=1)). It returns 0 only after RXON
    succeeds, set_txpower succeeds, and
    add_broadcast_node succeeds. Four step-boundary
    probes (`iwn_auth: RXON OK chan=N`,
    `iwn_auth: set_txpower OK`,
    `iwn_auth: add_broadcast_node OK ridx=R`, and
    `iwn_auth: returning err=0`) make it visible which
    preconditioning step succeeded and which failed.
  - `iwn_tx` is the iwn equivalent of `iwx_tx`. Like
    `iwx_tx`, it calls `mbuf_adj` to strip the 802.11
    header before storing `data->m = m` for completion
    bookkeeping. The completion-time mbuf is therefore
    post-trim and cannot be safely cast back to
    `struct ieee80211_frame *`. The iwn-side capture
    pattern matches the rev4 iwx-side pattern: capture
    the MGT frame identity (subtype, peer = `i_addr1`,
    and the AUTH body's `auth_transaction_sequence`
    if subtype == AUTH and the AUTH body length is
    sufficient) into stack locals BEFORE the first
    `mbuf_adj`, then store the captured locals onto a
    new per-tx-buffer slot on `struct iwn_tx_data`
    immediately after the existing `data->m = m`
    assignment.
  - `iwn_tx_done` is the iwn equivalent of
    `iwx_rx_tx_cmd_single`. On MGT TX completion it
    reads `data->diag_subtype != 0xff` to decide whether
    the slot is an interesting MGT frame, then emits the
    firmware-reported `txfail`, `ackfailcnt`, `rate`,
    `rflags`, and `len` together with the captured
    identity. This is the Layer (a) STA-side TX
    completion leaf on the iwn HAL.
  - `iwn_rx_done` is the iwn equivalent of
    `iwx_rx_frame`. Before calling `ieee80211_inputm`,
    the iwn-side probe filters the same five MGT
    subtypes (AUTH, ASSOC_RESP, REASSOC_RESP, DEAUTH,
    DISASSOC) so beacons and probe responses do not
    flood the oslog. This is the Layer (b) STA-side
    RX-delivery leaf on the iwn HAL.

The net80211 leaves (`ieee80211_input_management_frame` and
`ieee80211_recv_auth`) are HAL-independent and remain in
their rev4 iwx-cycle form because they live in
`itl80211/openbsd/net80211/ieee80211_input.c`, not in any
HAL-specific path. They cover Layer (c) ONLY for both HALs
(host-side mgmt RX delivery into net80211 and the auth-
specific entry/short-frame leaves). Layer (d) is AP-side
hostapd L2-ACK evidence and is NOT observable from any
kext-side leaf; it must be collected from the controlled
host AP (FAST_LAB_AP) during the exact Stage 2 trigger
window. See the updated Stage 2 plan in "Next steps".

A `smoke_marker AirportItlwm_start OK` record is also
emitted at the success return of `AirportItlwm::start`
(after `registerService()`, `RT_SET(18)`, and the
`DISARM_PANIC_TIMER` macro). This is the HAL-independent
same-carrier check: it fires regardless of which HAL the
device routes to, and it proves the project-owned os_log
carrier (`os_log_create("com.zxystd.AirportItlwm",
"iwx.auth_ack")`) is reachable on this VM before any HAL-
specific attach runs. `iwx_auth_diag_init()` is
idempotent (`os_log_create` is a cached handle factory),
so calling it from `AirportItlwm::start`,
`iwx_attach`, and `iwn_attach` does not create three
independent log handles.

### iwn-side `struct iwn_tx_data` diag-field lifecycle

  - Three new fields are added to `struct iwn_tx_data` in
    `itlwm/hal_iwn/if_iwnvar.h`:
      * `uint8_t  diag_subtype`   (sentinel 0xff means
        the slot does not hold a captured MGT identity)
      * `uint16_t diag_auth_seq`  (sentinel 0xffff means
        the frame is not AUTH, or the first mbuf
        segment is shorter than `hdrlen + 4` bytes so
        the AUTH body is not contiguous with `wh` in
        the first segment)
      * `uint8_t  diag_peer[6]`   (`i_addr1` of the
        outgoing MGT frame; zero-initialized by the
        `iwn_tx` stack local when identity not
        captured)
  - The submitted Stage 1 does NOT rely on bzero
    initialization of `ring->data`. The reviewed
    `iwn_alloc_tx_ring` (`itlwm/hal_iwn/ItlIwn.cpp`
    ~1356-1402) allocates descriptor / command DMA and
    creates DMA maps but does not bzero `ring->data`,
    so the diag fields cannot be assumed zero at
    allocation. The actual safety invariant is the
    explicit per-`iwn_tx` write-before-publication
    pattern described below.
  - Write site: `iwn_tx` declares stack locals
    initialized to the sentinels
    (`diag_subtype = 0xff`,
    `diag_auth_seq = 0xffff`,
    `diag_peer = {0,0,0,0,0,0}`) at the function
    capture block (BEFORE the first `mbuf_adj`),
    unconditionally overwrites them with the captured
    MGT identity when `type == IEEE80211_FC0_TYPE_MGT`,
    and unconditionally stores all three onto
    `data->diag_*` immediately after the existing
    `data->m = m;` / `data->ni = ni;` assignment.
    Every call to `iwn_tx` therefore writes all three
    diag fields BEFORE the TX descriptor is published
    to firmware (the DMA descriptor segments and the
    scheduler write that hands the slot to firmware
    follow the `data->diag_*` writes in the function
    body).
  - Read site: `iwn_tx_done` is driven by the
    firmware TX_RESP delivery callback, which fires
    only AFTER the descriptor that was just published
    by the matching `iwn_tx` has been consumed by
    firmware. By the time `iwn_tx_done` runs for this
    slot, the matching `iwn_tx` has therefore already
    completed all of its `data->*` writes in source
    order (`data->m = m;`, `data->ni = ni;`,
    `data->ampdu_txmcs = ni->ni_txmcs;`,
    `data->diag_subtype = ...;`,
    `data->diag_auth_seq = ...;`,
    `memcpy(data->diag_peer, ...)`), as documented at
    the write site above and in P1 below.
    `iwn_tx_done` returns early at the existing
    `if (data->ni == NULL) return;` guard at the top
    of the function, and the MGT TX diagnostic probe
    is placed AFTER that early return and immediately
    BEFORE `iwn_tx_done_free_txdata(sc, data)` (the
    function that later clears `data->ni = NULL`).
    The diagnostic fields are therefore guaranteed to
    be the values written by the matching `iwn_tx`
    for this exact slot, because (a) descriptor
    publication to firmware follows all three diag
    writes inside `iwn_tx` (P1), (b) firmware
    TX_RESP delivery follows descriptor publication,
    (c) `iwn_tx_done` is entered after that delivery,
    and (d) the probe runs before
    `iwn_tx_done_free_txdata` clears the slot. The
    safety boundary is descriptor publication, not
    the relative ordering of `data->m` / `data->ni`
    versus `data->diag_*` within the same `iwn_tx`
    invocation. The MGT filter on the probe is
    `data->diag_subtype != 0xff`, so non-MGT
    completions are silently skipped.
  - Lifetime: the slot is overwritten on every
    `iwn_tx` invocation. After the firmware TX_RESP is
    processed, `iwn_tx_done_free_txdata(sc, data)`
    clears `data->m`, `data->ni`, `data->totlen`,
    `data->ampdu_nframes`, and `data->ampdu_txmcs`;
    `data->diag_*` are not explicitly reset there
    because the next `iwn_tx` writes them
    unconditionally before re-publishing the slot,
    and `iwn_tx_done`'s `data->ni == NULL` early
    return prevents any probe firing on a freed slot
    before the next `iwn_tx` re-arms it.

The invariant is therefore (corrected vs the prior
revision of this proof; the actual `iwn_tx` source
order in `itlwm/hal_iwn/ItlIwn.cpp` lines 3834-3843
is `data->m = m; data->ni = ni;
data->ampdu_txmcs = ni->ni_txmcs;
data->diag_subtype = diag_subtype;
data->diag_auth_seq = diag_auth_seq;
memcpy(data->diag_peer, diag_peer,
sizeof(data->diag_peer));`, followed by the TX
descriptor build-out and the scheduler write that
hands the slot to firmware):

  P1: `iwn_tx` writes `data->m` and `data->ni` first
      (this is what later flags the slot as published-
      and-owned for `iwn_tx_done`), then writes
      `data->ampdu_txmcs`, then writes all three diag
      fields (`data->diag_subtype`,
      `data->diag_auth_seq`, `data->diag_peer[6]`)
      unconditionally for the slot, and only AFTER
      all of those `data->*` writes are complete does
      the function build the TX descriptor segments
      and perform the scheduler write that publishes
      the slot to firmware. The firmware therefore
      cannot deliver a TX_RESP for this slot until
      after all three diag fields have been written.
  P2: `iwn_tx_done` returns early at the existing
      `if (data->ni == NULL) return;` guard at the
      top of the function (BEFORE the MGT TX
      diagnostic probe). The probe is placed AFTER
      that early return and immediately BEFORE
      `iwn_tx_done_free_txdata(sc, data)`, and the
      `data->ni = NULL` clear is performed only
      inside `iwn_tx_done_free_txdata`. The probe
      can therefore only observe slots that satisfy
      `data->ni != NULL`. Because `iwn_tx_done` is
      driven by the firmware TX_RESP delivery
      callback, the probe is only entered for slots
      that have been published (P1) and are not yet
      freed by `iwn_tx_done_free_txdata`.
  P3: P1 + P2 together imply the MGT TX probe always
      reads `diag_*` values written by the matching
      `iwn_tx`, never values from a prior slot use,
      and never values from an uninitialized
      allocation. The relative ordering of the
      `data->m` / `data->ni` writes versus the diag
      writes inside the same `iwn_tx` invocation does
      not weaken P3, because the safety boundary that
      matters for the diagnostic probe is "diag
      fields are written before descriptor
      publication to firmware", not "diag fields are
      written before `data->m` / `data->ni`".

The iwn-port adds 135 lines to `itlwm/hal_iwn/ItlIwn.cpp`,
22 lines to `itlwm/hal_iwn/if_iwnvar.h`, and 18 lines to
`AirportItlwm/AirportItlwmV2.cpp` (the HAL-independent
smoke marker and the `iwx_diag_log.h` include). All other
files in this Stage 1 (the iwx-side probes, the net80211
leaves, the project-owned `iwx_diag_log.h` carrier) are
unchanged from rev4 and are carried forward in the same
text form.

## Next steps

  Stage 1: structural review of this probe set + this
  report (now including both the iwx HAL probes carried
  forward from rev4 and the new iwn HAL port).

  Stage 2 (after approval): install the iwn-port kext via
  the active project install/reboot flow, verify the
  HAL-independent smoke marker AND the iwn-specific smoke
  marker are visible on this VM, then fire exactly one
  `networksetup -setairportnetwork` and map the iwn-side
  auth-ACK leaves to a Case A-F finding. The Stage 2
  install/reboot flow is:

    1. Build the kext on the guest via
       `bash scripts/build_tahoe.sh /System/Library/
       KernelCollections/BootKernelExtensions.kc` and
       verify the Tahoe BootKC undefined-symbol check
       passes (the same build command Stage 1 already ran;
       Stage 2 may reuse the same Build/Debug/Tahoe/
       AirportItlwm.kext that Stage 1 produced).
    2. Replace the live kext in place: backup the existing
       /Library/Extensions/AirportItlwm.kext, copy
       Build/Debug/Tahoe/AirportItlwm.kext into
       /Library/Extensions/AirportItlwm.kext, and reassert
       root:wheel ownership on the new tree. The active
       project guidance (docs/AGENT_HANDOFF_2026-04-28.md
       "If install is approved, it is enough to remove the
       old /Library/Extensions/AirportItlwm.kext and copy
       the new one there") does not require `kmutil` for
       legacy install, but on Tahoe 26.2 the auxiliary
       kernel collection at
       /Library/KernelCollections/AuxiliaryKernelExtensions
       .kc must also be regenerated for the new kext UUID
       to be selected at boot. Stage 2 therefore runs
         sudo kmutil create --new aux
             --auxiliary-path
                 /Library/KernelCollections/
                 AuxiliaryKernelExtensions.kc.new
             --volume-root /
             --boot-path
                 /System/Library/KernelCollections/
                 BootKernelExtensions.kc
             --system-path
                 /System/Library/KernelCollections/
                 SystemKernelExtensions.kc
             --bundle-path /Library/Extensions/AirportItlwm.kext
             --allow-missing-kdk
       then atomically swaps the new AuxKC into
       /Library/KernelCollections/AuxiliaryKernelExtensions
       .kc (keeping the prior AuxKC under a `.preinstall-bak`
       suffix for rollback).
    3. Reboot the guest (`sudo /sbin/reboot`) and wait for
       SSH to come back. Confirm via `kmutil showloaded |
       grep -i airportitlwm` that the new iwn-port kext
       UUID (recorded in the Stage 1 request body) is the
       running kext (not the prior UUID).
    4. BEFORE firing any auth-ACK trigger, verify both
       smoke markers are visible exactly once via
         sudo log show --info --debug --predicate \
           'subsystem == "com.zxystd.AirportItlwm" AND \
            category == "iwx.auth_ack"' \
           --start "<reboot ts>"
       and confirm both literal lines appear:
         itlwm: smoke_marker AirportItlwm_start OK
         itlwm: smoke_marker iwn_attach OK
       The first line proves the project-owned carrier is
       reachable on this VM regardless of HAL; the second
       line proves the iwn HAL specifically is the live
       HAL on this VM and the iwn-side probes can be
       expected to fire. If either smoke marker is NOT
       visible at this step, the corresponding leaf chain
       is unreachable; file a precise blocker and do NOT
       proceed.
    5. Only if step 4 passes, prepare and arm FAST_LAB_AP
       Layer (d) collection BEFORE firing the trigger:
         5a. Confirm FAST_LAB_AP is running on the host
             interface (`wlxe84e062bc4f5`) using
             `status-itlwm-lab-ap.sh`; confirm hostapd,
             dnsmasq (or the configured lease provider),
             and the AP-mode WPA2-PSK profile are
             active. The host AP must be configured for
             the same SSID/passphrase as the project
             keychain entry, with redaction.
         5b. Start a host-side `journalctl -u hostapd`
             (or the configured hostapd log path), a
             `journalctl -u dnsmasq` slice if the lease
             provider is dnsmasq, and snapshot the
             pre-trigger `iw dev wlxe84e062bc4f5 station
             dump` to baseline the AP's view of the
             STA's L2 state.
         5c. Synchronize the host-AP and guest clocks
             via a recorded timestamp pair so the
             post-hoc cross-correlation between guest
             unified-log records and host hostapd /
             station-dump records uses an unambiguous
             window.
    6. With step 5 instrumentation armed, ensure the
       project keychain holds the PSK for the
       FAST_LAB_AP SSID and fire exactly one
       `networksetup -setairportnetwork en1
       <FAST_LAB_AP SSID redacted> <FAST_LAB_AP
       PASSPHRASE redacted>`. Wait for the resulting
       state (success or Error -3912) to settle, then
       capture (a) the guest unified-log output for the
       same subsystem/category predicate during the
       trigger window AND (b) the FAST_LAB_AP host-
       side evidence for the same trigger window:
         - hostapd log slice (with the redacted SSID
           used as the filter token) covering the
           trigger window; the L2-ACK / "did not
           acknowledge authentication response" / IEEE
           802.11 state-machine records for the
           STA-private MAC are the Layer (d)
           observation.
         - dnsmasq / lease-provider slice for the same
           window (informational; tells whether the
           STA reached DHCP).
         - post-trigger `iw dev wlxe84e062bc4f5
           station dump` snapshot.
         - host AP status output and any other
           FAST_LAB_AP runtime evidence the controlled
           AP exposes.
       All redaction rules for FAST_LAB_AP SSID /
       passphrase and the STA-private MAC apply to the
       evidence stored in commit-approval/.
    7. Map the captured iwn-side leaves
       (`iwn_auth: ...`, `iwn_tx_done: MGT ...`,
       `iwn_rx_done: MGT ...`) AND the HAL-independent
       net80211 leaves
       (`ieee80211_input_management_frame`,
       `ieee80211_recv_auth`, which cover Layer (c)
       ONLY) AND the FAST_LAB_AP host-side Layer (d)
       evidence captured in step 6 to a Case A-F
       finding per "Probe interpretation matrix"
       above, or file a precise blocker explaining
       which leaf was missing. Layer (d) is NOT
       observable from any kext-side probe; without
       the host AP evidence collected in the same
       trigger window, the Case A-F discriminator
       cannot fully classify whether the STA's
       AUTH(seq=1) was L2-ACKed by the AP and whether
       the AP transmitted an AUTH(seq=2) the STA never
       received. Prior-cycle hostapd evidence (e.g.,
       the rev5 "did not acknowledge authentication
       response" record cited in "Anomaly" above) is
       usable as historical context but cannot
       substitute for the same-window Layer (d)
       capture required by Stage 2.

  The Stage 2 finding seeds the next CR (either a semantic
  patch for the identified leaf or a follow-up diagnostic
  Stage 1 that drills into the chosen case).


## Stage 2 runtime finding (rev5 carrier, 2026-05-18 trigger)

The rev5 iwn-port auth-ACK diagnostic carrier was installed and
one controlled join trigger was executed against the FAST_LAB_AP
host. The accepted runtime classification is a precise missing-
leaf blocker, not a Case A-F classification:

  **MISSING_LEAF_KEXT_MGT_TX_RX_PATH_NOT_INSTRUMENTED_BY_REV5**

### Trigger window and identity

- Trigger: one `networksetup -setairportnetwork en1 ...`
  invocation, `2026-05-18T23:48:23Z` to `2026-05-18T23:48:44Z`,
  returning user-visible
  `Failed to join network <FAST_LAB_AP_SSID_redacted>.
   Error: -3912 com.apple.wifi.apple80211API.error error -3912.`
  The underlying Apple80211 error that mapped to user-visible
  `-3912` was `-3905` (association timeout).
- Built kext sha256
  `22a1529b45ca0b54838ba384bcc7f55a9f8fa547363e17b80d4d45cf78a987d3`,
  loaded UUID `06BBED00-91AB-37DE-80B3-96AEF4E71A85`, confirmed
  by `kmutil showloaded` after reboot.
- One trigger only; no retry, polling loop, forced callback, or
  state forcing; no second install, kext reload, or join.

### Layer findings (all four kext-side carrier leaves MISSING; Layer (d) CONFIRMED)

- Layer (a) `iwn_tx_done` MGT auth(seq=1) completion: MISSING
  (zero records in the trigger window).
- Layer (b) `iwn_rx_done` MGT auth(seq=2) delivery: MISSING
  (zero records in the trigger window).
- Layer (c) `ieee80211_recv_auth` net80211 entry: MISSING
  (zero records in the trigger window).
- Layer (c1) `iwn_auth` step boundaries: MISSING (the
  iwn_auth state machine was not entered during the trigger
  window).
- Layer (d) host-AP L2-ACK observation: CONFIRMED. The
  controlled FAST_LAB_AP `hostapd` recorded three records for
  the guest's private MAC (PMAC) `2a:4c:b9:e2:44:0f`:
  two `wlp0s20f3: STA 2a:4c:b9:e2:44:0f IEEE 802.11: did
  not acknowledge authentication response` entries, then one
  `wlp0s20f3: STA 2a:4c:b9:e2:44:0f IEEE 802.11:
  deauthenticated due to inactivity (timer DEAUTH/REMOVE)`
  entry. These records appear in
  `/tmp/itlwm-lab-ap/hostapd.log` at lines 63-65, immediately
  after the pre-trigger snapshot ended at line 62; they are
  the Layer (d) evidence required by step 7 of "Stage 2
  trigger plan" above and recorded in
  `commit-approval/runtime_evidence/CR-479-stage2-after-fix-
   runtime-auth-ack-iwn-hal-diagnostic-output-carrier-rev5-
   20260519/40_join/13_host_layer_d_evidence.txt`.

### PMAC attribution evidence

- The PMAC `2a:4c:b9:e2:44:0f` is the same value observed on
  the guest `en1` post-trigger state snapshot
  (`commit-approval/runtime_evidence/CR-479-stage2-after-fix-
   runtime-auth-ack-iwn-hal-diagnostic-output-carrier-rev5-
   20260519/50_post/00_post_trigger_state.txt`) and on the
  host `hostapd` records cited above. Together these two
  independent sources support the PMAC attribution.
- Raw airportd unified-log records for the trigger window
  REDACT the PMAC value (Apple's normal anonymization in
  airportd/CoreWLAN public log output), so the raw airportd
  evidence is NOT cited as a textual source for the literal
  PMAC. The PMAC attribution rests on the guest post-trigger
  `en1` PMAC plus the host `hostapd` line 63-65 append after
  the pre-trigger snapshot, which is the audit-accepted
  evidence chain.
- Host AP interface identity: the FAST_LAB_AP alias profile
  documents the USB MT7612U adapter `wlxe84e062bc4f5` as the
  test peer, but at this trigger window that USB adapter was
  not plugged in; the host `start-itlwm-lab-ap.sh` auto-
  selected the only available Wi-Fi iface, the built-in
  Intel `wlp0s20f3`, with the same FAST_LAB_AP SSID, WPA2-PSK
  security, channel 6, gateway `10.77.0.1/24`, and `dnsmasq`
  DHCP preserved end-to-end. This iface substitution is
  documented in
  `commit-approval/runtime_evidence/CR-479-stage2-after-fix-
   runtime-auth-ack-iwn-hal-diagnostic-output-carrier-rev5-
   20260519/40_join/14_host_iface_identity_reconciliation.txt`
  and does not affect the Layer (d) PMAC attribution.
- Carrier smoke markers `itlwm: smoke_marker iwn_attach OK`
  and `itlwm: smoke_marker AirportItlwm_start OK` were both
  visible at boot in the unified-log subsystem
  `com.zxystd.AirportItlwm` category `iwx.auth_ack`,
  confirming the carrier itself was alive; the silence at
  the trigger window is a property of the management-frame
  path actually exercised by the Apple ASSOC IOCTL, not of
  the carrier.

### Discriminator interpretation

Per the "Probe interpretation matrix" above, the rev5 carrier
TX-side leaves (`iwn_tx_done: MGT`) AND RX-side leaves
(`iwn_rx_done: MGT`) AND the HAL-independent net80211 leaf
(`ieee80211_recv_auth`) are ALL silent for the SAME trigger
window in which the AP confirms a real Layer (d) AUTH-response
non-ACK plus deauthentication. This is NOT Case A through F:
it means the live management-frame TX/RX path actually
exercised by the Apple ASSOC IOCTL for an Open-System AUTH
on this VM does not pass through any of the four kext-side
leaves the rev5 carrier covers. The rev5 carrier therefore
cannot classify the auth-ACK anomaly into the Case A-F
matrix; it has answered the discriminator with a precise
missing-leaf finding instead.

### Next technical route (seeded by this finding)

The next Stage 1 must instrument the actual Apple80211
ASSOC IOCTL to iwn management-frame TX/RX path that this
finding shows is uncovered. Required coverage:

1. Apple80211 IOCTL dispatch in
   `AirportItlwm/AirportItlwmV2.cpp` (specifically the
   `SIOCSA80211` / ASSOC IOCTL entry the `airportd` code
   path uses for an Open-System AUTH attempt).
2. Non-`iwn_tx` management-frame TX entry points exercised
   between the Apple80211 IOCTL and the iwn ring (any
   helper that bypasses the `iwn_tx` path).
3. The RX delivery / notification path that would receive
   AP AUTH(seq=2) from the iwn hardware up to net80211,
   independent of the `iwn_rx_done` MGT leaf already
   instrumented and shown silent.
4. Any `AirportItlwmAgent` or other shared-side
   participation, if decomp evidence shows it sits on
   the IOCTL path.

The new Stage 1 must include the full field-lifecycle
inventory for the new leaves, the same per-leaf payload
tree the rev5 cycle used, and one Stage 2 trigger to verify
whether the new carrier observes the management-frame TX/RX
events that rev5 missed.

### Status

- Stage 1 rev5 (diagnostic carrier source review): APPROVED
  for after-fix runtime by the Stage 1 rev5 decision
  (`commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-
   auth-ack-iwn-hal-diagnostic-output-carrier-rev5-
   20260519.md`).
- Stage 2 rev5 (after-fix runtime + commit gate): runtime
  evidence ACCEPTED as
  `MISSING_LEAF_KEXT_MGT_TX_RX_PATH_NOT_INSTRUMENTED_BY_REV5`;
  commit gate REJECTED by the Stage 2 rev5 decision
  (`commit-approval/decisions/COMMIT_DECISION_CR-479-stage2-
   after-fix-runtime-auth-ack-iwn-hal-diagnostic-output-
   carrier-rev5-rev5-20260519.md`). The commit blocker was
  that the rev5 packet did not include this Stage 2 finding
  in the tracked source diff and did not request commit.
- This appendix records the accepted Stage 2 finding inside
  the tracked source diff; the request that bundles it
  proposes a commit-ready Stage 1 for the diagnostic carrier
  plus this documentation.
- Commit remains forbidden until a Stage 2 packet for the
  carrier-plus-documentation diff is commit-ready and
  receives `status: APPROVED` with `allow_commit_now: YES`.

## Apple80211 ASSOC -> iwn management TX/RX path probe extension (rev2, 2026-05-19)

The Stage 2 missing-leaf finding above shows the rev5/rev6
carrier's four kext-side leaves (`iwn_tx_done` MGT,
`iwn_rx_done` MGT, `ieee80211_recv_auth`, `iwn_auth` step
boundaries) are ALL silent during a real `networksetup` join
trigger while the controlled FAST_LAB_AP records a Layer (d)
non-ACK + deauthentication for the same guest PMAC. The
rev5/rev6 carrier therefore covers a path that the Tahoe
Apple80211 ASSOC IOCTL does not exercise.

This rev2 extension supersedes the rev1 extension. The rev1
extension was REJECTED at Stage 1 structural review per
`commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-
 apple80211-assoc-iwn-mgt-path-diagnostic-rev1-
 20260519.md` with four blockers. rev2 cures all four:

- BLOCKER_1: rev1's "recovered chain" named the pre-Tahoe
  `AirportItlwm::apple80211_ioctl` override as part of the
  Tahoe live chain. In the reviewed Tahoe source, that
  override is inside `#if __IO80211_TARGET < __MAC_26_0`
  and is NOT compiled for the Tahoe target
  (`__IO80211_TARGET=__MAC_26_0`). rev2 removes that
  claim and names the actual Tahoe-active routing seams.
- BLOCKER_2: rev1 omitted the hidden WCL association
  carrier already documented in the committed source at
  `AirportItlwm/AirportItlwmSkywalkInterface.cpp` (the
  `getAWDL_PEER_TRAFFIC_STATS` slot 0x45 that, when the
  payload length matches
  `TahoeAssociationContracts::isAssocCandidatesPayloadLength`,
  routes the assoc-candidates blob into
  `setWCL_ASSOCIATE` and from there into `associateSSID`).
  rev2 adds probes at both seams and updates the Stage 2
  discriminator with hidden-carrier cases.
- BLOCKER_3: rev1 added no new RX-side leaf independent
  of the silent `iwn_rx_done` MGT leaf. rev2 adds a new
  RX-side leaf at `iwn_rx_phy` entry (the firmware PHY
  notification that, per the existing source comment, MUST
  precede every `MPDU_RX_DONE`). A silent `iwn_rx_phy`
  during the trigger window proves the firmware delivered
  NO RX events at all; a firing `iwn_rx_phy` with a silent
  `iwn_rx_done` MGT proves the firmware delivered non-MGT
  RX activity but no MGT frame matching the rev5 filter.
- BLOCKER_4: rev1 reused the project-owned `IWX_AUTH_DIAG`
  carrier for new ASSOC ingress / state / TX-output leaves
  without updating the stable source comment in
  `itl80211/linux/iwx_diag_log.h`. rev2 extends that
  source comment so its enumerated consumer list matches
  every current call site.

### Recovered Tahoe-active call chain (source evidence)

The reviewed Tahoe source at HEAD `03b37a9` builds with
`__IO80211_TARGET=__MAC_26_0`. The pre-Tahoe
`AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *,
unsigned long, void *, bool, bool)` override is wrapped in
`#if __IO80211_TARGET < __MAC_26_0` and is therefore NOT
part of the live Tahoe chain. Two compiled Tahoe-active
ingress seams remain:

  Tahoe ingress A (BSD ioctl):
    airportd / CoreWLAN userspace
      -> bsdControl / SIOCSA80211, data = apple80211req
         with req_type = APPLE80211_IOC_ASSOCIATE
      -> AirportItlwmSkywalkInterface::processBSDCommand
         (`AirportItlwm/AirportItlwmSkywalkInterface.cpp`
          line 1581 in the committed source; the function
          casts data to `apple80211req *` and forwards
          GET/SET A80211 requests to
          `processApple80211Ioctl` on the same object;
          returns to `super::processBSDCommand` if the
          forwarded handler returns
          `kIOReturnUnsupported`)
      -> AirportItlwmSkywalkInterface::processApple80211Ioctl

  Tahoe ingress B (card-specific ioctl):
    airportd / CoreWLAN userspace
      -> family card-specific ioctl
      -> AirportItlwm::handleCardSpecific
         (`AirportItlwm/AirportItlwmV2.cpp` line 5385; the
          V2-controller override that, for the Tahoe
          target, builds `apple80211req` and forwards via
          `routeTahoeSkywalkIoctl`)
      -> routeTahoeSkywalkIoctl
         (`AirportItlwm/AirportItlwmV2.cpp` line 1300;
          gates by `shouldRouteTahoeSkywalkIoctlReq` and
          then calls
          `sky->processApple80211Ioctl(cmd, req)`)
      -> AirportItlwmSkywalkInterface::processApple80211Ioctl

Both ingress paths converge on the same
`processApple80211Ioctl(UInt cmd, apple80211req *req)` switch
on `req->req_type`. The committed source documents TWO
distinct ASSOC handlers reachable from that switch:

  Public-carrier branch:
    -> case `APPLE80211_IOC_ASSOCIATE`
       routes `cmd == SIOCSA80211` to
       `setASSOCIATE((apple80211_assoc_data *)
                     req->req_data)`
    -> AirportItlwmSkywalkInterface::setASSOCIATE
       (the public-carrier ASSOC handler that, after
        `setAUTH_TYPE` + `setRSN_IE`, calls
        `associateSSID(ad->ad_ssid, ..., importLocalPmk=
        true, externalPmkOwner=false)`)
    -> AirportItlwmSkywalkInterface::associateSSID
       (the WCL/Skywalk WPA / RSN / nwkey configurator
        that writes `ic->ic_des_essid` / `ic_des_bssid`
        and the WPA params on `ic`)

  Hidden WCL-assoc-candidates carrier branch:
    -> case `APPLE80211_IOC_AWDL_PEER_TRAFFIC_STATS`
       routes into
       `AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS(data, length)`
       (line 2829 of `AirportItlwmSkywalkInterface.cpp`;
        the source comment there documents that "Tahoe
        visible APPLE80211_IOC_ASSOCIATE does not fall
        into the public `setASSOCIATE(...)` path. Family
        `getSetHandler(20)` first emits the hidden carrier
        `0x45` with the full `0x3ad8` assoc-candidates
        blob and, when WCL does not absorb it, the
        fallback lands on this slot.")
    -> when `data != nullptr` and
       `TahoeAssociationContracts::isAssocCandidatesPayloadLength(length)`
       evaluates true, the slot calls
       `setWCL_ASSOCIATE(reinterpret_cast<
                          apple80211AssocCandidates *>(data))`
    -> AirportItlwmSkywalkInterface::setWCL_ASSOCIATE
       (line 5163; the hidden-WCL-carrier handler that
        eventually feeds into `associateSSID`)
    -> AirportItlwmSkywalkInterface::associateSSID
       (same WCL configurator as the public branch)

Both branches converge on `associateSSID`. From there:

  -> (gap A) net80211 trigger after `ic_des_essid` is set
     transitions `ic->ic_state` toward `S_AUTH` (classic
     OpenBSD: `ieee80211_node_join` / `ieee80211_end_scan`
     calling `ieee80211_new_state(IEEE80211_S_AUTH, ...)`)
  -> `ieee80211_new_state` ->
     `ic->ic_newstate = iwn_newstate`
     (`itlwm/hal_iwn/ItlIwn.cpp` line 604:
      `ic->ic_newstate = iwn_newstate;`)
  -> `ItlIwn::iwn_newstate` (case `IEEE80211_S_AUTH`)
     -> `ItlIwn::iwn_auth` (rev5 carrier instrumented;
        SILENT in the rev5 Stage 2 trigger)
     -> `IEEE80211_SEND_MGMT(..., IEEE80211_FC0_SUBTYPE_AUTH,
        seq=1)` ->
        `ic->ic_send_mgmt = ieee80211_send_mgmt`
        (`itl80211/openbsd/net80211/ieee80211_proto.c` 119)
     -> `ieee80211_send_mgmt` (`...AUTH`) ->
        `ieee80211_get_auth` -> `ieee80211_mgmt_output`
        (`itl80211/openbsd/net80211/ieee80211_output.c`)
     -> `ieee80211_mgmt_output` assembles the 802.11
        frame header, sets
        `wh->i_addr2 = ic->ic_myaddr` (the PMAC), submits
        via `ifp->if_output` / `ic->ic_start` to the iwn
        TX ring; TX completion fires `iwn_tx_done` (rev5
        carrier, SILENT).

For the RX path:

  -> firmware delivers an RX_PHY notification on every
     received frame (`itlwm/hal_iwn/ItlIwn.cpp` source
     comment on `iwn_rx_done`: "Each MPDU_RX_DONE
     notification must be preceded by an RX_PHY one.")
  -> `ItlIwn::iwn_rx_phy` records the PHY descriptor
     (rev5 carrier did NOT instrument this leaf; rev2
     adds it).
  -> firmware then delivers `IWN_RX_DONE` /
     `IWN_MPDU_RX_DONE` for the 802.11 frame ->
     `ItlIwn::iwn_rx_done` (rev5 carrier instrumented
     with a subtype filter; SILENT for the
     AUTH/ASSOC_RESP/REASSOC_RESP/DEAUTH/DISASSOC subtypes
     in the rev5 Stage 2 trigger).
  -> `ieee80211_inputm` -> net80211 management dispatch
     -> `ieee80211_recv_auth` (rev5 carrier instrumented;
        SILENT in the rev5 Stage 2 trigger).

### Hypotheses motivating the rev2 probe placement

The rev5 evidence shows {iwn_tx_done MGT, iwn_rx_done MGT,
ieee80211_recv_auth, iwn_auth step} all silent during a
real Layer (d) auth-response non-ACK. Three structurally-
distinct hypotheses are consistent with that observation,
and the rev2 probes are placed to disambiguate them on the
next Stage 2 trigger:

  Hypothesis H1: the ASSOC IOCTL never reaches the
    AirportItlwm dispatch on this VM; airportd / CoreWLAN
    drops the request before either of the two compiled
    Tahoe ingress seams sees it, or it routes through a
    different interface (e.g. a virtual interface) on
    which our handlers are not active. In that case,
    probes P_BSD and P_route stay silent.

  Hypothesis H2: the IOCTL reaches the kext via one of
    the compiled Tahoe ingress seams, runs the public-
    carrier branch through `setASSOCIATE` and
    `associateSSID`, but the net80211/iwn state machine
    transition that should drive
    `ieee80211_new_state(IEEE80211_S_AUTH, ...)` never
    fires OR fires but is bypassed by an alternate TX
    path. In that case probes P1..P3 fire on the public
    branch while probes P4/P5 and P6 reveal where the
    path actually ends.

  Hypothesis H3: the IOCTL reaches the kext but the
    ASSOC payload travels through the hidden WCL
    assoc-candidates carrier (`getAWDL_PEER_TRAFFIC_STATS`
    slot 0x45 with the `0x3ad8` assoc-candidates blob
    routed into `setWCL_ASSOCIATE`), not the public
    `setASSOCIATE` slot. In that case probes P_hidden_carrier
    and P_hidden_wcl fire while P1/P2 stay silent and
    P3 (`associateSSID`) still fires because both branches
    converge there. The submitted rev1 discriminator would
    have misclassified this Tahoe-common case as
    "ASSOC IOCTL never reached the kext"; the rev2
    discriminator handles it as a first-class case.

Each rev2 probe is behavior-neutral (no return-value
change, no state mutation, no resource allocation, no
early-return short-circuit) and emits exactly one os_log
record per call.

### Eleven probe leaves in the rev2 ASSOC + RX coverage

The probes share the same project-owned carrier defined
in `itl80211/linux/iwx_diag_log.h`
(`subsystem == "com.zxystd.AirportItlwm"`,
 `category == "iwx.auth_ack"`,
 macro `IWX_AUTH_DIAG(fmt, ...)`). The carrier object
docstring has been extended in this rev2 patch so its
enumerated consumer list matches every current call site.
The carrier initialization, fallback, and unified-log
predicate contract are unchanged from the rev5/rev6
contract; see the existing "Carrier object lifecycle" /
"Carrier observability contract" sections of
`iwx_diag_log.h` for the lifetime rules.

Leaf P_BSD: BSD ASSOC ingress probe
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: inside `processBSDCommand`, immediately after
        `apple80211req *req = static_cast<apple80211req *>
         (data);` and BEFORE the existing
        `isTahoeCurrentLinkProbeReq` block.
  Filter: only emits when
          `req != nullptr &&
           req->req_type == APPLE80211_IOC_ASSOCIATE &&
           cmd == SIOCSA80211`.
  Format: `processBSDCommand_assoc: cmd=0x%x req_type=%d
           ic_state=%d`
  Purpose: prove the SIOCSA80211 APPLE80211_IOC_ASSOCIATE
           IOCTL reaches the Tahoe-active BSD ingress
           seam on this kext.

Leaf P_route: card-specific ASSOC ingress probe
  File: `AirportItlwm/AirportItlwmV2.cpp`
  Site: inside `routeTahoeSkywalkIoctl`, immediately
        after the `shouldRouteTahoeSkywalkIoctlReq` gate
        succeeds and BEFORE the
        `sky->processApple80211Ioctl(cmd, req)` call.
  Filter: only emits when
          `req->req_type == APPLE80211_IOC_ASSOCIATE &&
           isSet (cmd == SIOCSA80211)`.
  Format: `routeTahoeSkywalkIoctl_assoc: cmd=0x%x
           req_type=%d`
  Purpose: prove the SIOCSA80211 APPLE80211_IOC_ASSOCIATE
           IOCTL reaches the Tahoe-active card-specific
           ingress seam (V2 controller) on this kext.

Leaf P1: Apple80211 ASSOC dispatch probe
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: inside `processApple80211Ioctl`, at the
        `case APPLE80211_IOC_ASSOCIATE:` arm BEFORE the
        ternary that routes to `setASSOCIATE` for
        `cmd == SIOCSA80211`.
  Format: `apple80211_assoc_ioctl: cmd=0x%x req_type=%d
           ic_state=%d`
  Purpose: prove the public-branch dispatch arm for
           `APPLE80211_IOC_ASSOCIATE` is entered.

Leaf P2: public setASSOCIATE entry probe
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: at the top of
        `AirportItlwmSkywalkInterface::setASSOCIATE`,
        immediately after the function body opens and
        BEFORE the existing `CR257_PROBE` line.
  Format: `setASSOCIATE: ad_present=%d ssid_len=%u
           ad_mode=%d auth_upper=0x%x auth_lower=%u
           rsn_ie_len=%u ic_state=%d`
  Purpose: prove the public-branch ASSOC handler ran
           with non-NULL `apple80211_assoc_data` and
           record the join mode + auth-type bits.

Leaf P_hidden_carrier: hidden assoc-candidates carrier
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: at the top of
        `AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS`,
        immediately after the function body opens and
        BEFORE the existing source-commented block that
        routes the hidden carrier into `setWCL_ASSOCIATE`.
  Filter: NONE at the probe (it always fires when the
          slot is entered); a derived boolean
          `is_assoc_blob` records whether
          `TahoeAssociationContracts::isAssocCandidatesPayloadLength(length)`
          evaluated true so the discriminator can tell
          ASSOC-blob entries from set-mac-carrier / other
          fallback entries.
  Format: `getAWDL_PEER_TRAFFIC_STATS_hidden_assoc:
           data_present=%d length=0x%x is_assoc_blob=%d
           ic_state=%d`
  Purpose: prove the hidden carrier slot was entered and
           record whether the payload length matches the
           ASSOC-candidates layout (which would route it
           into `setWCL_ASSOCIATE`).
  Redaction: no `data` byte is logged; only length,
             presence flag, and the boolean that recognizes
             the ASSOC blob.

Leaf P_hidden_wcl: hidden WCL ASSOC entry probe
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: at the top of
        `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE`,
        immediately after the function body opens and
        BEFORE the existing `__atomic_add_fetch` of
        `cr237_setWCL_ASSOCIATE_count`.
  Format: `setWCL_ASSOCIATE: candidates_present=%d
           ic_state=%d`
  Purpose: prove the hidden-WCL-carrier handler ran
           (the second of the two ASSOC handlers that
           converge on `associateSSID`).
  Redaction: no `apple80211AssocCandidates *` field byte
             is logged; only the presence flag and
             `ic_state`.

Leaf P3: associateSSID (Skywalk WCL configurator) entry
  File: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  Site: at the top of
        `AirportItlwmSkywalkInterface::associateSSID`,
        immediately after the function body opens and
        BEFORE the existing `__atomic_add_fetch` of
        `cr237_associateSSID_count`.
  Format: `associateSSID_skywalk: ssid_len=%u
           auth_upper=0x%x auth_lower=%u key_len=%u
           import_local_pmk=%d external_pmk_owner=%d
           ic_state=%d`
  Purpose: prove either ASSOC branch reached the WCL
           configurator (both branches converge here)
           and record PMK ownership flags so the next
           stage can distinguish the local-PMK vs
           external-PMK branches.
  Redaction: no SSID character, key byte, BSSID octet,
             or passphrase content; only lengths and
             flag bits.

Leaf P4: ieee80211_send_mgmt AUTH-only entry
  File: `itl80211/openbsd/net80211/ieee80211_output.c`
  Site: at the top of `ieee80211_send_mgmt`, immediately
        after the local variable declarations and BEFORE
        the existing `if (ni == NULL) panic(...)` line.
  Filter: only emits when
          `(type & IEEE80211_FC0_TYPE_MASK) ==
           IEEE80211_FC0_TYPE_MGT &&
           (type & IEEE80211_FC0_SUBTYPE_MASK) ==
           IEEE80211_FC0_SUBTYPE_AUTH`.
  Format: `ieee80211_send_mgmt: subtype=0x%02x
           arg1_seq_status=0x%x arg2=0x%x ic_state=%d`
  Purpose: prove the net80211 management-frame TX entry
           was invoked for an AUTH frame in the trigger
           window.

Leaf P5: ieee80211_mgmt_output AUTH-only entry
  File: `itl80211/openbsd/net80211/ieee80211_output.c`
  Site: at the top of `ieee80211_mgmt_output`, immediately
        after the local `wh` declaration and BEFORE the
        existing `if (ni == NULL) panic(...)` line.
  Filter: same auth-ACK scoping as P4.
  Format: `ieee80211_mgmt_output: subtype=0x%02x
           mbuf_present=%d ic_state=%d`
  Purpose: prove the management-frame TX submission to
           the driver TX path was invoked for an AUTH
           frame.

Leaf P6: iwn_newstate entry
  File: `itlwm/hal_iwn/ItlIwn.cpp`
  Site: at the top of `ItlIwn::iwn_newstate`, immediately
        after the existing
        `XYLog("%s nstate=%d\n", ...)` line.
  Filter: NONE.
  Format: `iwn_newstate: from_state=%d to_state=%d
           mgt_arg=0x%x`
  Purpose: prove the iwn HAL state machine was entered
           with the right transition.

Leaf R1: iwn_rx_phy (RX-side independent coverage)
  File: `itlwm/hal_iwn/ItlIwn.cpp`
  Site: at the top of `ItlIwn::iwn_rx_phy`, immediately
        after the existing `bus_dmamap_sync(...)` line
        (so the `stat->flags` field is already valid for
        read).
  Filter: NONE (every PHY notification is recorded; the
          source comment on `iwn_rx_done` documents that
          "Each MPDU_RX_DONE notification must be
          preceded by an RX_PHY one", so this leaf fires
          once per inbound frame and provides the RX-side
          coverage the committed missing-leaf appendix
          required, independent of the silenced
          `iwn_rx_done` MGT-subtype leaf).
  Format: `iwn_rx_phy: flags=0x%x ic_state=%d`
  Purpose: prove the firmware delivered RX-PHY
           notifications in the trigger window. A silent
           `iwn_rx_phy` means the firmware delivered NO
           RX events at all; a firing `iwn_rx_phy` with
           a silent `iwn_rx_done` MGT means non-MGT RX
           is happening but no MGT frame in the
           rev5/rev6 subtype filter set arrived.
  Redaction: no MAC, IE, or payload byte; only the
             PHY-stat flags integer (RX rate / AGG
             markers; no air/key material) and the
             current `ic_state`.

### Per-field lifecycle for the rev2 payload fields

All eleven probes emit through the `IWX_AUTH_DIAG` macro
(single `os_log` call). There is no shared payload
struct, no allocation, no producer/consumer queue, and
no state-machine field introduced for these probes. The
per-field lifecycle is identical for every value
emitted:

- Producer: live call-site reads the value from on-stack
  arguments, the caller's `apple80211req` payload (which
  the caller owns for the IOCTL call lifetime), the
  caller's `apple80211_assoc_data` /
  `apple80211AssocCandidates` payload (caller-owned),
  `ic->ic_state` (owned by net80211; sampled without
  locking, which is acceptable for an observation-only
  diagnostic record), or `sc->last_rx_stat` /
  `stat->flags` (firmware-delivered, owned by the iwn
  driver). The probe never writes any of these.
- Copy: value passed by value to `os_log` as a
  printf-style varargs argument; `os_log` snapshots the
  value into the unified-log firehose buffer record.
- Mutation: NONE. No probe writes to any kernel state.
- Lifetime: scoped to the `os_log` call. The probe
  owns no allocation, holds no lock, and does not
  extend any caller-argument lifetime.
- State-machine edges: NONE. Each probe is observation-
  only.
- Cleanup edges: NONE.

Per-probe source / sink:

  P_BSD: `cmd`, `req_type`, `ic_state` (sources: function
    argument, `req->req_type`, `fHalService->
    get80211Controller()->ic_state`).
  P_route: `cmd`, `req_type` (sources: function argument,
    `req->req_type`). No `ic_state` because
    `routeTahoeSkywalkIoctl` is a free function in V2
    without direct access to the controller; `ic_state`
    on the converging downstream P1 records the same
    information one call deeper, before any work in
    `processApple80211Ioctl`.
  P1: `cmd`, `req_type`, `ic_state` (sources as in P_BSD).
  P2: `ad_present`, `ssid_len`, `ad_mode`, `auth_upper`,
    `auth_lower`, `rsn_ie_len`, `ic_state` (sources:
    `apple80211_assoc_data *ad` fields, controller
    state).
  P_hidden_carrier: `data_present`, `length`,
    `is_assoc_blob` (derived from
    `TahoeAssociationContracts::isAssocCandidatesPayloadLength(length)`),
    `ic_state`.
  P_hidden_wcl: `candidates_present`, `ic_state`.
  P3: `ssid_len`, `auth_upper`, `auth_lower`, `key_len`,
    `importLocalPmk`, `externalPmkOwner`, `ic_state`
    (sources: associateSSID arguments + controller state).
  P4: `subtype`, `arg1`, `arg2`, `ic_state` (sources:
    `ieee80211_send_mgmt` arguments + `ic->ic_state`).
  P5: `subtype`, `mbuf_present`, `ic_state` (sources:
    `ieee80211_mgmt_output` arguments + `ic->ic_state`).
  P6: `from_state` (sampled `ic->ic_state` BEFORE
    transition), `to_state` (`nstate` argument),
    `mgt_arg` (`arg` argument).
  R1: `flags` (`letoh16(stat->flags)` from
    `iwn_rx_stat`, firmware-delivered; owned by the iwn
    RX path, copied by value), `ic_state` (`sc->sc_ic
    .ic_state`).

### Stage 2 trigger plan for rev2

1. Build, install, and reboot once with the rev2 kext
   (`scripts/build_tahoe.sh
    /System/Library/KernelCollections/BootKernelExtensions.kc`
   to also run the explicit BootKC symbol check, then
   `kmutil create --new aux` and the atomic swap chain
   documented in the rev5 evidence root).
2. Verify BOTH carrier smoke markers
   (`itlwm: smoke_marker iwn_attach OK` and
   `itlwm: smoke_marker AirportItlwm_start OK`) are
   visible in the unified log under
   `subsystem == "com.zxystd.AirportItlwm" AND
    category == "iwx.auth_ack"` BEFORE running any join
   trigger.
3. Capture pre-trigger guest baseline (`ifconfig`,
   `kmutil showloaded`, panic check, log freshness
   baseline).
4. Capture pre-trigger FAST_LAB_AP `hostapd.log` line
   count.
5. Fire EXACTLY ONE
   `networksetup -setairportnetwork en1
    <FAST_LAB_AP_SSID_redacted>
    <FAST_LAB_AP_PASS_redacted>` invocation, wait for
   the user-visible join result to settle, and record
   its return string (with the SSID redacted).
6. Capture the trigger-window guest unified-log slice
   under the project carrier predicate; the slice must
   include records for the rev2 leaves
   {P_BSD, P_route, P1, P2, P_hidden_carrier,
    P_hidden_wcl, P3, P4, P5, P6, R1} AND the
   rev5/rev6 leaves (`iwn_tx_done` MGT,
   `iwn_rx_done` MGT, `ieee80211_recv_auth`,
   `iwn_auth` steps).
7. Capture the FAST_LAB_AP `hostapd.log` slice for the
   same window; the Layer (d) records for the guest
   PMAC must be cross-referenced to the same
   chronological line numbers as in the rev5/rev6
   Stage 2 evidence.
8. Capture post-trigger guest state (`ifconfig en1`
   PMAC, `kmutil showloaded`, panic check, no second
   trigger).
9. Map the captured rev2 probe firings to the
   discriminator below, OR file a precise blocker
   explaining what is missing.

### rev2 discriminator (Stage 2 case classification)

Let
  IN := {P_BSD, P_route}
  PUB := {P1, P2}
  HIDDEN := {P_hidden_carrier (with is_assoc_blob=1),
              P_hidden_wcl}
  CONV := {P3}
  TX_NET := {P4, P5}
  STATE := {P6}
  TX_FW := {`iwn_tx_done` MGT}
  RX_FW := {R1}
  RX_NET := {`iwn_rx_done` MGT,
              `ieee80211_recv_auth`}
  AP := the FAST_LAB_AP Layer (d) non-ACK/deauth records
        for the guest PMAC.

Cases:

  Case R-A: IN entirely silent.
    Interpretation: the SIOCSA80211 APPLE80211_IOC_ASSOCIATE
    IOCTL never reached either Tahoe-active ingress seam
    on this VM. Next route: instrument the airportd /
    CoreWLAN userspace or the interface selection on the
    host side; the kext is not the live path.

  Case R-B: IN fires (one or both), PUB silent and HIDDEN
    silent.
    Interpretation: the IOCTL reached the kext but
    `processApple80211Ioctl` did not route to the
    `APPLE80211_IOC_ASSOCIATE` arm and did not route to
    `getAWDL_PEER_TRAFFIC_STATS` with an ASSOC-blob
    payload. Next route: re-examine
    `processApple80211Ioctl` for other ASSOC-bearing
    `req_type` values.

  Case R-C_pub: IN fires, PUB fires, HIDDEN silent.
    Interpretation: the public ASSOC branch is the live
    Tahoe path. The previously-blocking rev1 R-A/R-B
    misclassification risk is removed.

  Case R-C_hidden: IN fires, PUB silent, HIDDEN fires.
    Interpretation: the hidden WCL assoc-candidates
    carrier is the live Tahoe path. This is the case
    the rev1 discriminator would have misclassified as
    R-A; rev2 makes it first-class.

  Case R-C_both: IN fires, PUB fires, HIDDEN fires.
    Interpretation: both ASSOC branches fire (possible
    if airportd issues both legacy and new requests).
    The Stage 2 result is still distinguished by
    whether CONV (P3) fires only once or twice.

  Case R-D: any PUB/HIDDEN branch fires, CONV silent.
    Interpretation: the entered branch early-returned
    before `associateSSID`. For PUB: see the
    `setASSOCIATE` early-return conditions
    (`ic_state < S_SCAN`, already in ASSOC/AUTH, NULL
    ad, IBSS). For HIDDEN: see `setWCL_ASSOCIATE`
    early-return conditions. Next route: instrument the
    specific early-return.

  Case R-E: IN, PUB or HIDDEN, CONV, STATE all fire
    (P6 records to_state == `IEEE80211_S_AUTH`), but
    TX_NET and TX_FW silent.
    Interpretation: the iwn HAL entered AUTH but
    `ieee80211_send_mgmt(AUTH)` was never invoked.
    Either `iwn_auth` aborted before the AUTH frame was
    built (Layer (c1) records will show how far it got),
    OR an alternate AUTH TX path bypasses
    `ieee80211_send_mgmt` / `ieee80211_mgmt_output`.
    Next route: re-examine `iwn_auth` and the iwn TX
    management path.

  Case R-F: IN, PUB or HIDDEN, CONV, STATE, P4 fire,
    P5 silent OR TX_FW silent.
    Interpretation: net80211 send path was entered but
    the iwn driver did not actually submit the frame to
    the hardware ring (or the submission completed
    silently). Next route: instrument iwn `start` /
    `transmit` between `ieee80211_mgmt_output` and the
    iwn TX ring submission.

  Case R-G: IN, PUB or HIDDEN, CONV, STATE, P4, P5,
    TX_FW all fire, but RX_FW (R1) silent in the trigger
    window while AP records Layer (d) non-ACK.
    Interpretation: the iwn TX path submitted the AUTH
    frame and the firmware reported completion, but no
    RX activity returned. This is the AP-side anomaly
    the rev5 carrier was originally designed to classify
    (Case A-F per the existing "Probe interpretation
    matrix"); rev2 has now removed the missing-leaf
    blocker that prevented that classification.

  Case R-H: IN, PUB or HIDDEN, CONV, STATE, P4, P5,
    TX_FW all fire, RX_FW (R1) fires (firmware delivered
    RX) but RX_NET silent (no MGT/AUTH delivered to
    net80211).
    Interpretation: firmware is receiving frames but not
    delivering AUTH to net80211. Next route: examine the
    iwn RX MPDU dispatch between `iwn_rx_phy` and
    `iwn_rx_done`.

  Missing-leaf result: the probes plus the four rev5/rev6
    leaves do not cover what actually fires; this means
    another live path exists (e.g. AirportItlwmAgent
    or a non-Skywalk interface). File a precise blocker
    rather than a Case R-A..R-H result.

### Why existing logs / source / reference evidence cannot answer this without the new rev2 probes

- The rev5/rev6 carrier did NOT instrument the two
  Tahoe-active ASSOC ingress seams (`processBSDCommand`
  and `routeTahoeSkywalkIoctl`), the public-branch
  setASSOCIATE handler, the hidden WCL assoc-candidates
  carrier (`getAWDL_PEER_TRAFFIC_STATS` + `setWCL_ASSOCIATE`),
  the Skywalk `associateSSID` configurator, the
  net80211 management TX entry / output, the iwn HAL
  state-machine entry, or the iwn `iwn_rx_phy` RX
  notification leaf. The accepted Stage 2 rev5/rev6
  evidence only shows the four covered leaves silent
  and the AP-side Layer (d) records a real auth-response
  non-ACK; it cannot distinguish among R-A..R-H above.
- The Tahoe-active `processBSDCommand` and
  `routeTahoeSkywalkIoctl` ingress paths are reachable
  only on the Tahoe target
  (`__IO80211_TARGET=__MAC_26_0`); the pre-Tahoe
  `AirportItlwm::apple80211_ioctl` override the rev1
  request mistakenly named is wrapped in
  `#if __IO80211_TARGET < __MAC_26_0` and is NOT
  compiled for the Tahoe target.
- The hidden WCL assoc-candidates carrier is already
  documented in the committed source comment at the top
  of `AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS`,
  but neither the rev5/rev6 carrier nor any other
  pre-rev2 runtime probe records when that slot
  actually carries the ASSOC blob; only a runtime probe
  at each step can tell which branch the Tahoe
  `networksetup` join exercises.
- The existing `XYLog`-based legacy logs (e.g.
  `XYLog("%s nstate=%d\n", ...)` at `iwn_newstate`,
  `XYLog("DEBUG %s routing hidden-assoc carrier ...")`
  at `getAWDL_PEER_TRAFFIC_STATS`, `XYLog("DEBUG %s
  cmd=0x%x ...")` at `processBSDCommand`) emit through
  the project's legacy log path; they are NOT visible
  via the project-owned unified-log carrier predicate
  the auditor accepts as the durable Stage 2 evidence
  channel.
- The Apple `airportd` / `CoreWLAN` userspace is closed
  source on Tahoe; its IOCTL dispatch path cannot be
  observed by any local source-only check on the guest
  itlwm tree.
- The iwn driver's own `iwn_rx_phy` source comment
  states that "Each MPDU_RX_DONE notification must be
  preceded by an RX_PHY one", so an R1 probe at
  `iwn_rx_phy` is the most upstream RX-side leaf
  available inside the driver; only a runtime sample
  at that leaf can prove whether the firmware delivered
  any RX activity in the trigger window, independent
  of the silenced `iwn_rx_done` MGT-subtype leaf.

## Stage 2 runtime finding (rev7 carrier, 2026-05-19 trigger)

The rev7 carrier was installed and one controlled join
trigger was executed against the FAST_LAB_AP host on
2026-05-19. The accepted runtime classification is:

  **CASE_A_WITH_HIDDEN_WCL_INGRESS_AND_DIRECT_IWN_TX_PATH**

Case A here means the iwn HAL fired its MGT TX
completion for the STA-originated AUTH(seq=1) frame
with `txfail=1 ackfailcnt=16` (firmware reports the AP
did NOT L2-ACK the STA's AUTH request) — the original
"Probe interpretation matrix" Case A. The rev7 carrier
also identified a previously-undocumented Tahoe
dispatch seam: the live Apple80211 ASSOC IOCTL on this
VM reaches `setWCL_ASSOCIATE` directly, NOT via any of
the probed public ingress paths
(`processBSDCommand`, `routeTahoeSkywalkIoctl`,
`processApple80211Ioctl` case `APPLE80211_IOC_ASSOCIATE`,
or public `setASSOCIATE`), and the iwn TX path is
exercised without entering either
`ieee80211_send_mgmt` / `ieee80211_mgmt_output` or
`iwn_newstate`.

### Trigger window and identity

- Trigger: ONE `networksetup -setairportnetwork en1
  <FAST_LAB_AP_SSID_redacted>
  <FAST_LAB_AP_PASS_redacted>` invocation,
  `2026-05-19T03:14:00Z` to `2026-05-19T03:14:22Z`
  (22 s), returning user-visible
  `Failed to join network <FAST_LAB_AP_SSID_redacted>.
   Error: -3912 com.apple.wifi.apple80211API.error
   error -3912.`
- Built kext binary sha256
  `3913cf9a1b4f65ac0bafe9e54aadc3df525093e9d8cce1615bea83c3a2eb6e2b`,
  loaded UUID `6A4F55B6-A0FA-3780-9FA3-DB39FEF30FD0`,
  confirmed by `kmutil showloaded` after reboot.
- Boot smoke markers visible BEFORE the trigger under
  the project predicate
  `subsystem == "com.zxystd.AirportItlwm" AND
   category == "iwx.auth_ack"`:
    `itlwm: smoke_marker iwn_attach OK` at
      `2026-05-19 06:12:40.505060+0300`;
    `itlwm: smoke_marker AirportItlwm_start OK` at
      `2026-05-19 06:12:40.533138+0300`.
  (The `smoke_marker iwx_attach OK` marker is not
  expected on this iwn-only device `0x088E`.)
- en1 PMAC rotation: pre-trigger (post-reboot
  persistent) `8e:0b:d6:24:82:bd`; post-trigger
  `4e:37:20:57:6e:b0`. The post-trigger PMAC is
  the one airportd issued for the JOIN attempt and
  is the cross-layer attribution key for this
  trigger.
- One trigger only; no retry, polling loop, forced
  callback, or state forcing; no second install,
  kext reload, or join.

### Trigger-window probe firings

Trigger-window slice
`commit-approval/runtime_evidence/CR-479-stage2-after-fix-runtime-apple80211-assoc-iwn-mgt-path-diagnostic-rev7-20260519/40_join/04_guest_iwx_authack_log.txt`
(92 lines: 84 `iwn_rx_phy` records + 4 substantive
records). Substantive records:

  06:14:01.266188 +0300
    `itlwm: getAWDL_PEER_TRAFFIC_STATS_hidden_assoc:
     data_present=1 length=0x9 is_assoc_blob=0
     ic_state=1`
    -> hidden slot entered with `length=0x9`, NOT the
       documented `0x3ad8` assoc-candidates payload
       length; this is NOT the seam carrying the
       live ASSOC.

  06:14:01.266272 +0300
    `itlwm: setWCL_ASSOCIATE:
     candidates_present=1 ic_state=1`
    -> hidden WCL ASSOC handler entered DIRECTLY,
       84 microseconds after the unrelated
       `getAWDL_PEER_TRAFFIC_STATS` fallback. The
       documented fallback at
       `getAWDL_PEER_TRAFFIC_STATS` is NOT the path
       that reached `setWCL_ASSOCIATE` (the lengths
       do not match). An INDEPENDENT dispatch reaches
       `setWCL_ASSOCIATE`. This is the live Tahoe
       ASSOC handler.

  06:14:01.266309 +0300
    `itlwm: associateSSID_skywalk: ssid_len=16
     auth_upper=0x8 auth_lower=1 key_len=0
     import_local_pmk=0 external_pmk_owner=1
     ic_state=1`
    -> Skywalk associateSSID configurator entered
       37 microseconds after `setWCL_ASSOCIATE`.
       `ssid_len=16` matches the 16-character
       `<FAST_LAB_AP_SSID_redacted>`.
       `auth_upper=0x8` =
       `APPLE80211_AUTHTYPE_WPA2_PSK`;
       `auth_lower=1` =
       `APPLE80211_AUTHTYPE_OPEN`. `key_len=0` (no
       key bytes flow through this probe; PMK is
       externally owned per
       `import_local_pmk=0 / external_pmk_owner=1`).

  06:14:14.996220 +0300
    `itlwm: iwn_tx_done: MGT subtype=0xb0
     peer=80:e4:ba:20:ef:f9 auth_seq=0x0001
     txfail=1 ackfailcnt=16 rate=0x0a rflags=0x42
     len=30`
    -> iwn HAL TX completion for the STA-originated
       AUTH(seq=1) management frame, fired 13.7 s
       after `associateSSID_skywalk`.
       `subtype=0xb0` =
       `IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH`.
       `peer=80:e4:ba:20:ef:f9` = AP BSSID (matches
       the host `iw dev wlp0s20f3` output for the
       FAST_LAB_AP host adapter).
       `auth_seq=0x0001` = AUTH transaction
       sequence 1 (the STA's AUTH request).
       `txfail=1` = firmware reports TRANSMIT
       FAILURE; `ackfailcnt=16` = the firmware
       retried the frame 16 times and got no L2
       ACK. This is the Layer (a) leaf the existing
       "Probe interpretation matrix" Case A
       describes.

Probes silent during the trigger window:

- P_BSD `processBSDCommand_assoc` — live ASSOC IOCTL
  does NOT come through `processBSDCommand`.
- P_route `routeTahoeSkywalkIoctl_assoc` — live
  ASSOC IOCTL does NOT come through
  `handleCardSpecific` -> `routeTahoeSkywalkIoctl`.
- P1 `apple80211_assoc_ioctl` — the public
  `processApple80211Ioctl` switch arm for
  `APPLE80211_IOC_ASSOCIATE` is NOT entered.
- P2 `setASSOCIATE` — the public setASSOCIATE
  handler is NOT entered.
- P4 `ieee80211_send_mgmt` AUTH-only — net80211
  management-frame TX entry is NOT invoked for the
  AUTH frame.
- P5 `ieee80211_mgmt_output` AUTH-only — net80211
  management-frame TX output is NOT invoked either.
- P6 `iwn_newstate` — silent during the trigger
  window (P6 records exist at boot but none between
  06:13:55 and 06:14:30 +0300); the iwn HAL state
  machine is NOT transitioned for the AUTH attempt.
- `iwn_auth` Layer (c1) — iwn HAL auth function
  NOT entered.
- `iwn_rx_done` MGT (rev5/rev6 Layer b) — silent;
  consistent with the AP-side Layer (d) record
  showing the AP did NOT transmit AUTH(seq=2).
- `ieee80211_recv_auth` Layer (c) — silent;
  consistent with `iwn_rx_done` MGT silent.

R1 `iwn_rx_phy` — 84 records during the trigger
window, consistent with normal scan/beacon RX
activity.

### Host AP Layer (d) records

Host hostapd line counts: pre-trigger = 65,
post-trigger = 67, delta = 2. The two appended
records (lines 66..67 of
`/tmp/itlwm-lab-ap/hostapd.log`):

  wlp0s20f3: STA 4e:37:20:57:6e:b0 IEEE 802.11:
    did not acknowledge authentication response
  wlp0s20f3: STA 4e:37:20:57:6e:b0 IEEE 802.11:
    did not acknowledge authentication response

PMAC attribution: guest `en1` POST-trigger PMAC =
AP-side hostapd STA-MAC for the trigger window
appends = `4e:37:20:57:6e:b0`. Pre-trigger
(post-reboot) en1 PMAC was `8e:0b:d6:24:82:bd`;
airportd rotated the en1 MAC at JOIN time. Raw
airportd unified-log records for the trigger
window REDACT the PMAC value (Apple anonymization
in airportd/CoreWLAN public log output); raw
airportd is NOT cited as a textual source for the
literal PMAC.

### Material classification and residual route

The rev7 result resolves the rev5/rev6 missing-leaf
finding
(`MISSING_LEAF_KEXT_MGT_TX_RX_PATH_NOT_INSTRUMENTED_BY_REV5`)
PARTIALLY:

- The iwn TX path IS exercised on this trigger
  (`iwn_tx_done` MGT auth_seq=1 with
  `txfail=1 ackfailcnt=16`); the rev5/rev6 silent
  reading of `iwn_tx_done` MGT may have been a
  consequence of the live trigger not reaching the
  iwn TX path on that cycle, or a result of a
  different airportd dispatch path on that VM /
  trigger.
- The live ASSOC handler reached is `setWCL_ASSOCIATE`
  via a hidden Tahoe dispatch seam that bypasses
  every public `processApple80211Ioctl` ingress
  instrumented by rev7 AND bypasses the documented
  `getAWDL_PEER_TRAFFIC_STATS` fallback (the
  fallback's `length=0x9` reading does not match
  the `0x3ad8` assoc-candidates contract).
- The iwn TX path between
  `AirportItlwmSkywalkInterface::associateSSID` and
  `iwn_tx_done` does NOT pass through
  `ieee80211_send_mgmt`, `ieee80211_mgmt_output`, or
  `iwn_newstate`; the direct iwn TX entry point
  remains unidentified by source recovery alone.

The residual route is two-fold:

1. **Independent caller of `setWCL_ASSOCIATE`**:
   recover and instrument the dispatch seam that
   reaches
   `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE`
   in the live Tahoe trigger window WITHOUT passing
   through the documented
   `getAWDL_PEER_TRAFFIC_STATS` fallback. This is
   likely a different vtable slot in
   `IO80211InfraProtocol` / Tahoe's WCL family
   dispatcher, or a method called from
   `processBSDCommand` / `routeTahoeSkywalkIoctl`
   on a request type other than
   `APPLE80211_IOC_ASSOCIATE` /
   `APPLE80211_IOC_AWDL_PEER_TRAFFIC_STATS`.
2. **Direct iwn management TX path between
   `associateSSID` and `iwn_tx_done`**: recover
   and instrument the call sequence that pushes
   the STA-originated AUTH(seq=1) frame to the
   iwn TX ring without entering
   `ieee80211_send_mgmt`, `ieee80211_mgmt_output`,
   or `iwn_newstate`. The next Stage 1 carrier
   must add probes at the actual iwn driver
   ingress between `setWCL_ASSOCIATE` /
   `associateSSID` and `iwn_tx_done`.

### Status

- Stage 1 rev7 (diagnostic carrier source review):
  APPROVED_FOR_AFTER_FIX_RUNTIME by the Stage 1
  rev7 decision sha
  `4383356b1d2f83fec5de6d1e71f6e3b4b16c3fa3b2c2be1f7eaa49966754b351`.
- Stage 2 rev7 (after-fix runtime + commit gate):
  runtime evidence ACCEPTED as
  `CASE_A_WITH_HIDDEN_WCL_INGRESS_AND_DIRECT_IWN_TX_PATH`;
  commit gate REJECTED. The commit blockers were
  that the rev7 packet did not include the Stage 2
  finding in the tracked source diff and that the
  request overclaimed commit-readiness for a
  finding with a remaining residual route.
- This appendix records the accepted Stage 2 rev7
  finding inside the tracked source diff.
- Commit remains forbidden until the residual
  route above is also covered by a future
  diagnostic Stage 1 and that Stage 2 packet
  reaches commit-ready status.

## Residual route Stage 1 (rev1, 2026-05-19 plan)

The Stage 2 rev8 control-plane decision rejected the
Stage 2 commit gate (BLOCKER_1 profile boundary plus
BLOCKER_2 residual-route open) and explicitly routed
the next coder task as a bounded residual-route
diagnostic Stage 1 instrumenting both open elements
named in the rev7 finding (see the prior section
"Stage 2 runtime finding (rev7 carrier, 2026-05-19
trigger)"):

  1. The independent caller of `setWCL_ASSOCIATE`
     (the rev7 evidence proved `setWCL_ASSOCIATE`
     fired without P_hidden_carrier reaching its
     routing site, because the assoc-blob length
     mismatch in `getAWDL_PEER_TRAFFIC_STATS`
     (0x9 vs 0x3ad8) kept the documented dispatcher
     silent).
  2. The direct iwn management TX path between
     `associateSSID` and `iwn_tx_done` (the rev7
     evidence proved `iwn_tx_done MGT auth_seq=1`
     fired while P4 (`ieee80211_send_mgmt`), P5
     (`ieee80211_mgmt_output`), P6 (`iwn_newstate`),
     and `iwn_auth` were all silent, implying the
     AUTH frame entered the iwn HAL TX path without
     traversing the documented net80211 mgmt path).

This residual-route Stage 1 packet adds three
incremental probes to the existing diagnostic
carrier; it does NOT remove or modify any rev2..rev8
probe. The packet is registered as a NEW request id
on the same correlation chain
`CR-479-stage1-apple80211-assoc-iwn-mgt-path-residual-route-diagnostic-rev1-20260519`
because the auditor decision required "a bounded
residual-route task" rather than a supersede of
rev8.

### Residual-route probe additions

| # | File | Site | Field added |
|---|------|------|-------------|
| R_caller_wcl | `AirportItlwm/AirportItlwmSkywalkInterface.cpp` | `setWCL_ASSOCIATE` entry (P_hidden_wcl payload extension) | `caller=%p` from `__builtin_return_address(0)` |
| R2 | `itlwm/hal_iwn/ItlIwn.cpp` | `_iwn_start_task` immediately after a successful `mq_dequeue(&ic->ic_mgtq)` returning non-NULL | MGT-AUTH `peer` + `auth_seq` for every AUTH MGT frame pulled from `ic_mgtq` |
| R3 | `itlwm/hal_iwn/ItlIwn.cpp` | `iwn_tx` entry after the existing capture-before-mbuf_adj identity block | MGT-AUTH `peer` + `auth_seq` for every AUTH MGT frame that reaches the iwn HAL TX path |

The R_caller_wcl payload field is an extension of the
existing P_hidden_wcl probe (same site, same filter,
same emission point); it adds a single integer field
to the existing format string and is therefore
behavior-neutral by the same argument that justified
the rev2..rev8 probes.

R2 and R3 are NEW probes. Both are filtered to MGT-
AUTH frames only (type `IEEE80211_FC0_TYPE_MGT` +
subtype `IEEE80211_FC0_SUBTYPE_AUTH`) and read fields
that are already in scope at the insertion point. R2
reads the dequeued mbuf via `mtod()` on the still-
untrimmed mbuf (the same safe pattern used by the
existing iwn_tx / iwx_tx capture-before-mbuf_adj
blocks). R3 reuses the already-captured `diag_peer`
and `diag_auth_seq` locals.

### Residual-route discriminator matrix

| R_caller_wcl `caller` address resolves into | R2 fires for same `auth_seq` | R3 fires for same `auth_seq` | iwn_tx_done MGT `auth_seq` fires | Case |
|---------------------------------------------|------------------------------|------------------------------|----------------------------------|------|
| our kext, near `getAWDL_PEER_TRAFFIC_STATS` line 2876 | YES | YES | YES | RR-A documented mgtq route, documented dispatcher |
| our kext, near `getAWDL_PEER_TRAFFIC_STATS` line 2876 | NO | YES | YES | RR-B documented dispatcher reaches `setWCL_ASSOCIATE`, but the AUTH frame is injected directly into `iwn_tx` after `setWCL_ASSOCIATE` |
| system Apple80211 binary | YES | YES | YES | RR-C independent Apple-side caller of `setWCL_ASSOCIATE`, but AUTH frame still travels documented `ic_mgtq` route |
| system Apple80211 binary | NO | YES | YES | RR-D independent Apple-side caller of `setWCL_ASSOCIATE` AND AUTH frame injected directly into `iwn_tx` |
| (any) | (any) | NO | YES | RR-E `iwn_tx_done` MGT auth_seq fires without R3 (impossible if iwn_tx is the only entry to the TX ring; a fire here proves a second TX-ring entry path) |
| (any) | YES | NO | YES | RR-F `iwn_tx` is bypassed in favor of a direct TX ring write |

The rev7 evidence is consistent with the Case RR-D
expectation but does not yet have the discriminator
probes to confirm it. The Stage 2 packet that follows
the Stage 1 approval of this rev1 residual-route
request will run a single live trigger, capture the
new probe firings, and decide which RR-* case
actually fired.

### Security and privacy (residual-route additions)

The R_caller_wcl `caller` field is a kernel return
PC. It is a pure-integer instruction address; it is
not a memory copy of any data or key material, it
does not leak any string, and the address has no PII.
The address is constrained to whatever instruction
followed the `setWCL_ASSOCIATE` callsite in the
caller; for the documented dispatcher it points just
past the call site at
`AirportItlwm/AirportItlwmSkywalkInterface.cpp` line
2876, and for the independent Apple-side dispatcher
it points into the system Apple80211 binary.

R2 and R3 emit the management-frame receiver MAC
(`addr1`) and the AUTH transaction-sequence integer,
both of which are structurally public 802.11 control-
plane values already approved on the rev2..rev8
carrier-scope contract. No 802.11 data-frame payload,
key material, or `apple80211_assoc_data` body byte
flows through these probes.

### Source delta inventory (versus rev8)

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`:
  enhance the existing P_hidden_wcl probe payload at
  `setWCL_ASSOCIATE` entry to add a single `caller=%p`
  field captured via `__builtin_return_address(0)`.
- `itlwm/hal_iwn/ItlIwn.cpp`:
  insert R2 probe at `_iwn_start_task` after a
  successful `mq_dequeue(&ic->ic_mgtq)`; insert R3
  probe at `iwn_tx` entry after the existing capture-
  before-mbuf_adj identity block. Both new probes are
  filtered to MGT-AUTH only.
- `itl80211/linux/iwx_diag_log.h`:
  extend the carrier consumer list under "Bounded
  usage" to enumerate the R_caller_wcl payload
  extension and the R2 / R3 new probes; no macro,
  carrier handle, security/privacy contract, or
  lifecycle contract change.
- `analysis/iwx_auth_ack_boundary.md` (this section):
  document the residual-route plan, probe inventory,
  RR-A..RR-F discriminator matrix, and security
  contract for the new probes.

### Stage 2 plan after Stage 1 rev1 approval

The Stage 2 packet that follows this Stage 1 rev1
approval will:

  - build, install, reboot, and verify the rev1
    diagnostic carrier per the existing rev2..rev8
    install / reboot / smoke verification protocol
    (kmutil auxiliary KernelCollection rebuild +
    swap; smoke markers
    `smoke_marker iwn_attach OK` +
    `smoke_marker AirportItlwm_start OK` verified
    before the trigger);
  - run a single Tahoe-active JOIN trigger against
    the same FAST_LAB_AP fixture used in the rev7
    runtime (no new trigger profile);
  - capture the iwx.auth_ack trigger-window slice
    via the same `log show ... predicate
    'subsystem == "com.zxystd.AirportItlwm" AND
     category == "iwx.auth_ack"'` flow;
  - map the captured probe firings onto the RR-A..
    RR-F matrix above and decide the actual
    residual-route case;
  - record the cross-layer correlation between
    R_caller_wcl `caller` address and the kext load
    address / Apple80211 symbol map to resolve
    residual-route element 1.

This Stage 2 packet will continue to satisfy the
operator constraint
`commit_requested: NO` while the residual route is
not yet closed; the FINAL commit-ready packet will
only be assembled when ALL residual route elements
have been mapped to a concrete reference contract
in either our kext or the documented Apple80211
boundary.


## Stage 2 rev1 runtime result (carry-forward into tracked diff)

This section records the rev1 Stage 2 after-fix
runtime result inside tracked analysis so that any
subsequent decision packet can cite the finding
from the diff itself (curing
BLOCKER_5_MATERIAL_RUNTIME_FINDING_NOT_IN_TRACKED_DIFF
of the residual-route blocker-sweep correction).

Authorization that produced this runtime:

  Stage 1 rev1 residual-route decision sha256:
    a309adba6e39555c3b4bb74c881b9438d9886dc84035e7d4d7b317387812647e
  Stage 1 rev1 approval constraints:
    live_runtime_launch_authorized: YES
    authorized_runtime_scope: build, install, reboot,
      smoke check, and exactly one FAST_LAB_AP join
      trigger for this residual-route diagnostic
    additional_join_triggers_authorized: NO
    kext_unload_authorized: NO
    commit_authorized: NO

Loaded kext identity (verified post-reboot via
`kmutil showloaded --bundle-identifier
com.zxystd.AirportItlwm`):

  bundle id: com.zxystd.AirportItlwm
  version:   2.4.0
  UUID:      D3C9A298-C11D-3E49-9382-B063979E4665
  binary sha256:
             652f9ecb52f59d01ebf3800034d357e58346a4615da52cbe683c0aa74b7b85f6
  load base: 0xffffff7f9661c000
  size:      0xf125a2 bytes
  Both values match the Stage 1 rev1 approved kext.

Post-boot smoke markers visible via the
`subsystem == "com.zxystd.AirportItlwm" AND
category == "iwx.auth_ack"` predicate:
  `smoke_marker iwn_attach OK`         at 07:18:20.238804+0300
  `smoke_marker AirportItlwm_start OK` at 07:18:20.271220+0300

Single FAST_LAB_AP JOIN trigger:
  T-pre:   2026-05-19T04:20:21Z (07:20:21 local)
  T-post:  2026-05-19T04:20:42Z (07:20:42 local)
  networksetup result:
    `Failed to join network
     <FAST_LAB_AP_SSID_redacted>.
     Error: -3912 The operation couldn't be
     completed.`
  pre-trigger en1 PMAC:  7e:c7:81:45:2a:27
  post-trigger en1 PMAC: b6:af:68:ec:39:25
    (Apple PMAC randomization rotates the MAC at
     JOIN; same pattern as the rev7 Stage 2 trigger)

Host FAST_LAB_AP hostapd evidence for guest PMAC:
  2026-05-19T07:20:35+0300 hostapd:
    STA b6:af:68:ec:39:25 IEEE 802.11:
    did not acknowledge authentication response
  2026-05-19T07:20:40+0300 hostapd:
    STA b6:af:68:ec:39:25 IEEE 802.11:
    did not acknowledge authentication response
  2026-05-19T07:25:35+0300 hostapd:
    STA b6:af:68:ec:39:25 IEEE 802.11:
    deauthenticated due to inactivity
    (timer DEAUTH/REMOVE)

  Hostapd interpretation: the AP sends an 802.11
  AUTH RESPONSE only after receiving an 802.11
  AUTH REQUEST from the STA. The hostapd "did not
  acknowledge" message fires when the STA's L2
  ACK does not arrive within the firmware ACK
  timeout. Therefore the guest radio emitted at
  least one AUTH REQUEST per logged event.

Kext-side probe firings during the trigger window:
  All non-`iwn_rx_phy` `iwx.auth_ack` records since
  boot:
    `smoke_marker iwn_attach OK`           07:18:20.238804
    `smoke_marker AirportItlwm_start OK`   07:18:20.271220
    `iwn_newstate from_state=0 to_state=1
     mgt_arg=0xffffffff`                   07:18:20.614921

  NO firing in the trigger window for:
    R_caller_wcl (`setWCL_ASSOCIATE` entry)
    R2 (`_iwn_start_task` mgtq MGT-AUTH dequeue)
    R3 (`iwn_tx` entry MGT-AUTH)
    `iwn_tx_done` MGT auth_seq (existing probe)
    P_BSD / P_route / P1 / P2 (Apple80211 ingress)
    P_hidden_carrier
    P3 (`associateSSID_skywalk`)
    P4 / P5 (net80211 AUTH TX)
    P6 (post-boot `iwn_newstate` transitions)
    `iwn_auth` step-boundary probes
    `iwn_tx_done` MGT, `iwn_rx_done` MGT,
    `ieee80211_recv_auth`.

  `iwn_rx_phy` (R1) fires throughout (radio alive).

PMAC attribution chain: guest post-trigger en1
PMAC `b6:af:68:ec:39:25` == AP hostapd STA-MAC
`b6:af:68:ec:39:25` (YES). Raw airportd records
NOT cited because Apple redacts the rotated PMAC
in airportd logs; en1 PMAC is read directly via
`ifconfig en1` on the guest.

`caller=` address visibility: R_caller_wcl probe
did NOT fire during the trigger window. No
`caller=<PC>` address was captured. The
documented symbolization method (file
`60_symbolize/00_symbolize_summary.txt`) was
exercised against the loaded kext base
`0xffffff7f9661c000` to confirm the
offset/atos recipe is sound, but no address was
available to symbolize.

This rev1 Stage 2 runtime is recorded here as
durable routing evidence; it is not a closure of
the residual route.

## NARROWING preamble for the firmware host-command boundary work below

The sections following this preamble cover a
DELIBERATELY NARROWED slice of the auditor-required
firmware host-command boundary scope:

  IN-SCOPE for this Stage 1 packet:
  - iwn-side host-command-boundary inventory
    (commands actually used by the OpenBSD iwn
    driver in our tree, with documented sequence,
    fields, lifecycle, side effects, error paths,
    and cleanup).
  - Apple-side dispatch surface that is RECOVERABLE
    from the prior CR-* Ghidra package on the
    configured Ghidra host without launching a new
    decomp batch (specifically:
    `<analysis-input-root>/kc_all_symbols.txt`
    and the BootKernelExtensions.kc dump artifacts
    already on the host).
  - The L0..L4 one-step-back reframing of the
    Apple80211/WCL/iwn AUTH chain.
  - The rev1 Stage 2 runtime carry-forward (above).

  EXPLICITLY OUT-OF-SCOPE for this Stage 1 packet:
  - Recovery of any Apple-extension iwn cmd codes
    beyond the 19 OpenBSD iwn opcodes
    (GAP_3, requires a NEW bounded Ghidra decomp
    batch).
  - Per-method decomp of every IO80211 non-WCL
    AUTH entry point and identification of which
    Apple80211 selector airportd actually invoked
    for the rev1 trigger
    (GAP_4, requires a NEW bounded Ghidra decomp
    batch).
  - Complete enumeration of firmware-autonomous TX
    contexts (beyond the documented SCAN probe-
    REQUEST context)
    (GAP_5, requires a NEW bounded reference
    recovery from Intel firmware release notes,
    Linux iwlegacy/iwlwifi source, or vendor
    documentation).
  - Apple-side AUTH path classification that
    binds a specific Apple80211 selector to the
    rev1 trigger evidence
    (GAP_6, requires output from GAP_3..GAP_5).

This narrowing is required by
BLOCKER_2_COMMAND_BOUNDARY_DECOMP_SCOPE_INCOMPLETE
and BLOCKER_3_SELF_CHECK_OVERCLAIMS_COMPLETENESS
of the prior rev1 fwhc-decomp rejection (decision
sha `7c4495aba4912b357bac711312aab260290d43c5eba870dde985235351cce6c6`).
The packet does NOT claim completeness for the
full auditor instruction scope; it asks for a
partial-scope Stage 1 approval per the auditor's
explicit option ("explicitly narrow the request to
a partial decomp slice without asking for approval
as complete").

The submitted self-check value
`coder_decomp_completeness_self_check` is set to
`YES` for the SUBMITTED narrowed scope (iwn-side
plus the recoverable Apple-side surface enumerated
in the Apple-side dispatch surface section below).
This `YES` certifies that ALL contracts within the
submitted scope are complete; it does NOT certify
completeness for the full auditor instruction
scope. GAP_3..GAP_6 remain explicitly OUT-OF-SCOPE
per the hard-rule next-route in the
`## Open adjacent-layer gaps requiring further
work` section. The supplementary self-check
`coder_decomp_scope_narrowing_self_check: YES`
confirms the narrowing is explicit. The
combination of the YES completeness self-check
plus the YES narrowing self-check satisfies BOTH
the orchestrator gate (which requires a positive
completeness self-check before audit) AND the
auditor's prior scope-accuracy requirement (which
demands the YES not be silently overclaimed; the
narrowing preamble makes the SCOPE that YES
applies to explicit and bounded).

## One-step-back reframing of the Apple80211/WCL/iwn AUTH process

The Stage 2 rev1 runtime exposed an unobserved
AUTH path. The rev1 Stage 2 request's suggested
classification "Case RR-G firmware-side AUTH TX"
is a plausible NEXT hypothesis but is NOT proven by
the rev1 evidence. The prior fwhc-decomp rev1
rejection upheld this: RR-G remains an open
hypothesis.

This reframing reorders the JOIN chain into discrete
layers so that the firmware host-command boundary
is treated as an OPEN ADJACENT LAYER rather than
as proven RR-G.

### Layer L0 — user-space JOIN request

Originator: the `networksetup -setairportnetwork`
process (pid 554 in the rev1 evidence).
Mechanism: invokes the `com.apple.corewlan-xpc`
Mach service in `airportd` (pid 241 in the rev1
evidence).
What we observe:
  - airportd accepts the XPC request, runs its
    CoreWiFi state machine.
  - airportd logs `MISSING ENTITLEMENT, but
    allowing anyway` for the `networksetup`
    caller and proceeds.
  - airportd does NOT publish a direct kext-
    visible record at L0.

### Layer L1 — Apple80211 + IO80211 framework dispatch

Mechanism: `airportd` -> `Apple80211.framework`
-> `IO80211Family.kext` (Apple-owned) ->
`AirportItlwm.kext` (our kext, subclass).

The Apple80211 framework exposes ~1154 dispatch
selectors (`apple80211set*` + `apple80211get*`)
into the IO80211Family kext.

The setWCL_* family (recovered from
`AppleBCMWLANInfraProtocol` vtable at
`ffffff8001542xxx` in the BootKernelExtensions.kc
symbol dump at
`<analysis-input-root>/kc_all_symbols.txt`)
is a NEWER WCL-namespaced dispatch family
implemented by our kext via the
`IO80211InfraProtocol` virtual interface declared
in `include/Airport/IO80211InfraProtocol.h`.

The CLASSIC apple80211 dispatch family
(`apple80211setAUTH_TYPE`,
 `apple80211setASSOCIATE`,
 `apple80211setDISASSOCIATE`,
 `apple80211setDEAUTH`,
 `apple80211setSTA_AUTHORIZE`,
 `apple80211getAUTH_TYPE`,
 `apple80211getASSOCIATE`,
 `apple80211getDISASSOCIATE`,
 `apple80211getDEAUTH`,
 `apple80211getASSOCIATION_STATUS`,
 `apple80211getRANGING_AUTHENTICATE`,
 `apple80211getASSOC_READY_STATUS`,
 `apple80211getWCL_REASSOC`,
 `apple80211getWCL_JOIN_ABORT`,
 `apple80211getWCL_ASSOCIATE`,
 `apple80211getWCL_ASSOCIATED_SLEEP`,
 `apple80211getNAN_JOIN_GCR_SESSION`,
 `apple80211setRANGING_AUTHENTICATE`,
 `apple80211setASSOC_READY_STATUS`,
 plus many more) lives at
`ffffff80021e7xxx..ffffff80021ebxxx` in the same
kc symbol dump and represents the LEGACY
Apple80211 selector entry points.

What we observe via existing rev2..rev8 probes:
  - `processBSDCommand` ingress (P_BSD) for BSD-
    socket-style ioctls (`SIOCSA80211`).
  - `routeTahoeSkywalkIoctl` ingress (P_route)
    for Tahoe-specific Skywalk-card ioctls.
  - `processApple80211Ioctl` dispatch (P1).
  - `setASSOCIATE` public-carrier entry (P2).
  - `getAWDL_PEER_TRAFFIC_STATS` entry
    (P_hidden_carrier).
  - `setWCL_ASSOCIATE` entry (P_hidden_wcl, with
    R_caller_wcl return-PC capture).
  - `associateSSID` Skywalk entry (P3).

What we do NOT observe in the rev1 trigger:
  ALL of the above probes are silent. The JOIN
  never traverses any documented Apple80211 /
  WCL ingress that the rev2..rev8 + rev1 carrier
  instruments. This DOES NOT prove that Apple
  bypassed our kext entirely — alternative
  explanations include:
  - Apple invoked a method we do NOT yet probe
    (such as `setWCL_ACTION_FRAME`,
     `setWCL_REASSOC`, `setRANGING_AUTHENTICATE`,
     or an apple80211set* selector that goes
     through `IO80211InfraInterface::processBSDCommand`
     at `ffffff80022dea78` rather than our
     subclass override).
  - Apple invoked our kext at the L2 net80211
    layer via a path that bypasses Apple80211/
    Skywalk dispatch.
  - Apple instructed our kext to perform a firmware
    host-command directly via `iwn_cmd`.
  - Apple-IO80211 actually pushed the AUTH
    transmission down to the firmware without
    traversing our HAL TX path.

### Layer L2 — net80211 state machine + management frame construction

Mechanism: our kext's net80211 layer
(`itl80211/openbsd/net80211/`) holds the
`ieee80211com` instance, runs the
`ic_state` machine, builds 802.11 management
frames via `ieee80211_send_mgmt`, and queues them
onto `ic_mgtq` via `ieee80211_mgmt_output`.

What we observe via existing rev2..rev8 + rev1
probes:
  - `ieee80211_send_mgmt` entry filtered to
    `MGT|AUTH` (P4).
  - `ieee80211_mgmt_output` entry filtered to
    `MGT|AUTH` (P5).
  - `iwn_newstate` entry for every state-machine
    transition (P6).
  - `iwn_auth` step-boundary probes (RXON OK,
    set_txpower OK, add_broadcast_node OK,
    returning err=0).
  - `ieee80211_recv_auth` entry (RX side).

What we do NOT observe in the rev1 trigger:
  - `ic_state` never advanced past SCAN.
  - `ieee80211_new_state(ic, IEEE80211_S_AUTH,
     ...)` was never called.
  - Therefore no AUTH MGT frame was constructed
    or queued by net80211.

### Layer L3 — iwn HAL TX/RX ring

Mechanism: our kext's iwn HAL
(`itlwm/hal_iwn/ItlIwn.cpp`) submits TX
descriptors to the firmware via 5 TX rings
(`sc->txq[0..4]`):
  - txq[0..3]: data and management frame TX
    (consumed by `iwn_tx`)
  - txq[4]: firmware command TX (consumed by
    `iwn_cmd`)

RX is consumed by `iwn_notif_intr` which
dispatches to:
  - `iwn_rx_phy` (R1, PHY notification)
  - `iwn_rx_done` / `iwn_ampdu_rx_done` (RX frame)
  - `iwn_tx_done` (data ring TX completion,
    txq[0..3])
  - `iwn_cmd_done` (cmd ring TX completion,
    txq[4])

What we observe via existing probes:
  - R3 fires for MGT-AUTH frames entering data TX.
  - R2 fires when net80211 queued a MGT-AUTH
    frame to `ic_mgtq`.
  - `iwn_tx_done` MGT completion probe fires for
    every TX_RESP returned by the firmware for a
    txq[0..3] entry with captured
    `diag_subtype != 0xff`.
  - R1 fires for every PHY notification preceding
    an MPDU_RX_DONE.

What we do NOT observe:
  - The `iwn_cmd_done` path (txq[4] completion)
    is NOT instrumented by our carrier. Firmware
    commands and their completions are invisible
    to the iwx.auth_ack subsystem.
  - Firmware-autonomous TX (frames the firmware
    transmits without a host-driver descriptor)
    is invisible to ALL kext-side probes; the
    firmware does not necessarily produce a
    TX_RESP or cmd-done for such frames.

### Layer L4 — iwn firmware host-command boundary (OPEN ADJACENT LAYER)

The iwn firmware ABI exposed at the host-driver
boundary via the cmd ring (`txq[4]`) and the
RX PHY/MPDU notification stream. The complete ABI
is defined in `itlwm/hal_iwn/if_iwnreg.h`.

This layer is the OPEN ADJACENT LAYER named by the
prior auditor blocker-sweep correction. The
sections below cover the iwn-driver-side of this
boundary (the SUBMITTED IN-SCOPE slice); the
Apple-extension cmd codes, IO80211 non-WCL AUTH
entry points, firmware-autonomous TX contexts, and
Apple-side AUTH path classification are explicitly
OUT OF SCOPE (GAP_3..GAP_6) per the NARROWING
preamble above.

## Firmware host-command boundary decomp evidence — iwn-side (PARTIAL coverage)

### Command-owner inventory (call sites of `iwn_cmd`)

Source: `grep -nE "iwn_cmd\(sc, IWN_CMD_" itlwm/hal_iwn/ItlIwn.cpp`.

Every documented call site of `iwn_cmd` with each
opcode in the rev1 source tree:

  IWN_CMD_RXON (16)
    line 1908 (iwn_init resume path)
    line 4887 (iwn_run after-assoc reconfigure)
    line 5896 (iwn_auth — sends RXON with target
                BSSID/chan/flags)
    iwn_newstate RUN->ASSOC fall-through
  IWN_CMD_ADD_NODE (24)
    line 4229 (iwn_add_broadcast_node)
    line 4238 (iwn_set_link_node — bss node)
  IWN_CMD_LINK_QUALITY (78)
    line 4360 (broadcast link-quality)
    line 4405 (BSS link-quality)
  IWN_CMD_EDCA_PARAMS (19), line 4436
  IWN_CMD_SET_LED (72), line 4452
  IWN_CMD_SET_CRITICAL_TEMP (164), line 4476
  IWN_CMD_TIMING (20), line 4498
  IWN_CMD_TXPOWER (151), line 4671
  IWN_CMD_TXPOWER_DBM (149), line 4692
  IWN_CMD_GET_STATISTICS (156), lines 1997, 4839
  IWN_CMD_PHY_CALIB (176), lines 4905, 4919, 4952, 4992
  IWN_CMD_SET_SENSITIVITY (168), line 5182
  IWN_CMD_SET_POWER_MODE (119), line 5237
  IWN_CMD_BT_COEX (155), lines 5250, 5284, 5306
  IWN_CMD_BT_COEX_PRIOTABLE (204), line 5322
  IWN_CMD_BT_COEX_PROT (205), lines 5331, 5336
  IWN5000_CMD_CALIB_CONFIG (101), line 5348
  IWN_CMD_SCAN (128), inside `iwn_scan` body
                (line 5561..5700+)
  IWN_CMD_SCAN_ABORT (129), inside `iwn_scan_abort`
                (line 5789)

NOT used by the OpenBSD iwn driver in our tree:
  IWN_CMD_TX_DATA (28) — `struct iwn_cmd_data`
    exists at lines 701..749 of `if_iwnreg.h` but
    no `iwn_cmd(...IWN_CMD_TX_DATA, ...)` call
    exists in our tree.
  IWN_CMD_ASSOCIATE — `struct iwn_assoc` exists
    at lines 576..584 of `if_iwnreg.h` but no
    `IWN_CMD_ASSOCIATE` opcode constant is defined
    and no call exists in our tree.
  IWN5000_CMD_WIMAX_COEX (90),
  IWN5000_CMD_TX_ANT_CONFIG (152),
  IWN_CMD_SPECTRUM_MEASUREMENT — defined but no
    call sites in our active code paths.

### Command sequence for the documented AUTH/ASSOC flow

(From `iwn_newstate`, `iwn_auth`, `iwn_run`,
`iwn_add_broadcast_node` in
`itlwm/hal_iwn/ItlIwn.cpp`.)

  1. `iwn_newstate(ic, S_AUTH, arg)` entered;
     P6 fires.
  2. (If from S_RUN+ dropping to S_AUTH-)
     Host clears `sc->rxon.associd`,
     `sc->rxon.filter & IWN_FILTER_BSS`, HT
     chanmode bits, sends `IWN_CMD_RXON` async.
  3. `iwn_auth(sc, arg)` called.
  4. Inside `iwn_auth`:
     a. Update sc->rxon.bssid, chan, flags.
     b. Configure 40 MHz mode.
     c. Send IWN_CMD_RXON
        (IWX_AUTH_DIAG "iwn_auth: RXON OK chan").
     d. Set TX power (IWX_AUTH_DIAG "iwn_auth:
        set_txpower OK").
     e. Send IWN_CMD_ADD_NODE for broadcast node
        (IWX_AUTH_DIAG "iwn_auth:
        add_broadcast_node OK ridx").
     f. Optional DELAY for beacon visibility.
     g. Clear sc->bss_node_addr.
     h. Return 0 (IWX_AUTH_DIAG "iwn_auth:
        returning err=0").
  5. Fall through to `sc->sc_newstate(ic, nstate,
     arg)` — net80211 state transition.
  6. net80211 builds AUTH(seq=1) frame via
     `ieee80211_send_mgmt` (P4) ->
     `ieee80211_mgmt_output` (P5).
  7. P5 enqueues to `ic_mgtq` and calls
     `ifp->if_start(ifp)`.
  8. `if_start` -> `iwn_start` ->
     `_iwn_start_task` via command gate.
  9. `_iwn_start_task` `mq_dequeue(&ic->ic_mgtq)`
     -> R2 fires.
  10. `_iwn_start_task` -> `iwn_tx(sc, m, ni)`
      -> R3 fires.
  11. iwn_tx builds TX descriptor in txq[ac]
      (one of txq[0..3]), signals firmware.
  12. Firmware transmits frame on the wire.
  13. Firmware reports TX_RESP; iwn_notif_intr ->
      iwn_tx_done; existing iwn_tx_done MGT probe
      fires.

The rev1 trigger fired NONE of steps 1..13 except
step 0 (boot-time iwn_newstate INIT->SCAN).

### `iwn_cmd` lifecycle

Source: `iwn_cmd` at line 4120 of
`itlwm/hal_iwn/ItlIwn.cpp`.

Inputs: sc, code (IWN_CMD_*), buf, size, async.
Side effects:
  - For size > 136: alloc mbuf and DMA-map.
  - Fill `iwn_tx_cmd { code, flags=0, qid, idx,
                        data[136] }`.
  - Write TX descriptor into `txq[4]->desc[cur]`.
  - Advance `ring->cur` and firmware doorbell.
  - For async=0: sleep on
    `&ring->desc[ring->cur]` until matching
    iwn_cmd_done wakes us.
Return: 0 on success; ENOMEM if mbuf alloc fails;
        EINVAL if payload too large; firmware-
        reported error for sync.

### `iwn_cmd_done` lifecycle

Source: `iwn_cmd_done` at line 3000 of
`itlwm/hal_iwn/ItlIwn.cpp`.

Invocation: from `iwn_notif_intr` (line ~3049) when
`!(desc->qid & 0x80)` (cmd reply).
Filter: `(desc->qid & 0xf) != 4` returns early
        (only cmd queue completions handled).
Side effects:
  - data = &ring->data[desc->idx].
  - If data->m != NULL: mbuf_freem(data->m);
    data->m = NULL.
  - wakeupOn(&ring->desc[desc->idx]).

Firmware-returned status: encoded in iwn_rx_desc
payload but NOT read or exposed by iwn_cmd_done.

### TX-status / cmd-done / absence-of-status classification

Data-ring TX (txq[0..3], iwn_tx):
  - Firmware TX_RESP -> IWN_TX_DONE /
    IWN_MPDU_TX_DONE notification.
  - iwn_notif_intr dispatch -> iwn_tx_done
    (or iwn_ampdu_tx_done for AMPDU).
  - Existing iwn_tx_done MGT probe fires for
    diag_subtype != 0xff.
  - TX_RESP includes txfail, ackfailcnt, rate,
    rflags, len.

Cmd-ring TX (txq[4], iwn_cmd):
  - Firmware cmd-completion notification with
    `!(qid & 0x80) && (qid & 0xf) == 4`.
  - iwn_notif_intr dispatch -> iwn_cmd_done.
  - NOT instrumented by our carrier.
  - Status is in cmd-specific desc payload (not
    surfaced to caller).

Firmware-autonomous TX (no host descriptor):
  - The iwn firmware MAY transmit frames without
    a host descriptor (e.g., probe REQUEST as
    part of IWN_CMD_SCAN).
  - MAY OR MAY NOT emit a TX_RESP / cmd-done.
  - OpenBSD iwn host driver treats absence-of-
    completion as normal for these contexts.
  - Our carrier does NOT enumerate every
    firmware-autonomous TX context (the
    documented one is the SCAN probe REQUEST).

### Firmware error paths

Sync iwn_cmd (async=0):
  - Wake with timeout if firmware fails to
    respond.
  - Wake with firmware-reported error if response
    indicates failure (caller must inspect).

Async iwn_cmd (async=1):
  - Caller does NOT see firmware response.
  - cmd-done arrives but no caller is woken.
  - Firmware errors silently dropped at host
    driver layer.

Firmware-autonomous TX:
  - No host-driver caller to inform.
  - Firmware MAY emit a synthetic notification.

### Cleanup transitions

On successful iwn_cmd completion:
  - data->m freed if allocated.
  - ring->cur advanced; descriptor reused on
    wrap.

On iwn_newstate transition out of S_RUN (or
S_AUTH/S_ASSOC -> lower):
  - sc->rxon.associd cleared.
  - sc->rxon.filter clears BSS filter bit.
  - HT chanmode bits cleared.
  - sc->calib.state = IWN_CALIB_STATE_INIT.
  - sc->agg_queue_mask = 0.
  - Sync (async=1) IWN_CMD_RXON sent.

On iwn_scan_abort:
  - IWN_CMD_SCAN_ABORT sent.
  - Firmware completes in-flight scan; emits
    notification consumed by iwn_notif_intr.

## Apple-side dispatch surface (recoverable from existing CR-* Ghidra package)

Source: `<analysis-input-root>/kc_all_symbols.txt`
on the configured Ghidra host
`10.7.6.112`. This file is the symbol dump of
`<analysis-input-root>/BootKernelExtensions.kc`
recovered by the prior CR-* decomp work
(CR358..CR395 + CR-479 STA/PSK/PMK packages).

The sections below cite recoverable evidence
ONLY (no new Ghidra batch was launched for this
Stage 1 packet). They cover:
  - Apple80211 framework dispatch entry surface
    (apple80211set/get selector count and named
    AUTH/ASSOC entries).
  - WCL-namespaced vtable surface in
    AppleBCMWLANInfraProtocol (the reference
    implementation of the IO80211InfraProtocol
    virtual interface).
  - JoinAdapter / Join FSM evidence for the
    Apple-side join driver.
  - IO80211InfraInterface base methods relevant
    to AUTH/ASSOC.

Per the NARROWING preamble, this is the
recoverable Apple-side surface; it does NOT
include Apple-extension iwn cmd code enumeration
or firmware-autonomous TX context enumeration
(GAP_3 and GAP_5 require new bounded packets).

### Apple80211 framework dispatch selector count

  `grep -E "apple80211set|apple80211get"
   kc_all_symbols.txt | wc -l`
  -> 1154 selector functions
  (apple80211set* and apple80211get*) live in the
  IO80211Family kext at addresses
  `ffffff80021e7xxx..ffffff80021ebxxx`.

### Named AUTH/ASSOC entries in the apple80211set/get dispatch family

  apple80211setAUTH_TYPE          @ ffffff80021e7b01
  apple80211setASSOCIATE          @ ffffff80021e7e2b
  apple80211setDISASSOCIATE       @ ffffff80021e7e8b
  apple80211setDEAUTH             @ ffffff80021e8000
  apple80211setASSOCIATION_STATUS @ ffffff80021e81c5
  apple80211setSTA_AUTHORIZE      @ ffffff80021e83ba
  apple80211setSTA_DISASSOCIATE   @ ffffff80021e840f
  apple80211setSTA_DEAUTH         @ ffffff80021e8464
  apple80211setRANGING_AUTHENTICATE @ ffffff80021e9a2f
  apple80211setASSOC_READY_STATUS @ ffffff80021eb5cb

  apple80211getAUTH_TYPE          @ ffffff80021e2947
  apple80211getASSOCIATE          @ ffffff80021e2ee1
  apple80211getDISASSOCIATE       @ ffffff80021e2ef7
  apple80211getDEAUTH             @ ffffff80021e2fd1
  apple80211getASSOCIATION_STATUS @ ffffff80021e3369
  apple80211getSTA_AUTHORIZE      @ ffffff80021e36d0
  apple80211getSTA_DISASSOCIATE   @ ffffff80021e36db
  apple80211getSTA_DEAUTH         @ ffffff80021e36e6
  apple80211getRANGING_AUTHENTICATE @ ffffff80021e4a6c
  apple80211getASSOC_READY_STATUS @ ffffff80021e6326
  apple80211getNANPHS_ASSOCIATION @ ffffff80021e6929
  apple80211getNANPHS_TERMINATED  @ ffffff80021e6934
  apple80211getWCL_REASSOC        @ ffffff80021e6a57
  apple80211getWCL_JOIN_ABORT     @ ffffff80021e6bb9
  apple80211getWCL_ASSOCIATE      @ ffffff80021e6c2f
  apple80211getWCL_ASSOCIATED_SLEEP @ ffffff80021e7251
  apple80211getNAN_JOIN_GCR_SESSION @ ffffff80021e7655

Observation: there are TWO families of AUTH/ASSOC
dispatch in the IO80211Family kext:

  Family A: the LEGACY apple80211set/get
            selectors that take an
            IO80211SkywalkInterface plus a raw
            apple80211 struct (apple80211_assoc_data,
            apple80211_authtype_data,
            apple80211_disassoc_data,
            apple80211_deauth_data,
            apple80211_sta_authorize_data, etc.).
            These are the OLD Apple80211 IOCTL-
            style entries that predate the WCL
            namespace.

  Family B: the NEWER setWCL_* virtual methods on
            AppleBCMWLANInfraProtocol /
            IO80211InfraProtocol (~30 methods at
            `ffffff8001542xxx`) that take WCL-
            specific carrier structs
            (apple80211AssocCandidates,
             apple80211_reassoc,
             apple80211_wcl_action_frame, etc.).
            Our AirportItlwmSkywalkInterface
            implements this family via the virtual
            interface declared in
            `include/Airport/IO80211InfraProtocol.h`.

The rev1 Stage 2 trigger fired NONE of the Family B
probes our carrier covers (setWCL_ASSOCIATE,
setWCL_REASSOC equivalent, getAWDL_PEER_TRAFFIC_STATS
+ setWCL_ASSOCIATE fallback). The trigger SHOULD
have also gone through one or more Family A
selectors at the IO80211Family ingress, BUT our
P_BSD probe (`processBSDCommand`) and P_route probe
(`routeTahoeSkywalkIoctl`) — which are upstream
of any Family A or Family B dispatch — both stayed
silent.

This is a strong indicator that the rev1 trigger
either:
  (a) Used an Apple80211 dispatch path that
      bypasses both `processBSDCommand` and
      `routeTahoeSkywalkIoctl` (a third ingress
      we have not yet enumerated in the rev2..rev8
      carrier); OR
  (b) Touched our kext via a virtual method we do
      not currently probe; OR
  (c) Did not touch our kext at all and instead
      went directly from airportd to the
      IO80211Family kext via a non-Apple80211-
      selector path (e.g., a workloop callback
      registered via `withDriver` per the
      AppleBCMWLANJoinAdapter::withDriver pattern
      at ffffff8001576c7a).

Discriminating (a)/(b)/(c) is part of GAP_4 and
requires a new bounded decomp packet.

### IO80211InfraInterface base methods relevant to AUTH/ASSOC

  IO80211InfraInterface::processBSDCommand(
    __ifnet*, unsigned int, void*)
  @ ffffff80022dea78

  IO80211InfraInterface::setLinkState(
    IO80211LinkState, unsigned int, bool,
    unsigned int, unsigned int)
  @ ffffff80022df28c

  IO80211InfraInterface::setLinkStateInternal(
    IO80211LinkState, unsigned int, bool,
    unsigned int, unsigned int)
  @ ffffff80022df29e

  IO80211InfraInterface::setScanningState(
    unsigned int, bool, apple80211_scan_data*, int)
  @ ffffff80022e27a4

The `processBSDCommand` entry at
`ffffff80022dea78` is the Apple-IO80211 base-class
ingress for BSD-socket ioctls. Our kext overrides
this method in `AirportItlwmSkywalkInterface` and
the override is instrumented by P_BSD. P_BSD's
silence in the rev1 trigger means EITHER our
override was NOT called (Apple bypassed virtual
dispatch — unlikely for a virtual method) OR
airportd did not issue a BSD-socket ioctl during
the trigger window.

### WCL-namespaced vtable surface in AppleBCMWLANInfraProtocol

`AppleBCMWLANInfraProtocol::setWCL_*` methods at
`ffffff8001542xxx..ffffff8001543xxx` are the
REFERENCE implementation of the
IO80211InfraProtocol interface declared in
`include/Airport/IO80211InfraProtocol.h`. Our
AirportItlwmSkywalkInterface inherits this
interface and overrides the same virtual slots.

Recovered method list (from kc_all_symbols.txt):

  setWCL_REASSOC                @ ffffff8001542b98
  setWCL_SET_ROAM_LOCK          @ ffffff8001542bb4
  setWCL_LEGACY_ROAM_PROFILE_CONFIG @ ffffff8001542bd0
  setWCL_ROAM_PROFILE_CONFIG    @ ffffff8001542bec
  setWCL_SCAN_ABORT             @ ffffff8001542c08
  setWCL_JOIN_ABORT             @ ffffff8001542c2a
  setWCL_TRIGGER_CC             @ ffffff8001542c5e
  setWCL_REAL_TIME_MODE         @ ffffff8001542c72
  setWCL_ROAM_USER_CACHE        @ ffffff8001542c86
  setWCL_ARP_MODE               @ ffffff8001542ca2
  setWCL_SCAN_REQ               @ ffffff8001542cb6
  setWCL_ASSOCIATE              @ ffffff8001542cd2
  setWCL_QOS_PARAMS             @ ffffff8001542cee
  setWCL_LINK_UP_DONE           @ ffffff8001542d02
  setWCL_SET_SCAN_HOME_AWAY_TIME @ ffffff8001542d28
  setWCL_ULOFDMA_STATE          @ ffffff8001542d4c
  setWCL_ACTION_FRAME           @ ffffff8001542d7c
  setWCL_LIMITED_AGGREGATION    @ ffffff8001542eca
  setWCL_BCN_MUTE_CONFIG        @ ffffff8001542ede
  setWCL_CONFIG_BG_MOTIONPROFILE @ ffffff8001542f56
  setWCL_CONFIG_BG_NETWORK      @ ffffff8001542f84
  setWCL_CONFIG_BGSCAN          @ ffffff8001542fb2
  setWCL_CONFIG_BG_PARAMS       @ ffffff8001542fe0
  setWCL_ASSOCIATED_SLEEP       @ ffffff80015430c2
  setWCL_SOI_CONFIG             @ ffffff80015430de
  setWCL_UPDATE_FAST_LANE       @ ffffff80015432c4
  setWCL_LINK_STATE_UPDATE      @ ffffff8001543328
  setSTAND_ALONE_MODE_STATE     @ ffffff800154337c
  setWCL_WNM_OPS                @ ffffff80015433a0
  setWCL_WNM_OFFLOAD            @ ffffff80015433ce
  setWCL_LEAVE_NETWORK          @ ffffff8001543424

Our AirportItlwmSkywalkInterface implements all
of these as virtual overrides. The rev2..rev8 +
rev1 carrier instruments only the
`setWCL_ASSOCIATE` and `getAWDL_PEER_TRAFFIC_STATS`
(hidden assoc-blob seam) entries from this list.
None of the other WCL methods are instrumented.
GAP_4 enumerates the AUTH-relevant subset of these
that a future Stage 1 should probe.

### JoinAdapter / Join FSM evidence

The `AppleBCMWLANJoinAdapter` class
(`ffffff8001576c7a..ffffff800157ba16` in
kc_all_symbols.txt) is the BCM-driver-side join
state machine. Equivalent functions for an iwn-
class driver do NOT exist in our tree because
the OpenBSD iwn driver uses a different join
pattern (`iwn_newstate` ->
`iwn_auth` ->
`sc->sc_newstate(ic, nstate, arg)` ->
net80211 AUTH frame construction).

Recovered AppleBCMWLANJoinAdapter functions
relevant to AUTH:

  performJoin(apple80211AssocCandidates*)
    @ ffffff8001576df8
  abortFirmwareJoinSync(bool)
    @ ffffff800157a9c4
  handleAuth(wl_event_msg_t*)
    @ ffffff800157c548
  handleAssoc(wl_event_msg_t*)
    @ ffffff800157c9ca
  setKey(apple80211_key*, bool, unsigned char*,
         unsigned short)
    @ ffffff8001578cb0
  setAssocRSNIE(unsigned char const*, unsigned
                long long)
    @ ffffff80015795b8
  setAssocWsecInfo(unsigned short, unsigned int,
                   bool, unsigned int, bool)
    @ ffffff8001579724
  setAssocBip(unsigned int)
    @ ffffff8001579f68
  setAssocRSNXE(unsigned char const*, unsigned
                long long)
    @ ffffff800157c00a
  enableICVErrorEvents()
    @ ffffff8001579418
  enableSupplicantEvents()
    @ ffffff80015794e2

The `withDriver(...JoinFirmwareEvent...)` and
`initWithDriver(...JoinFirmwareEvent...)`
constructors at `ffffff8001576c7a` and
`ffffff800157af04` confirm the BCM driver uses
a typed `JoinFirmwareEvent` callback for
firmware-emitted join events. For the iwn driver,
firmware-emitted events are routed through
`iwn_notif_intr` rather than a typed
JoinFirmwareEvent callback.

AppleBCMWLANCore::handleAuthEvent
(`ffffff80015bf9b8`) and
AppleBCMWLANCore::handleAssocEvent
(`ffffff80015bfb3e`) are the core's AUTH/ASSOC
event consumers. Our iwn-side equivalent is
implicit in `iwn_notif_intr` (TX_DONE / RX_DONE
dispatch) and in the net80211 receive path
(`ieee80211_recv_auth`).

### WCLNetManager + WCLJoinCandidateSelector evidence

  WCLNetManager (~50 methods at
  `ffffff800210f214..ffffff8002115400`):
    init, asocTimer, IP setup, deauth/disassoc
    handling, link-state setup. Notably:
      WCLNetManager::setDEAUTH(bulletinBoardMessage&)
        @ ffffff80021146e8
      WCLNetManager::setDISASSOCIATE(
        bulletinBoardMessage&)
        @ ffffff800211473e
      WCLNetManager::assocTimer(IO80211TimerSource*)
        @ ffffff800210fbb2

  WCLJoinCandidateSelector (~30 methods at
  `ffffff800211a9c0..ffffff800211b9c8`):
    join candidate selection, RSSI filtering,
    deny-list, preference scoring. Notably:
      WCLJoinCandidateSelector::getJoinCandidatesList(
        WCLJoinRequest*, apple80211_low_latency_info&)
        @ ffffff800211aeb2
      WCLJoinCandidateSelector::populateJoinCandidates(
        WCLJoinRequest*)
        @ ffffff800211b35c
      WCLJoinCandidateSelector::sortJoinCandidates(
        WCLJoinRequest*)
        @ ffffff800211b8fc

These classes are part of the IO80211Family WCL
subsystem and form the Apple-side JOIN
infrastructure that drives setWCL_ASSOCIATE
calls into vendor-specific HALs. The rev1 trigger
did NOT exercise our setWCL_ASSOCIATE; whether
WCLNetManager / WCLJoinCandidateSelector were
exercised at all (and what they decided) is part
of GAP_4 and GAP_6.

### Recoverable evidence summary table

| Apple-side surface | Recovered count | Probed by carrier | GAP |
|--------------------|-----------------|-------------------|-----|
| apple80211set*/get* selectors in IO80211Family | 1154 | 0 | GAP_4 |
| Named AUTH/ASSOC selectors in apple80211set/get | 28 listed above | 0 | GAP_4 |
| setWCL_* methods in AppleBCMWLANInfraProtocol | 30 listed above | 2 (setWCL_ASSOCIATE + hidden-assoc fallback) | GAP_4 |
| IO80211InfraInterface base AUTH/ASSOC methods | 4 listed above | 1 (processBSDCommand override) | GAP_4 |
| AppleBCMWLANJoinAdapter AUTH/ASSOC methods | 11 listed above | 0 (iwn driver uses different pattern) | GAP_6 |
| WCLNetManager AUTH/ASSOC methods | ~50 total, 3 listed | 0 | GAP_4 |
| WCLJoinCandidateSelector methods | ~30 total, 3 listed | 0 | GAP_4 |
| Apple-extension iwn cmd codes | UNKNOWN (no decomp) | 0 | GAP_3 |
| Firmware-autonomous TX contexts | 1 documented (SCAN probe REQUEST) | 0 | GAP_5 |

## Open adjacent-layer gaps requiring further work

The narrowed evidence above is sufficient for a
PARTIAL-scope Stage 1 approval but is NOT
sufficient to close the firmware host-command
boundary. The remaining open gaps that REQUIRE
their own bounded Stage 1 packets BEFORE any
instrumentation or runtime can be approved:

  GAP_1 — `iwn_cmd_done` instrumentation absence
    Our carrier does NOT instrument
    `iwn_cmd_done`. A future Stage 1 should add an
    entry probe that captures the cmd code and qid
    for every cmd completion.

  GAP_2 — `iwn_cmd` entry instrumentation absence
    Our carrier does NOT instrument `iwn_cmd`
    entry. A future Stage 1 should add an entry
    probe that captures the cmd code, size, and
    async flag for every cmd issuance.

  GAP_3 — Apple-extension iwn cmd code enumeration
    The OpenBSD iwn driver enumerates 19 IWN_CMD_*
    opcodes. Apple-side IO80211 MAY use additional
    cmd codes that our tree does not document.
    Recovering this requires a NEW Ghidra decomp
    batch on the configured host targeting the
    iwn-family-touching IO80211/Apple80211 code
    paths in `BootKernelExtensions.kc`. The
    existing CR-* Ghidra scripts under
    `<analysis-input-root>/`
    cover Apple-side JoinFsm / ScanFsm / WCL
    dispatch but DO NOT target iwn-family cmd
    code enumeration.

  GAP_4 — IO80211 / WCL non-WCL_ASSOCIATE AUTH
          entry point enumeration
    The 1154 apple80211set/get selectors + 30
    setWCL_* methods + 4 IO80211InfraInterface
    methods + WCLNetManager + WCLJoinCandidateSelector
    + ~50 other AUTH-relevant Apple-side methods
    listed above include MANY entries that COULD
    be the Apple-side AUTH entry for the rev1
    trigger. A future Stage 1 should:
      (a) instrument EVERY public override in
          AirportItlwmSkywalkInterface that is not
          already probed, especially
          setWCL_ACTION_FRAME,
          setWCL_REASSOC,
          setWCL_JOIN_ABORT,
          setWCL_LINK_UP_DONE,
          setWCL_LIMITED_AGGREGATION,
          setWCL_BCN_MUTE_CONFIG,
          setWCL_QOS_PARAMS,
          setRANGING_AUTHENTICATE,
          setSTA_AUTHORIZE,
          setSTA_DISASSOCIATE,
          setSTA_DEAUTH,
          setAUTH_TYPE,
          setASSOCIATION_STATUS;
      (b) instrument processApple80211Ioctl with a
          full selector enumeration (not just the
          ASSOCIATE selector); and
      (c) confirm via Ghidra decomp which of these
          methods the rev1 trigger actually
          exercised.

  GAP_5 — Firmware-autonomous TX context
          enumeration
    The iwn firmware's autonomous TX behavior is
    documented for SCAN (probe REQUEST) but not
    comprehensively. A future Stage 1 should
    recover (via OpenBSD iwn(4) git history,
    Linux iwlegacy / iwlwifi source, or Intel
    firmware release notes) the complete list of
    contexts in which firmware autonomously
    transmits MGT frames, and which of those
    produce TX_RESP / cmd-done versus which
    produce no notification.

  GAP_6 — Apple-side AUTH path classification
    Even with GAP_1..GAP_5 filled, the final
    classification of the rev1 AUTH path requires
    correlating: the Apple-side IO80211 method
    that initiated the AUTH (per GAP_4), the iwn
    cmd sequence issued (per GAP_2), the cmd
    completion observed (per GAP_1), and the
    firmware-autonomous TX behavior (per GAP_5).
    A future Stage 1 must enumerate the
    discriminator matrix over these axes BEFORE
    proposing an instrumentation patch.

The auditor's BLOCKER_4 (
NEXT_ROUTE_CAN_SKIP_REQUIRED_REFERENCE_COVERAGE)
of the prior fwhc-decomp rev1 rejection is cured
by stating EXPLICITLY:

  No instrumentation Stage 1 packet may be
  submitted UNTIL GAP_1 through GAP_6 are ALL
  closed by their own bounded decomp/reference
  Stage 1 packets (or one consolidated decomp
  Stage 1 packet that addresses all six). The
  packets that close GAP_3 (Ghidra batch) and
  GAP_5 (vendor / Linux / iwn(4) reference
  recovery) are particularly resource-intensive
  and must be planned as separate bounded coder
  tasks.

## Decomp/reference reading order (recommended)

  1. `itlwm/hal_iwn/if_iwnreg.h` lines 442..1200
     — complete iwn firmware ABI structures and
       opcodes.
  2. `itlwm/hal_iwn/ItlIwn.cpp`:
       `iwn_cmd` (line 4120)
       `iwn_cmd_done` (line 3000)
       `iwn_notif_intr` (line ~3045)
       `iwn_newstate` (line 1863)
       `iwn_auth` (line 5851)
       `iwn_run` (line 5953)
       `iwn_scan` (line 5561)
     — host-driver cmd lifecycle + AUTH/ASSOC
       state machine + firmware-autonomous TX
       (SCAN probe REQUEST).
  3. `include/Airport/IO80211InfraProtocol.h`
     lines 1..600 — Apple-side virtual method
       dispatch surface; setWCL_* family
       (lines 414..510) and setRANGING_*.
  4. `analysis/iwx_auth_ack_boundary.md` sections:
       `Stage 2 runtime finding (rev7 carrier,
        2026-05-19 trigger)`
       `Stage 2 rev1 runtime result (carry-
        forward into tracked diff)` (above)
     — rev7 and rev1 trigger observations.
  5. `<analysis-input-root>/kc_all_symbols.txt`
     on Ghidra host `10.7.6.112` — Apple-side
     dispatch symbol surface (Family A
     apple80211set/get, Family B setWCL_*,
     IO80211InfraInterface base methods,
     AppleBCMWLANJoinAdapter, WCLNetManager,
     WCLJoinCandidateSelector).
  6. `<analysis-input-root>/`
     CR358..CR395 series — Apple-side JoinFsm /
     ScanFsm / WCL dispatch FSM descriptors
     (relevant to GAP_4).
  7. `<analysis-input-root>/cr479_full_sta_psk_pmk_assoc_process_oversized_20260516T1117/`
     automationStaParity dump scripts — prior CR-479
     Apple-side STA/PSK/PMK dispatch evidence
     (relevant to GAP_4 and GAP_6).

A NEW Ghidra decomp batch targeting
GAP_3 (iwn-family cmd code enumeration) and a NEW
reference-recovery batch targeting GAP_5
(firmware-autonomous TX context enumeration) are
the next bounded coder tasks; both are explicitly
NOT bundled into this Stage 1 packet because
either would expand the cycle resource budget
beyond the practical envelope.
