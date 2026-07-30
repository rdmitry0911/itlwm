/*
 * Build gates for the product WCL SAE path and its separate laboratory
 * stimulus.
 *
 * Tahoe's normal WCL CIPHER_PWD carrier is a product input.  It is available
 * whenever the in-kext SAE crypto core is part of the target.  The diagnostic
 * UserClient remains a stricter compile-time conjunction: no boot argument,
 * IORegistry property, environment value, or UserClient type can admit it in
 * an ordinary artifact.
 */
#ifndef AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP

#include <HAL/ItlSaeDriverTarget.h>

#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1
#else
#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 0
#endif

#if defined(IWN_SOFTWARE_PMF_LAB_BUILD) && IWN_SOFTWARE_PMF_LAB_BUILD && \
    AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 1
#else
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 0
#endif

#endif /* AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP */
