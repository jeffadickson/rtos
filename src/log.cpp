#include "rtos_log.hpp"
namespace rtos::detail {
namespace {
struct Buffer {
    char* out;size_t capacity,used{};bool truncated{};
    void put(char ch){if(used<capacity)out[used++]=ch;else truncated=true;}
    void number(uint64_t value,unsigned base,bool upper=false){
        char digits[20];unsigned n=0;const char* alphabet=upper?"0123456789ABCDEF":"0123456789abcdef";
        do{const auto quotient=value/base;digits[n++]=alphabet[value-quotient*base];value=quotient;}while(value);
        while(n)put(digits[--n]);
    }
};
}
FormatResult format_log(char* output,size_t capacity,const char* format,va_list arguments){
    if(!output||!capacity||!format)return {};
    Buffer buffer{output,capacity};
    // Bound format scanning even when the destination has already filled.
    for(size_t i=0;i<256;++i){
        char ch=format[i];if(!ch)return {buffer.used,true,buffer.truncated};
        if(ch!='%'){buffer.put(ch);continue;}
        if(++i>=256)return {};
        unsigned longs=0;ch=format[i];
        if(ch=='l'){longs=1;if(++i>=256)return {};ch=format[i];if(ch=='l'){longs=2;if(++i>=256)return {};ch=format[i];}}
        if(ch=='d'||ch=='i'){
            int64_t value=longs==2?va_arg(arguments,long long):longs==1?va_arg(arguments,long):va_arg(arguments,int);
            uint64_t magnitude=uint64_t(value);if(value<0){buffer.put('-');magnitude=uint64_t(0)-magnitude;}
            buffer.number(magnitude,10);
        }else if(ch=='u'||ch=='x'||ch=='X'){
            uint64_t value=longs==2?va_arg(arguments,unsigned long long):longs==1?va_arg(arguments,unsigned long):va_arg(arguments,unsigned int);
            buffer.number(value,ch=='u'?10:16,ch=='X');
        }else if(longs){return {};
        }else if(ch=='s'){
            const char* text=va_arg(arguments,const char*);if(!text)text="(null)";
            size_t n=0;for(;n<96&&text[n];++n)buffer.put(text[n]);
            if(n==96)buffer.truncated=true;
        }else if(ch=='c')buffer.put(char(va_arg(arguments,int)));
        else if(ch=='p'){buffer.put('0');buffer.put('x');buffer.number(reinterpret_cast<uintptr_t>(va_arg(arguments,void*)),16);}
        else if(ch=='%')buffer.put('%');
        else return {};
    }
    return {};
}
}
