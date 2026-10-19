#pragma once
#include "rtos_static.hpp"
namespace rtos::web {
using static_storage::Bytes;using static_storage::MutableBytes;using static_storage::Result;
struct Asset { const char* path;const char* mime;Bytes bytes; };
enum class State:uint8_t { idle,receiving,responding,done,failed };
using Write=int(*)(void*,const uint8_t*,size_t);
struct Stats { uint32_t received{},sent{},errors{},busy{}; };
// One owner, one request per connection; asset bytes must remain immutable until done/abort.
class Session {
    const Asset* assets_{};size_t count_{},used_{},header_size_{},header_sent_{},body_sent_{};
    char request_[512]{},header_[192]{};Bytes body_{};const char* mime_{"text/plain"};
    uint64_t deadline_{};State state_{State::failed};Stats stats_{};unsigned code_{};bool head_{},initialized_{};
    void respond(unsigned code,const char* reason,Bytes body,const char* mime);
    void parse();void append(const char* text);void number(size_t value);
public:
    /// Borrow immutable assets; now/timeout share the caller's monotonic time units.
    Result initialize(const Asset*,size_t,uint64_t now,uint64_t timeout);
    /// Discard request progress/statistics and set a new deadline using the same assets.
    /// The owner must finish/abandon the previous transport connection before reusing it.
    Result reset(uint64_t now,uint64_t timeout);
    /// Consume a bounded prefix; report its length even when the input has trailing bytes.
    Result feed(Bytes input,size_t& consumed);
    /// Offer up to budget response bytes; writer copies/consumes before returning.
    Result pump(Write,void*,size_t budget=256);
    /// Advance timeout policy explicitly; there is no hidden timer thread.
    Result expire(uint64_t now);
    void abort(){state_=State::failed;}
    State state()const{return state_;}unsigned status()const{return code_;}
    Stats statistics()const{return stats_;}static constexpr size_t request_capacity(){return 512;}
};
// No partial JSON is returned on exhaustion. Names are escaped, bounded byte strings.
class JsonWriter {
    // Caller owns destination; exhaustion makes finish() return no partial JSON.
    MutableBytes destination_;size_t used_{};bool failed_{};
    void byte(char ch);
public:
    explicit JsonWriter(MutableBytes destination):destination_(destination),failed_(!destination.valid()){}
    void text(const char* literal);void quoted(Bytes);void unsigned_number(uint64_t);
    Bytes finish()const{return failed_?Bytes{}:Bytes{destination_.data,used_};}
    bool good()const{return !failed_;}
};
}
