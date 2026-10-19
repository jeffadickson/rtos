#include "rtos_sync.hpp"
static rtos::Semaphore ready(0,1);
static rtos::EventFlags events;
static rtos::Thread producer,consumer;
static rtos::Stack<1024> producer_stack,consumer_stack;
void produce(void*){for(;;){ready.release();events.set(1);rtos::sleep_for(2);}}
void consume(void*){for(;;){uint32_t observed;ready.acquire(10);events.wait(1,observed,10,false,true);}}
int main(){rtos::create(producer,produce,nullptr,1,producer_stack,"producer");rtos::create(consumer,consume,nullptr,2,consumer_stack,"consumer");rtos::start(10000000);}
