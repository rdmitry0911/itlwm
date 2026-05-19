/*
 * Project-owned diagnostic output carrier for the iwx 802.11 Open-System
 * authentication ACK boundary diagnostic leaves.
 *
 * Bounded usage: this carrier is intentionally narrow. It is the
 * delivery channel for the four auth-ACK kext-side observation layers
 * already introduced for that diagnostic work item:
 *   - iwx_auth() success-path step-boundary probes
 *   - iwx_rx_tx_cmd_single() MGT TX completion probe
 *   - iwx_rx_frame() selected non-beacon MGT RX probe
 *   - ieee80211_recv_auth() entry and short-frame reject probes
 * plus a deterministic same-carrier smoke marker emitted once per
 * successful iwx_attach so the next runtime can first prove carrier
 * visibility before relying on the auth-ACK Case A-F classification.
 *
 * The existing project-wide XYLog macro (itl80211/linux/types.h)
 * remains the delivery channel for every other XYLog call site in the
 * codebase; it is NOT globally redirected through this carrier. The
 * scope here is limited to the auth-ACK diagnostic leaves and the
 * smoke marker.
 *
 * Carrier object lifecycle:
 *   - iwx_auth_diag_log is a single global os_log_t. It is created
 *     by iwx_auth_diag_init() exactly once (idempotent guard). It is
 *     never released; the project diagnostic carrier lives for the
 *     lifetime of the loaded kext, which matches the lifetime of the
 *     diagnostic call sites. This avoids any os_log_t lifetime race
 *     between the smoke marker and the four leaf probes.
 *   - Producer: iwx_auth_diag_init() (called from iwx_attach() on the
 *     success path, after iwx_preinit succeeds and immediately
 *     before the existing `return true;`).
 *   - Consumers: every IWX_AUTH_DIAG() call site.
 *   - Fall-back: if iwx_auth_diag_init() has not yet completed when an
 *     IWX_AUTH_DIAG() macro expands, the macro emits via OS_LOG_DEFAULT
 *     so output is still observable via the default kernel logger;
 *     after init, every emission goes to the
 *     com.zxystd.AirportItlwm / iwx.auth_ack handle.
 *
 * Carrier observability contract:
 *   Output is intended to be visible via the unified-log
 *   firehose buffer using a single-line log-show query with the
 *   project subsystem and category as the predicate:
 *     sudo log show --info --debug --predicate 'subsystem == "com.zxystd.AirportItlwm" AND category == "iwx.auth_ack"'
 *   regardless of the kernel msgbuf state, because os_log routes
 *   through the unified-log firehose buffer rather than the kernel
 *   msgbuf ring. The next approved runtime cycle must verify the
 *   smoke marker is visible via this predicate BEFORE running the
 *   single approved auth-ACK trigger, then verify the four auth-ACK
 *   leaves are visible via the same predicate, and only then map the
 *   captured leaves to a Case A-F classification.
 *
 * Security and privacy:
 *   Every IWX_AUTH_DIAG call site emits only 802.11 management/auth
 *   control-plane values (subtype, addr2 / receiver MAC, AUTH
 *   transaction sequence, firmware TX_RESP status / rate / retry /
 *   airtime, RSSI, RX length, channel index, generation counter,
 *   sta_id, duration_tu, errno). No PSK, PMK, passphrase, EAPOL-key,
 *   SSID secret, or data-frame payload byte ever flows through this
 *   carrier. The carrier is management-/auth-frame scoped by the same
 *   rule the existing rev4 patch documents in
 *   analysis/iwx_auth_ack_boundary.md "Security and privacy".
 */

#ifndef _ITLWM_IWX_DIAG_LOG_H_
#define _ITLWM_IWX_DIAG_LOG_H_

#include <os/log.h>

#ifdef __cplusplus
extern "C" {
#endif

extern os_log_t iwx_auth_diag_log;

void iwx_auth_diag_init(void);

#ifdef __cplusplus
}
#endif

/* IWX_AUTH_DIAG(fmt, ...): emit one auth-ACK diagnostic record via the
 * project-owned os_log handle.
 *
 * The macro is intentionally pass-through with no per-record locking,
 * filtering, or buffering. os_log itself is the buffering layer; it
 * routes through the unified-log firehose, which serializes records
 * across kext call sites.
 *
 * The "itlwm: " literal prefix preserves the diagnostic-string format
 * the rev4 XYLog probes already established, so the next runtime's
 * predicate-and-grep flow does not need a separate cookbook for the
 * four leaves vs. the smoke marker. */
#define IWX_AUTH_DIAG(fmt, ...)     os_log((iwx_auth_diag_log != NULL) ? iwx_auth_diag_log : OS_LOG_DEFAULT,            "itlwm: " fmt, ##__VA_ARGS__)

#endif /* _ITLWM_IWX_DIAG_LOG_H_ */
