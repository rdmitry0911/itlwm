/*
 * Minimal stdlib.h for the kernel-only SAE arithmetic subset.
 *
 * mbedTLS 3.6's alignment helper includes <stdlib.h> even though the selected
 * SAE profile needs neither allocation nor process-library facilities from
 * that header.  Letting that include fall through to the macOS user SDK pulls
 * libc's bounds-annotated allocation declarations into a kext compile.  Keep
 * the compatibility surface limited to kernel types and the sole abs() use in
 * the imported SAE source.
 */
#ifndef ITL_SAE_KERNEL_COMPAT_STDLIB_H
#define ITL_SAE_KERNEL_COMPAT_STDLIB_H

#include <sys/types.h>

#ifndef abs
#define abs(value) __builtin_abs(value)
#endif

#endif /* ITL_SAE_KERNEL_COMPAT_STDLIB_H */
