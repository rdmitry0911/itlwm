/*
 * wpa_supplicant/hostapd - Build time configuration defines
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This header file can be used to define configuration defines that were
 * originally defined in Makefile. This is mainly meant for IDE use or for
 * systems that do not have suitable 'make' tool. In these cases, it may be
 * easier to have a single place for defining all the needed C pre-processor
 * defines.
 */

#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

/*
 * The kext build deliberately selects only the opaque primitives reached by
 * SAE group 19.  This header is the narrow configuration patch recorded in
 * the driver provenance manifest; no hostapd service, control, socket, file,
 * timer, or diagnostic subsystem is admitted.
 */
#ifdef CONFIG_ITL_SAE_DRIVER
#include <net80211/ieee80211_sae_platform.h>

#define CONFIG_SAE
#define CONFIG_NO_STDOUT_DEBUG
#define CONFIG_NO_WPA_MSG
#define CONFIG_NO_RANDOM_POOL
#define OS_NO_C_LIB_DEFINES

#define os_malloc itl_sae_malloc
#define os_realloc itl_sae_realloc
#define os_free itl_sae_free
/*
 * Keep the memory wrappers as real kernel-platform functions.  Mapping them
 * to XNU's fortified macros before utils/os.h declares the OS_NO_C_LIB_DEFINES
 * prototypes corrupts those declarations.  The bounded implementations live
 * in ieee80211_sae_platform.c and never reach libc.
 */
#define abs(value) __builtin_abs(value)
#define abort() panic("itl_sae hostapd invariant")
#endif /* CONFIG_ITL_SAE_DRIVER */

/* Insert configuration defines, e.g., #define EAP_MD5, here, if needed. */

#ifdef CONFIG_WIN32_DEFAULTS
#define CONFIG_NATIVE_WINDOWS
#define CONFIG_ANSI_C_EXTRA
#define CONFIG_WINPCAP
#define IEEE8021X_EAPOL
#define PKCS12_FUNCS
#define PCSC_FUNCS
#define CONFIG_CTRL_IFACE
#define CONFIG_CTRL_IFACE_NAMED_PIPE
#define CONFIG_DRIVER_NDIS
#define CONFIG_NDIS_EVENTS_INTEGRATED
#define CONFIG_DEBUG_FILE
#define EAP_MD5
#define EAP_TLS
#define EAP_MSCHAPv2
#define EAP_PEAP
#define EAP_TTLS
#define EAP_GTC
#define EAP_OTP
#define EAP_LEAP
#define EAP_TNC
#define _CRT_SECURE_NO_DEPRECATE

#ifdef USE_INTERNAL_CRYPTO
#define CONFIG_TLS_INTERNAL_CLIENT
#define CONFIG_INTERNAL_LIBTOMMATH
#define CONFIG_CRYPTO_INTERNAL
#endif /* USE_INTERNAL_CRYPTO */
#endif /* CONFIG_WIN32_DEFAULTS */

#endif /* BUILD_CONFIG_H */
