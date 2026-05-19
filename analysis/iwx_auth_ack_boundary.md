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
