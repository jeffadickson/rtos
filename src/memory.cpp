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
// littlefs needs separator-span operations. These small freestanding routines
// avoid linking a general libc runtime; unsigned bytes preserve high-bit input.
// Prefixed entry points let host tests challenge our code without accidentally
// resolving the test calls to the host libc's implementations instead.
extern "C" size_t nexrt_strspn(const char* text,const char* accepted) {
    size_t count=0;
    while(text[count]){
        const auto value=static_cast<unsigned char>(text[count]);
        const char* p=accepted;while(*p&&static_cast<unsigned char>(*p)!=value)++p;
        if(!*p)break;
        ++count;
    }
    return count;
}
extern "C" size_t nexrt_strcspn(const char* text,const char* rejected) {
    size_t count=0;
    while(text[count]){
        const auto value=static_cast<unsigned char>(text[count]);
        const char* p=rejected;while(*p&&static_cast<unsigned char>(*p)!=value)++p;
        if(*p)break;
        ++count;
    }
    return count;
}
extern "C" size_t strspn(const char* text,const char* accepted){return nexrt_strspn(text,accepted);}
extern "C" size_t strcspn(const char* text,const char* rejected){return nexrt_strcspn(text,rejected);}
