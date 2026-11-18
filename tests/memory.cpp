#include <cassert>
#include <stddef.h>
extern "C" void* memset(void*, int, size_t);
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memmove(void*, const void*, size_t);
extern "C" int memcmp(const void*, const void*, size_t);
extern "C" size_t nexrt_strspn(const char*,const char*);
extern "C" size_t nexrt_strcspn(const char*,const char*);
int main() {
    unsigned char bytes[]{1,2,3,4,5,6};
    assert(memmove(bytes+1,bytes,5)==bytes+1);
    const unsigned char right[]{1,1,2,3,4,5};
    assert(memcmp(bytes,right,6)==0);
    memmove(bytes,bytes+1,5);
    const unsigned char left[]{1,2,3,4,5,5};
    assert(memcmp(bytes,left,6)==0);
    // Deliberately verify the C memset unsigned-char conversion contract.
    // cppcheck-suppress memsetValueOutOfRange
    assert(memset(bytes,0x1ff,3)==bytes);
    assert(bytes[0]==255 && bytes[2]==255 && bytes[3]==4);
    assert(memcpy(bytes,right,6)==bytes && memcmp(bytes,right,6)==0);
    assert(memcmp(bytes,left,6)<0 && memcmp(left,bytes,6)>0);
    memmove(bytes,bytes,6); assert(memcmp(bytes,right,6)==0);
    // Deliberately verify that a zero-length operation preserves the buffer.
    // cppcheck-suppress memsetZeroBytes
    memset(bytes,0,0); assert(bytes[0]==1);
    assert(nexrt_strspn("///state","/")==3);
    assert(nexrt_strcspn("state/next","/")==5);
    assert(nexrt_strspn("","abc")==0&&nexrt_strspn("abc","")==0);
    assert(nexrt_strcspn("abc","")==3&&nexrt_strcspn("","abc")==0);
    const char high[]={char(255),char(128),'x',0};
    const char accept[]={char(255),char(128),0};
    assert(nexrt_strspn(high,accept)==2&&nexrt_strcspn(high,accept)==0);
}
