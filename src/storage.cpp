#include "rtos_littlefs.hpp"
namespace rtos::storage {
bool Volume::bind(Device d){
    if(bound_||!d.read||!d.program||!d.erase||!d.sync||d.blocks<2||d.block_bytes<128||d.block_bytes%64){error_=LFS_ERR_INVAL;return false;}
    device_=d;config_.context=&device_;
    config_.read=[](const lfs_config* c,lfs_block_t b,lfs_off_t o,void* p,lfs_size_t n){auto& d=*static_cast<Device*>(c->context);return d.read(d.context,b,o,p,n);};
    config_.prog=[](const lfs_config* c,lfs_block_t b,lfs_off_t o,const void* p,lfs_size_t n){auto& d=*static_cast<Device*>(c->context);return d.program(d.context,b,o,p,n);};
    config_.erase=[](const lfs_config* c,lfs_block_t b){auto& d=*static_cast<Device*>(c->context);return d.erase(d.context,b);};
    config_.sync=[](const lfs_config* c){auto& d=*static_cast<Device*>(c->context);return d.sync(d.context);};
    config_.read_size=1;config_.prog_size=1;config_.block_size=d.block_bytes;config_.block_count=d.blocks;
    config_.block_cycles=100;config_.cache_size=64;config_.lookahead_size=sizeof(lookahead_);
    config_.read_buffer=read_;config_.prog_buffer=program_;config_.lookahead_buffer=lookahead_;
    bound_=true;error_=0;return true;
}
bool Volume::format(){if(!bound_||mounted_){error_=LFS_ERR_INVAL;return false;}error_=lfs_format(&fs_,&config_);return error_==0;}
bool Volume::mount(){if(!bound_||mounted_){error_=LFS_ERR_INVAL;return false;}error_=lfs_mount(&fs_,&config_);mounted_=error_==0;return mounted_;}
bool Volume::unmount(){if(!mounted_){error_=LFS_ERR_INVAL;return false;}error_=lfs_unmount(&fs_);if(!error_)mounted_=false;return error_==0;}
bool Volume::write(const char* name,const void* data,size_t bytes){
    if(!mounted_||!name||(!data&&bytes)||bytes>INT32_MAX){error_=LFS_ERR_INVAL;return false;}
    lfs_file_t file{};lfs_file_config options{};options.buffer=file_;
    error_=lfs_file_opencfg(&fs_,&file,name,LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC,&options);if(error_)return false;
    auto count=lfs_file_write(&fs_,&file,data,lfs_size_t(bytes));
    int close=lfs_file_close(&fs_,&file);
    error_=count<0?int(count):size_t(count)!=bytes?LFS_ERR_IO:close;return error_==0;
}
int Volume::read(const char* name,void* data,size_t capacity){
    if(!mounted_||!name||(!data&&capacity)||capacity>INT32_MAX){error_=LFS_ERR_INVAL;return error_;}
    lfs_file_t file{};lfs_file_config options{};options.buffer=file_;
    error_=lfs_file_opencfg(&fs_,&file,name,LFS_O_RDONLY,&options);if(error_)return error_;
    auto count=lfs_file_read(&fs_,&file,data,lfs_size_t(capacity));int close=lfs_file_close(&fs_,&file);
    error_=count<0?int(count):close;return error_?error_:int(count);
}
}
