#ifndef KernelMemoryTestSupport_hpp
#define KernelMemoryTestSupport_hpp
#include <stddef.h>

#ifdef __APPLE__
// The user-space SDK does not expose this kernel credential-scrubbing helper.
static inline void explicit_bzero(void *buffer, size_t size)
{
    volatile unsigned char *bytes = static_cast<volatile unsigned char *>(buffer);
    while (size-- != 0)
        *bytes++ = 0;
}
#endif
#endif
