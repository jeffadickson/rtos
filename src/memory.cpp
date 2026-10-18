// GCC can emit these operations for aggregate initialization/copies even in
// freestanding C++. Volatile byte accesses prevent recursive builtin lowering.
#include <stddef.h>
#include <stdint.h>
extern "C" void* memset(void* destination, int value, size_t count) {
    auto* out=static_cast<volatile unsigned char*>(destination);
    for (size_t i=0; i<count; ++i) out[i]=static_cast<unsigned char>(value);
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* out=static_cast<volatile unsigned char*>(destination);
    auto* in=static_cast<const volatile unsigned char*>(source);
    for (size_t i=0; i<count; ++i) out[i]=in[i];
    return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* out=static_cast<volatile unsigned char*>(destination);
    auto* in=static_cast<const volatile unsigned char*>(source);
    if (reinterpret_cast<uintptr_t>(destination)<reinterpret_cast<uintptr_t>(source)) {
        for (size_t i=0; i<count; ++i) out[i]=in[i];
    } else {
        while (count) { --count; out[count]=in[count]; }
    }
    return destination;
}
extern "C" int memcmp(const void* left, const void* right, size_t count) {
    auto* a=static_cast<const volatile unsigned char*>(left);
    auto* b=static_cast<const volatile unsigned char*>(right);
    for (size_t i=0; i<count; ++i) if (a[i]!=b[i]) return a[i]<b[i] ? -1 : 1;
    return 0;
}
