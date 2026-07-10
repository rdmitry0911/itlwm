#ifndef ITLWM_TEST_COMPAT_AVAILABILITY_H
#define ITLWM_TEST_COMPAT_AVAILABILITY_H

#include <stddef.h>

#ifndef __offsetof
#define __offsetof(type, member) offsetof(type, member)
#endif

#ifndef __MAC_10_15
#define __MAC_10_15 101500
#endif

#ifndef __MAC_11_0
#define __MAC_11_0 110000
#endif

#ifndef __MAC_12_0
#define __MAC_12_0 120000
#endif

#ifndef __MAC_13_0
#define __MAC_13_0 130000
#endif

#ifndef __MAC_14_0
#define __MAC_14_0 140000
#endif

#ifndef __MAC_14_4
#define __MAC_14_4 140400
#endif

#ifndef __MAC_26_0
#define __MAC_26_0 260000
#endif

#endif /* ITLWM_TEST_COMPAT_AVAILABILITY_H */
