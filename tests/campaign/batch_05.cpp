#include "common.hpp"
#include "rtos_uart.hpp"
using namespace rtos;
CASE(41,"NXR-041","empty UART write avoids invalid device address"){AxiUartTx t(UINTPTR_MAX);assert(t.write(nullptr,0)==0);}
CASE(42,"NXR-041","null nonempty UART source rejected"){uint32_t r[3]{};AxiUartTx t{uintptr_t(r)};assert(t.write(nullptr,1)==-1&&r[1]==0);}
CASE(43,"NXR-041","unaligned UART base rejected before MMIO"){AxiUartTx t(1);assert(t.write("x",1)==-1);}
CASE(44,"NXR-041","overflowing UART register range rejected"){AxiUartTx t(UINTPTR_MAX-3);assert(t.write("x",1)==-1);}
CASE(45,"NXR-041","unrepresentable UART count rejected before source read"){uint32_t r[3]{};AxiUartTx t{uintptr_t(r)};assert(t.write("x",size_t(INT_MAX)+1)==-1&&!r[1]);}
CASE(46,"NXR-041","UART high-bit byte remains unsigned"){uint32_t r[3]{};AxiUartTx t{uintptr_t(r)};const char c=char(255);assert(t.write(&c,1)==1&&r[1]==255);}
CASE(47,"NXR-041","full UART accepts no bytes"){uint32_t r[3]{0,77,8};AxiUartTx t{uintptr_t(r)};assert(t.write("x",1)==0&&r[1]==77);}
CASE(48,"NXR-041","TXFULL flag controls admission without idle flag"){uint32_t r[3]{};AxiUartTx t{uintptr_t(r)};assert(t.write("x",1)==1&&r[1]=='x');}
CASE(49,"NXR-041","TX write never changes status or RX register"){uint32_t r[3]{99,0,4};AxiUartTx t{uintptr_t(r)};assert(t.write("x",1)==1&&r[0]==99&&r[2]==4);}
CASE(50,"NXR-041","ready transport returns exact multi-byte count"){uint32_t r[3]{};AxiUartTx t{uintptr_t(r)};assert(t.write("abc",3)==3&&r[1]=='c');}
