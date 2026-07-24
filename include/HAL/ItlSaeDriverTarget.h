/*
 * Build-time availability of the driver-owned SAE crypto core.
 *
 * The Hostapd/mbedTLS implementation is deliberately present only in the
 * AirportItlwm-Tahoe PBX sources phase.  Keep this capability opt-in instead
 * of inferring it from an IWX source file that is shared by every supported
 * macOS target: an older target must compile its HAL surface without gaining
 * an unresolved SAE-engine reference.
 *
 * The Tahoe Debug and Release configurations define
 * ITL_SAE_DRIVER_CRYPTO=1.  The IO80211 target check is an independent guard
 * against accidentally enabling that definition on a non-Tahoe target.
 */
#ifndef _HAL_ITL_SAE_DRIVER_TARGET_H_
#define _HAL_ITL_SAE_DRIVER_TARGET_H_

/* The Tahoe project configuration passes __MAC_26_0 symbolically.  The SAE
 * core itself does not include Apple80211 headers, so provide the same local
 * fallback used by those headers before evaluating the build setting. */
#ifndef __MAC_26_0
#define __MAC_26_0 260000
#endif

#if defined(ITL_SAE_DRIVER_CRYPTO) && ITL_SAE_DRIVER_CRYPTO && \
    defined(__IO80211_TARGET) && (__IO80211_TARGET >= __MAC_26_0)
#define ITL_SAE_DRIVER_CRYPTO_AVAILABLE 1
#else
#define ITL_SAE_DRIVER_CRYPTO_AVAILABLE 0
#endif

#endif /* _HAL_ITL_SAE_DRIVER_TARGET_H_ */
