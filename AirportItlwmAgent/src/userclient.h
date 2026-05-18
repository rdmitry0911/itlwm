/*
 * AirportItlwmAgent — PLTI user client wrapper.
 *
 * Thin shell around IOServiceOpen('PLTI') and the two external
 * methods exposed by AirportItlwmUserClient on the kext side:
 *   - WaitAssociationTarget: blocks under the kext command gate
 *     until a new PSK association edge publishes a target. The
 *     helper passes its last_acked generation and gets back the
 *     current target snapshot.
 *   - DeliverPMK: ships a 32-byte WPA2 PMK back, with the
 *     generation echoed so the kext rejects stale material.
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

/*
 * Open the AirportItlwm PLTI user client. Returns kIOReturnSuccess
 * on success and writes the connection handle to *out_conn; the
 * caller is responsible for IOServiceClose(*out_conn) on shutdown.
 *
 * Logs the IOServiceMatching probe outcome (service found, open
 * result) but never logs key material.
 */
kern_return_t AgentOpenPLTI(io_connect_t *out_conn);

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

#endif /* AIRPORTITLWMAGENT_USERCLIENT_H */
