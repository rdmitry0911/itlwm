/*
 * Minimal limits.h for the kernel-only mbedTLS arithmetic subset.
 *
 * MacKernelSDK intentionally does not ship the user-space umbrella header,
 * while mbedTLS 3.6 unconditionally includes <limits.h>.  The selected SAE
 * profile is x86_64 Tahoe only; these definitions describe that ABI and do
 * not pull a libc header into the kext build.
 */
#ifndef ITL_SAE_KERNEL_COMPAT_LIMITS_H
#define ITL_SAE_KERNEL_COMPAT_LIMITS_H

#include <stdint.h>

#define CHAR_BIT 8
#define SCHAR_MAX 127
#define SCHAR_MIN (-128)
#define UCHAR_MAX 255U
#define CHAR_MAX SCHAR_MAX
#define CHAR_MIN SCHAR_MIN
#define SHRT_MAX 32767
#define SHRT_MIN (-32768)
#define USHRT_MAX 65535U
#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define UINT_MAX 4294967295U
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-9223372036854775807L - 1L)
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define ULLONG_MAX 18446744073709551615ULL

#endif /* ITL_SAE_KERNEL_COMPAT_LIMITS_H */
