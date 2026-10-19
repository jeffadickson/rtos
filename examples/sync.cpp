#include "rtos_ipc.hpp"
#include "rtos_queue.hpp"
#include "rtos_atomic.hpp"
#include "rtos_mutex.hpp"
#if RTOS_LOGGING
#include "rtos_log.hpp"
static rtos::Logger<8,64> console;
static rtos::AtomicU32 output_bytes;
int sink(void*,const char*,size_t n){output_bytes.fetch_add(static_cast<uint32_t>(n));return int(n);}
#endif
static rtos::DeviceTransactions device;
static rtos::Queue<uint32_t,4> messages;
static rtos::AtomicU32 processed;
static rtos::Semaphore ready(0,1);
static rtos::EventFlags events;
static rtos::Thread producer,consumer;
static rtos::Stack<1024> producer_stack,consumer_stack;
void produce(void*){for(;;){
#if RTOS_LOGGING
console.printf("value=%u\n",42u);
#endif
messages.send(42);ready.release();events.set(1);rtos::sleep_for(2);}}
void consume(void*){for(;;){
#if RTOS_LOGGING
console.drain(sink,nullptr,32);
#endif
uint32_t observed;uint32_t message;if(messages.receive(message,10)==rtos::Result::ok)device.transact(5,[&](){processed.fetch_add(message);return rtos::Result::ok;});ready.acquire(10);events.wait_any(1,observed,10,rtos::EventClear::yes);}}
int main(){rtos::create(producer,produce,nullptr,1,producer_stack,"producer");rtos::create(consumer,consume,nullptr,2,consumer_stack,"consumer");rtos::start(10000000);}
