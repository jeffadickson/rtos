#include "rtos.hpp"
namespace { rtos::Thread task;rtos::Stack<512> stack;volatile uint32_t count;
void work(void*){for(;;){++count;rtos::sleep_for(1);}} }
extern "C" int main(){if(!rtos::create(task,work,nullptr,1,stack,"nano"))return 1;rtos::start(10000000);}
