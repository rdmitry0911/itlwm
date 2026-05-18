/*
 * AirportItlwmAgent — WPA2 PMK derivation.
 *
 * Implements the IEEE 802.11-2016 PMK derivation for PSK
 * networks: PBKDF2-HMAC-SHA1(passphrase, ssid, 4096, 32). The
 * SSID is the IEEE 802.11 SSID byte window (octet string, up to
 * 32 bytes), used directly as the salt — no UTF-8 conversion.
 *
 * No key bytes are logged here. Only the SSID alias, the
 * passphrase length, and the result code are surfaced.
 */
#ifndef AIRPORTITLWMAGENT_WPA_KEY_H
#define AIRPORTITLWMAGENT_WPA_KEY_H

#include <stddef.h>
#include <stdint.h>

/*
 * AgentDerivePMK_PBKDF2: derive the 32-byte WPA2 PMK from a
 * passphrase + SSID using PBKDF2-HMAC-SHA1 with the standard
 * 4096-iteration count. Output is written to out_pmk32.
 *
 * Returns 0 on success, non-zero on failure (the CommonCrypto
 * CCKeyDerivationPBKDF return code is propagated).
 */
int AgentDerivePMK_PBKDF2(const uint8_t *passphrase,
                          size_t passphrase_len,
                          const uint8_t *ssid,
                          size_t ssid_len,
                          uint8_t out_pmk32[32]);

#endif /* AIRPORTITLWMAGENT_WPA_KEY_H */
