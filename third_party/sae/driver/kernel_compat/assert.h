/*
 * Kernel assert surface for the selected mbedTLS SAE arithmetic sources.
 *
 * mbedTLS imports <assert.h> through its common header.  Route that include
 * to XNU's kext assertion contract rather than the macOS user SDK; the
 * selected source subset does not need a user-space assertion facility.
 */
#ifndef ITL_SAE_KERNEL_COMPAT_ASSERT_H
#define ITL_SAE_KERNEL_COMPAT_ASSERT_H

#include <kern/assert.h>

#endif /* ITL_SAE_KERNEL_COMPAT_ASSERT_H */
