/*
 * wpa_supplicant/hostapd - Default include files
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This header file is included into all C files so that commonly used header
 * files can be selected with OS specific ifdef blocks in one place instead of
 * having to have OS/C library specific selection in many files.
 */

#ifndef INCLUDES_H
#define INCLUDES_H

/* Include possible build time configuration before including anything else */
#include "build_config.h"

#ifdef CONFIG_ITL_SAE_DRIVER
/*
 * KEXT-only replacement for hostapd's libc/POSIX umbrella.  The imported
 * subset is compiled with OS_NO_C_LIB_DEFINES and reaches only the bounded
 * allocator/RNG/zeroization definitions in ieee80211_sae_platform.c.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
typedef struct itl_sae_unused_file FILE;
#else
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef _WIN32_WCE
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#endif /* _WIN32_WCE */
#include <ctype.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif /* _MSC_VER */

#ifndef CONFIG_NATIVE_WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef __vxworks
#include <sys/uio.h>
#include <sys/time.h>
#endif /* __vxworks */
#endif /* CONFIG_NATIVE_WINDOWS */
#endif /* CONFIG_ITL_SAE_DRIVER */

#endif /* INCLUDES_H */
