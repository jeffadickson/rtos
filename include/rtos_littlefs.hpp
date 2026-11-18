#pragma once
/** @file Boot-static single-owner littlefs adapter and bounded NOR simulator.
 * This interface requires RTOS_STORAGE. It neither serializes multiple callers
 * nor grants ISR access. Flash callbacks must finish synchronously and report
 * durable completion from sync; cache contents are never a durability promise.
 */
#ifndef LFS_NO_MALLOC
#error "The static adapter requires LFS_NO_MALLOC for all littlefs translation units"
#endif
#include "lfs.h"
#include <stdint.h>
#include <stddef.h>
namespace rtos::storage {
struct Device {
    // Context and callbacks are borrowed and must outlive all volume operations.
    // Callbacks return zero or a negative littlefs error; sync means durable.
    void* context{};
    int (*read)(void*,uint32_t,uint32_t,void*,uint32_t){};
    int (*program)(void*,uint32_t,uint32_t,const void*,uint32_t){};
    int (*erase)(void*,uint32_t){};
    int (*sync)(void*){};
    uint32_t blocks{},block_bytes{};
};
/** One volume with two permanent volume caches and one permanent file cache.
 * Operations open/close internally, so maximum simultaneously open files is one.
 * All bool failures retain a specific littlefs code through last_error(). A
 * failed mount never formats or overwrites media. Bind once before execution.
 * After device I/O failure, restore the device and unmount/remount before reuse;
 * littlefs metadata caches are not a recovery transaction.
 * One externally serialized owner is required. Callback reentry is rejected with
 * LFS_ERR_INVAL before touching littlefs; concurrent access remains unsupported.
 */
class Volume {
    Device device_{};lfs_config config_{};lfs_t fs_{};
    uint8_t read_[64]{},program_[64]{},lookahead_[16]{},file_[64]{};
    // File metadata and its borrowed configuration have permanent volume lifetime.
    lfs_file_t handle_{};lfs_file_config file_config_{};
    bool bound_{},mounted_{},active_{};int error_{};
    // Detect callback reentry by the single owner; this is deliberately not a lock.
    struct Operation {
        Volume& volume;bool acquired;
        explicit Operation(Volume& v):volume(v),acquired(!v.active_){if(acquired)v.active_=true;else v.error_=LFS_ERR_INVAL;}
        ~Operation(){if(acquired)volume.active_=false;}
    };
public:
    constexpr Volume()=default;
    Volume(const Volume&)=delete;
    Volume& operator=(const Volume&)=delete;
    /// Bind once to a geometry whose blocks are a multiple of the 64-byte cache.
    bool bind(Device device);
    /// Explicitly erase/reformat an unmounted volume; never called by mount.
    bool format();
    /// Mount existing media; failure leaves it unmounted and never formats it.
    bool mount();
    /// Release mounted filesystem metadata; reject an already-unmounted volume.
    bool unmount();
    /// Synchronous single-file replacement; success includes close/flush result.
    bool write(const char* name,const void* data,size_t bytes);
    /// Read up to capacity; returns byte count or negative error, not total file size.
    int read(const char* name,void* data,size_t capacity);
    int last_error()const{return error_;}
};
/** Fixed NOR image: erased bits are one, programming only clears bits.
 * Erase and program validate their entire range before changing anything. An
 * optional operation countdown injects persistent I/O failure until cleared.
 * erase_all is a fixture reset, never an implicit response to mount failure.
 */
template<size_t Blocks,size_t BlockBytes> class SimFlash {
    static_assert(Blocks>=2&&BlockBytes>=128&&BlockBytes%64==0);
    uint8_t bytes_[Blocks][BlockBytes]{};int remaining_=-1;
    bool access(){if(remaining_==0)return false;if(remaining_>0)--remaining_;return true;}
    static bool range(uint32_t b,uint32_t off,uint32_t n){return b<Blocks&&off<=BlockBytes&&n<=BlockBytes-off;}
public:
    constexpr SimFlash()=default;
    SimFlash(const SimFlash&)=delete;
    void fail_after(int operations){remaining_=operations;}
    void erase_all(){for(auto& block:bytes_)for(auto& byte:block)byte=255;remaining_=-1;}
    int read(uint32_t b,uint32_t off,void* out,uint32_t n){
        if(!range(b,off,n)||(!out&&n))return LFS_ERR_INVAL;
        if(!access())return LFS_ERR_IO;
        auto* p=static_cast<uint8_t*>(out);for(uint32_t i=0;i<n;++i)p[i]=bytes_[b][off+i];return 0;
    }
    int program(uint32_t b,uint32_t off,const void* in,uint32_t n){
        if(!range(b,off,n)||(!in&&n))return LFS_ERR_INVAL;
        if(!access())return LFS_ERR_IO;
        const auto* p=static_cast<const uint8_t*>(in);
        for(uint32_t i=0;i<n;++i)if((bytes_[b][off+i]&p[i])!=p[i])return LFS_ERR_CORRUPT;
        for(uint32_t i=0;i<n;++i)bytes_[b][off+i]&=p[i];
        return 0;
    }
    int erase(uint32_t b){if(b>=Blocks)return LFS_ERR_INVAL;if(!access())return LFS_ERR_IO;for(auto& v:bytes_[b])v=255;return 0;}
    Device device(){return {this,
        [](void* c,uint32_t b,uint32_t o,void* p,uint32_t n){return static_cast<SimFlash*>(c)->read(b,o,p,n);},
        [](void* c,uint32_t b,uint32_t o,const void* p,uint32_t n){return static_cast<SimFlash*>(c)->program(b,o,p,n);},
        [](void* c,uint32_t b){return static_cast<SimFlash*>(c)->erase(b);},
        [](void* c){return static_cast<SimFlash*>(c)->access()?0:LFS_ERR_IO;},uint32_t(Blocks),uint32_t(BlockBytes)};}
};
}
