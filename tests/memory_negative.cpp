/** @file
 * @brief Deliberately invalid HOST-ONLY probes. Never link into firmware.
 * A C# gate requires the specific analyzer diagnostic and failing exit, so an
 * ordinary process crash cannot masquerade as successful analyzer detection.
 */
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
/// Keep the deallocation boundary visible to runtime analysis at Debug settings.
[[gnu::noinline]] void release_probe(void* pointer){free(pointer);}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    if(strcmp(argv[1],"overflow")==0){
        volatile int maximum=INT_MAX;
        volatile int result=maximum+argc; // Intentional signed overflow for UBSan.
        return result==0;
    }
    if(strcmp(argv[1],"uaf")!=0)return 2;
    auto* storage=static_cast<uint32_t*>(malloc(sizeof(uint32_t)));
    if(!storage)return 3;
    *storage=42;release_probe(storage);
    volatile uint32_t result=*static_cast<volatile uint32_t*>(storage); // Intentional freed read.
    return result==0;
}
