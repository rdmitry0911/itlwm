/*
 * AirportItlwmAgent — WPA2 PMK derivation.
 *
 * IEEE 802.11-2016, J.4.1: the WPA2 PMK is
 *   PBKDF2(HMAC-SHA1, passphrase, ssid, 4096, 256 bits)
 * The kext PMK sink in deliverExternalPMK requires key_len == 32,
 * matching the 256-bit (32-byte) PBKDF2 output.
 *
 * The implementation uses CommonCrypto's CCKeyDerivationPBKDF.
 * No PSK or PMK bytes are logged.
 */
#include "wpa_key.h"
#include "log.h"

#include <CommonCrypto/CommonKeyDerivation.h>
#include <string.h>

int
AgentDerivePMK_PBKDF2(const uint8_t *passphrase,
                      size_t passphrase_len,
                      const uint8_t *ssid,
                      size_t ssid_len,
                      uint8_t out_pmk32[32])
{
    if (passphrase == NULL || passphrase_len == 0 ||
        ssid == NULL || ssid_len == 0 || out_pmk32 == NULL) {
        AGENT_ERR("AgentDerivePMK_PBKDF2 invalid args "
                  "passphrase_len=%zu ssid_len=%zu",
                  passphrase_len, ssid_len);
        return -1;
    }

    /*
     * CCKeyDerivationPBKDF signature:
     *   int CCKeyDerivationPBKDF(CCPBKDFAlgorithm algorithm,
     *                            const char *password, size_t passwordLen,
     *                            const uint8_t *salt, size_t saltLen,
     *                            CCPseudoRandomAlgorithm prf,
     *                            uint rounds,
     *                            uint8_t *derivedKey, size_t derivedKeyLen);
     */
    int rc = CCKeyDerivationPBKDF(kCCPBKDF2,
                                  (const char *)passphrase,
                                  passphrase_len,
                                  ssid, ssid_len,
                                  kCCPRFHmacAlgSHA1,
                                  4096,
                                  out_pmk32, 32);
    if (rc != 0) {
        AGENT_ERR("CCKeyDerivationPBKDF rc=%d", rc);
        return rc;
    }
    AGENT_LOG("AgentDerivePMK_PBKDF2 OK ssid_len=%zu passphrase_len=%zu "
              "pmk_len=32 prf=HMAC-SHA1 iters=4096",
              ssid_len, passphrase_len);
    return 0;
}
