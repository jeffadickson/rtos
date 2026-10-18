#include <cassert>
#include <stddef.h>
extern "C" void* memset(void*, int, size_t);
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memmove(void*, const void*, size_t);
extern "C" int memcmp(const void*, const void*, size_t);
int main() {
    unsigned char bytes[]{1,2,3,4,5,6};
    assert(memmove(bytes+1,bytes,5)==bytes+1);
    const unsigned char right[]{1,1,2,3,4,5};
    assert(memcmp(bytes,right,6)==0);
    memmove(bytes,bytes+1,5);
    const unsigned char left[]{1,2,3,4,5,5};
    assert(memcmp(bytes,left,6)==0);
    assert(memset(bytes,0x1ff,3)==bytes);
    assert(bytes[0]==255 && bytes[2]==255 && bytes[3]==4);
    assert(memcpy(bytes,right,6)==bytes && memcmp(bytes,right,6)==0);
    assert(memcmp(bytes,left,6)<0 && memcmp(left,bytes,6)>0);
    memmove(bytes,bytes,6); assert(memcmp(bytes,right,6)==0);
    memset(bytes,0,0); assert(bytes[0]==1);
}
