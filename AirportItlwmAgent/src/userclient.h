/*
 * AirportItlwmAgent — PLTI user client wrapper.
 *
 * Thin shell around IOServiceOpen('PLTI'), the two legacy PSK external
 * methods, and the typed SAE relay transport selectors exposed by
 * AirportItlwmUserClient on the kext side:
 *   - WaitAssociationTarget: blocks under the kext command gate
 *     until a new PSK association edge publishes a target. The
 *     helper passes its last_acked generation and gets back the
 *     current target snapshot.
 *   - DeliverPMK: ships a 32-byte WPA2 PMK back, with the
 *     generation echoed so the kext rejects stale material.
 *   - SAE relay selectors 2--6: carry only fixed shared-ABI records;
 *     they do not create an SAE credential or cryptographic worker.
 *
 * No raw key bytes appear in any log line. The wrapper only
 * surfaces lengths, generations, return codes, and structural
 * markers.
 */
#ifndef AIRPORTITLWMAGENT_USERCLIENT_H
#define AIRPORTITLWMAGENT_USERCLIENT_H

#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include "assoc_target.h"
#if defined(__has_include)
#if __has_include(<ClientKit/AirportItlwmSaeRelayV1.h>)
#include <ClientKit/AirportItlwmSaeRelayV1.h>
#else
#include "../../include/ClientKit/AirportItlwmSaeRelayV1.h"
#endif
#else
#include "../../include/ClientKit/AirportItlwmSaeRelayV1.h"
#endif

/*
 * Open the AirportItlwm PLTI user client. Returns kIOReturnSuccess
 * on success and writes the connection handle to *out_conn; the
 * caller is responsible for IOServiceClose(*out_conn) on shutdown.
 *
 * AgentOpenPLTI logs probe failures. AgentOpenPLTIQuiet returns
 * the same status without logging expected transient "provider not
 * published yet" failures; the LaunchDaemon retry loop owns those
 * rate-limited lifecycle logs.
 */
kern_return_t AgentOpenPLTI(io_connect_t *out_conn);
kern_return_t AgentOpenPLTIQuiet(io_connect_t *out_conn);

/*
 * Block in the kext command gate until a new PSK association edge
 * publishes a target whose generation differs from last_acked.
 * On success, fills *out_target and returns kIOReturnSuccess. On
 * teardown (kext canceled or connection lost), returns
 * kIOReturnAborted and leaves *out_target untouched.
 */
kern_return_t AgentWaitAssociationTarget(io_connect_t conn,
                                         uint64_t last_acked,
                                         struct AirportItlwmAssociationTarget *out_target);

/*
 * Ship a 32-byte WPA2 PMK back through the PLTI sink. generation
 * MUST equal the generation the helper just received from
 * AgentWaitAssociationTarget; the kext rejects mismatched echoes
 * with kIOReturnNotPermitted (replay guard).
 *
 * No key bytes are logged here or in any downstream call.
 */
kern_return_t AgentDeliverPMK(io_connect_t conn,
                              uint64_t generation,
                              const uint8_t pmk32[32]);

/*
 * Typed accessors for the append-only SAE relay selectors.  They are only
 * transport wrappers: this Agent has no SAE worker yet, so these routines do
 * not obtain credentials, derive protocol secrets, or start an
 * authentication flow.
 *
 * Each record is the shared natural-layout V1 ABI.  The kext owns all
 * association identity, lifecycle, and state-machine admission.  The wrapper
 * rejects malformed fixed-record inputs and outputs locally, but does not
 * reinterpret their protocol contents or relax the controller's checks.
 */

/* Selector 2: no scalar or structure input; returns one SAE target record. */
kern_return_t AgentWaitSaeTarget(io_connect_t conn,
                                 struct AirportItlwmSaeTargetV1 *out_target);

/* Selector 3: sends one semantic Commit/Confirm reply record. */
kern_return_t AgentSubmitSaeReply(
    io_connect_t conn, const struct AirportItlwmSaeAuthReplyV1 *reply);

/* Selector 4: waits from the caller's last observed event sequence. */
kern_return_t AgentWaitSaeAuthEvent(
    io_connect_t conn, uint64_t last_seen_sequence,
    struct AirportItlwmSaeAuthEventV1 *out_event);

/* Selector 5: sends one verified SAE completion record. */
kern_return_t AgentCompleteSae(
    io_connect_t conn, const struct AirportItlwmSaeCompletionV1 *completion);

/* Selector 6: sends one bounded terminal SAE abort record. */
kern_return_t AgentAbortSae(
    io_connect_t conn, const struct AirportItlwmSaeAbortV1 *abort_message);

#endif /* AIRPORTITLWMAGENT_USERCLIENT_H */
