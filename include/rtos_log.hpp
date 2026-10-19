#pragma once
#include "rtos_queue.hpp"
#include "rtos_mutex.hpp"
#include <stdarg.h>
#if !RTOS_LOGGING
#error "Enable RTOS_LOGGING to use the queued logger"
#endif
namespace rtos {
namespace detail {
struct FormatResult {size_t size{};bool valid{},truncated{};};
FormatResult format_log(char*,size_t,const char*,va_list);
}
struct LogResult {Result result{};size_t bytes{};bool truncated{};};
struct LogInfo {QueueInfo queue;uint32_t accepted{},dropped{},truncated{},errors{},sent{};bool pending{};size_t offset{};};
// A synchronous writer consumes/copies accepted bytes before returning; it never retains data.
using LogWriter=int (*)(void*,const char*,size_t);
template<size_t Slots=8,size_t MessageBytes=64> class Logger {
    static_assert(MessageBytes>0&&MessageBytes<=UINT16_MAX);
    struct Record {uint16_t length;char text[MessageBytes];};
    Queue<Record,Slots> queue_;
    Mutex drain_mutex_;
    Record pending_{};
    size_t offset_{};
    bool active_{};
    uint32_t accepted_{},dropped_{},truncated_{},errors_{},sent_{};
    LogResult publish(Record& record,bool truncated,bool isr){
        detail::Critical critical;
        auto result=isr?queue_.try_send_from_isr(record):queue_.send(record);
        if(result==Result::ok){detail::increment(accepted_);if(truncated)detail::increment(truncated_);}
        else if(result==Result::full)detail::increment(dropped_);
        return {result,result==Result::ok?record.length:0u,truncated};
    }
    LogResult write_impl(const char* text,size_t size,bool isr){
        {
            detail::Critical critical;
            if(isr?(!detail::interrupt_context()||!detail::synchronization_running()):!detail::calling_thread())return {Result::context,0,false};
        }
        if(!text&&size)return {Result::invalid,0,false};
        Record record{};size_t n=size<MessageBytes?size:MessageBytes;
        for(size_t i=0;i<n;++i)record.text[i]=text[i];
        record.length=static_cast<uint16_t>(n);return publish(record,n<size,isr);
    }
public:
    constexpr Logger()=default;
    Logger(const Logger&)=delete;
    Logger& operator=(const Logger&)=delete;
    LogResult write(const char* text,size_t size){return write_impl(text,size,false);}
    // Preformatted bytes only: no varargs formatting in an interrupt handler.
    LogResult write_from_isr(const char* text,size_t size){return write_impl(text,size,true);}
    LogResult printf(const char* format,...){
        {detail::Critical critical;if(!detail::calling_thread())return {Result::context,0,false};}
        Record record{};va_list arguments;va_start(arguments,format);
        auto formatted=detail::format_log(record.text,MessageBytes,format,arguments);va_end(arguments);
        if(!formatted.valid){detail::Critical critical;detail::increment(errors_);return {Result::invalid,0,formatted.truncated};}
        record.length=static_cast<uint16_t>(formatted.size);return publish(record,formatted.truncated,false);
    }
    Result drain(LogWriter writer,void* context,size_t budget=32){
        if(!writer||budget==0)return Result::invalid;
        LockGuard guard(drain_mutex_);
        if(!guard.owns_lock())return guard.result();
        for(unsigned calls=0;calls<16&&budget;++calls){
            if(!active_){auto result=queue_.receive(pending_);if(result!=Result::ok)return result;offset_=0;active_=true;}
            size_t remaining=pending_.length-offset_;
            if(!remaining){active_=false;detail::Critical critical;detail::increment(sent_);continue;}
            size_t n=remaining<budget?remaining:budget;
            int accepted=writer(context,pending_.text+offset_,n);
            if(accepted<0||size_t(accepted)>n){detail::Critical critical;detail::increment(errors_);return Result::invalid;}
            if(!accepted)return Result::busy;
            offset_+=size_t(accepted);budget-=size_t(accepted);
            if(offset_==pending_.length){active_=false;detail::Critical critical;detail::increment(sent_);}
        }
        return active_||queue_.snapshot().depth?Result::busy:Result::ok;
    }
    LogInfo snapshot()const{detail::Critical critical;return {queue_.snapshot(),accepted_,dropped_,truncated_,errors_,sent_,active_,offset_};}
};
}
