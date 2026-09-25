#ifndef ZEROOS_TYPES_H
#define ZEROOS_TYPES_H

/* Hosted test toolchains (make desktop-check) pull in <stdint.h>
 * before this header; UINT64_MAX then already provides these typedefs
 * (and redefining uint64_t to a different underlying type would be an
 * error).  Freestanding guest builds never define UINT64_MAX here, so
 * they take the typedefs below exactly as before. */
#ifndef UINT64_MAX
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#endif /* UINT64_MAX */

#endif
