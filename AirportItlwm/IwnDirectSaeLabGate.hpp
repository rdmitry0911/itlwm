/*
 * Physical exclusion gate for the IWN direct-SAE laboratory stimulus.
 *
 * This is deliberately a compile-time conjunction.  A boot argument,
 * IORegistry property, environment value, or UserClient type must never make
 * a normal AirportItlwm artifact accept a password-bearing diagnostic input.
 */
#ifndef AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP

#include <HAL/ItlSaeDriverTarget.h>

#if defined(IWN_SOFTWARE_PMF_LAB_BUILD) && IWN_SOFTWARE_PMF_LAB_BUILD && \
    ITL_SAE_DRIVER_CRYPTO_AVAILABLE
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 1
#else
#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 0
#endif

/* The existing private WCL CIPHER_PWD route and the new laboratory-only
 * UserClient exercise the same IWN-only lower-half.  Keep their physical
 * enable boundary identical while preserving a name that makes the WCL
 * provenance explicit at its call site. */
#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS \
    AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS

#endif /* AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_GATE_HPP */
