#pragma once
#include <stdint.h>
/* No libc logging, abort, random generator or OS allocator is pulled into the
 * freestanding image. A violated internal invariant traps rather than returning
 * into corrupt state. Ordinary packet/resource errors still return lwIP errors.
 */
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x) do {} while(0)
#if defined(_MSC_VER)
#include <intrin.h>
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); __debugbreak(); for(;;){} } while(0)
#else
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); __builtin_trap(); } while(0)
#endif
