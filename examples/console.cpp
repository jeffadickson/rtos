#include "rtos_ipc.hpp"
// Linkable starting example. Replace the sink and clock with verified board hooks.
static rtos::Logger<4,64> console;
static rtos::AtomicU32 accepted_bytes;
static rtos::Thread application,output;
static rtos::Stack<1024> application_stack,output_stack;
static int write_some(void*,const char*,size_t bytes){
    accepted_bytes.fetch_add(static_cast<uint32_t>(bytes));
    return static_cast<int>(bytes); // Counting sink; does not drive a UART.
}
static void run_application(void*){
    for(uint32_t count=0;;++count){
        auto result=console.printf("count=%u\n",static_cast<unsigned>(count));
        // A full diagnostic queue drops this record; the application keeps running.
        (void)result;
        rtos::sleep_for(100);
    }
}
static void run_output(void*){
    for(;;){console.drain(write_some,nullptr,32);rtos::sleep_for(1);}
}
int main(){
    if(!rtos::create(application,run_application,nullptr,2,application_stack,"application")||
       !rtos::create(output,run_output,nullptr,1,output_stack,"output"))return 1;
    rtos::start(10000000); // Example only: supply the real board timer clock.
}
