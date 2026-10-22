#include "elf.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <iomanip>

using namespace nexrt::rv;
namespace {
/// UART text is separate from the JSON result: stderr is the console, and the
/// optional file preserves the exact transmitted bytes. No synthesized greetings.
struct UartOutput {
    std::ofstream file;
    std::string line;
    static void receive(void* context,uint8_t byte){
        auto& self=*static_cast<UartOutput*>(context);
        if(self.file.is_open()){
            self.file.put(char(byte));
            if(!self.file)throw std::runtime_error("UART log write failed");
        }
        if(byte=='\n'){
            std::cerr<<"[uart] "<<self.line<<'\n';self.line.clear();
            if(self.file.is_open())self.file.flush();
        }else{
            // Bound host console buffering even for firmware without newlines.
            self.line+=char(byte);
            if(self.line.size()==1024){std::cerr<<"[uart] "<<self.line<<'\n';self.line.clear();}
        }
    }
};
uint64_t number(const std::string& text) {size_t used=0;auto value=std::stoull(text,&used,0);if(used!=text.size())throw std::invalid_argument("Invalid integer: "+text);return value;}
std::string array(const Machine& cpu,uint32_t address,unsigned from,unsigned count) {
    std::ostringstream s;s<<'[';for(unsigned i=0;i<count;i++){if(i)s<<',';s<<cpu.peek32(address+4*(from+i));}return s.str()+']';
}
}
/// CLI runs the actual ELF on one modeled hart. Timers use virtual instruction
/// cost, not host time; host limits only bound the test. No hardware programming.
int main(int argc,char** argv) {
    try {
        Profile profile;std::string elf,output,uartLog;uint64_t maximum=100000000,hostSeconds=10,targetTicks=0,clock=100000000;
        for(int i=1;i<argc;i++){
            const std::string a=argv[i];auto value=[&](){if(i+1==argc)throw std::invalid_argument("Missing value for "+a);return std::string(argv[++i]);};
            if(a=="--elf")elf=value();else if(a=="--output")output=value();
            else if(a=="--uart-log")uartLog=value();
            else if(a=="--max-instructions")maximum=number(value());else if(a=="--host-seconds")hostSeconds=number(value());else if(a=="--target-ticks")targetTicks=number(value());else if(a=="--clock-hz")clock=number(value());
            else if(a=="--cycles-per-instruction"){uint64_t n=number(value());if(!n||n>1000000)throw std::invalid_argument("Invalid instruction cost");profile.cycles_per_instruction=uint32_t(n);}
            else if(a=="--user-pmp")profile.user_pmp=true;else if(a=="--zaamo")profile.zaamo=true;
            else if(a=="--no-compressed")profile.compressed=false;else if(a=="--no-bitmanip")profile.bitmanip=false;else if(a=="--wfi-sleep")profile.wfi_sleep=true;
            else if(a=="--ram-bytes"){const auto n=number(value());if(n>UINT32_MAX)throw std::invalid_argument("RAM too large");profile.ram_bytes=size_t(n);}
            else if(a=="--help"){std::cout<<"nexrt-rv --elf FILE [--user-pmp] [--zaamo] [--no-compressed] [--wfi-sleep] [--cycles-per-instruction N] [--max-instructions N] [--host-seconds N] [--target-ticks N] [--output FILE]\n";return 0;}
            else throw std::invalid_argument("Unknown option: "+a);
        }
        if(elf.empty()||!maximum||!hostSeconds||!clock)throw std::invalid_argument("Specify ELF and nonzero instruction/time/clock limits");
        Machine cpu(profile);Image image(elf,cpu);
        UartOutput uart;
        if(!uartLog.empty()){
            uart.file.open(uartLog,std::ios::binary);
            if(!uart.file)throw std::runtime_error("Cannot open UART log");
        }
        cpu.uart_sink=UartOutput::receive;cpu.uart_context=&uart;
        uint32_t status=0;bool soak=false,atomics=false;
        auto symbol=image.symbols.find("nexrt_soak_status");
        if(symbol!=image.symbols.end()){if(symbol->second.size!=228)throw std::runtime_error("Unexpected soak status layout");soak=true;status=symbol->second.address;}
        else if((symbol=image.symbols.find("nexrt_atomic_status"))!=image.symbols.end()){if(symbol->second.size!=48)throw std::runtime_error("Unexpected atomic status layout");atomics=true;status=symbol->second.address;}
        if(soak||atomics){
            const auto it=image.symbols.find(soak?"nexrt_soak_clock_hz":"nexrt_atomic_clock_hz");
            if(it==image.symbols.end()||it->second.size!=4||cpu.peek32(it->second.address)!=clock)throw std::runtime_error("Firmware clock identity does not match model clock");
        }
        const auto started=std::chrono::steady_clock::now();auto elapsed=[&](){return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
        std::string outcome="instruction_limit_reached";uint32_t phase=0,failure=0;
        while(cpu.steps<maximum){
            cpu.run(std::min<uint64_t>(4096,maximum-cpu.steps));
            if((soak||atomics)&&cpu.faults){
                std::cerr<<"Unexpected firmware fault cause="<<cpu.last_fault.cause<<" pc="<<cpu.last_fault.pc<<'\n';outcome="failed";break;
            }
            if(soak||atomics){
                phase=cpu.peek32(status);failure=cpu.peek32(status+8);
                if(phase==3||failure){outcome="failed";break;}
                if(phase==2){
                    outcome="completed";
                    if(atomics&&(cpu.peek32(status+4)!=uint32_t(profile.zaamo)||cpu.peek32(status+16)!=192||cpu.peek32(status+24)!=6||cpu.peek32(status+28)!=1||cpu.peek32(status+32)!=384||cpu.peek32(status+36)!=5))outcome="failed";
                    break;
                }
                if(phase>3){outcome="failed";break;}
                if(soak&&targetTicks&&cpu.peek32(status+20)>=targetTicks){outcome="target_limit_reached";break;}
            }
            if(elapsed()>=double(hostSeconds)){outcome="wall_limit_reached";break;}
        }
        const double wall=elapsed();std::ostringstream result;
        result<<std::setprecision(12)<<"{\"backend\":\"cpp-instruction-model\",\"outcome\":\""<<outcome<<"\",\"wallSeconds\":"<<wall<<",\"virtualCycles\":"<<cpu.mtime<<",\"virtualSeconds\":"<<double(cpu.mtime)/double(clock)
              <<",\"steps\":"<<cpu.steps<<",\"retired\":"<<cpu.retired<<",\"instructionsPerHostSecond\":"<<double(cpu.retired)/wall<<",\"cyclesPerInstruction\":"<<profile.cycles_per_instruction
              <<",\"wfiSleep\":"<<(profile.wfi_sleep?"true":"false")<<",\"skippedCycles\":"<<cpu.skipped_cycles<<",\"userPmp\":"<<(profile.user_pmp?"true":"false")<<",\"zaamo\":"<<(profile.zaamo?"true":"false")
              <<",\"phase\":"<<phase<<",\"failure\":"<<failure<<",\"timerTraps\":"<<cpu.timer_interrupts<<",\"ecalls\":"<<cpu.ecalls<<",\"amoOperations\":"<<cpu.amo_operations<<",\"pc\":"<<cpu.pc
              <<",\"lastTrapCause\":"<<cpu.last_trap.cause<<",\"lastTrapPc\":"<<cpu.last_trap.pc;
        if(soak)result<<",\"targetTicks\":"<<cpu.peek32(status+20)<<",\"jobs\":"<<cpu.peek32(status+24)<<",\"modes\":"<<array(cpu,status,7,3)<<",\"workerJobs\":"<<array(cpu,status,10,4)<<",\"dispatches\":"<<array(cpu,status,19,6)<<",\"stackEstimates\":"<<array(cpu,status,31,6);
        if(atomics)result<<",\"atomicStatus\":"<<array(cpu,status,0,12);
        result<<"}\n";std::cout<<result.str();
        if(!output.empty()){std::ofstream file(output);if(!file)throw std::runtime_error("Cannot open report output");file<<result.str();if(!file)throw std::runtime_error("Report output failed");}
        return outcome=="failed"?1:0;
    }catch(const std::exception& error){std::cerr<<"Interpreter error: "<<error.what()<<'\n';return 2;}
}
