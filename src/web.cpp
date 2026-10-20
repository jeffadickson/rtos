#include "rtos_web.hpp"
namespace rtos::web {
namespace {
size_t length(const char* text,size_t max){if(!text)return max;size_t n=0;while(n<max&&text[n])++n;return n;}
// Cppcheck 2.13 extrapolates the bound past short literals; short-circuit NUL
// checks stop both scans at their terminator. Re-review on analyzer upgrade.
// cppcheck-suppress arrayIndexOutOfBoundsCond
bool equal(const char* a,const char* b){size_t i=0;for(;i<96&&a[i]&&b[i];++i)if(a[i]!=b[i])return false;return i<96&&a[i]==b[i];}
char lower(char c){return c>='A'&&c<='Z'?char(c+32):c;}
bool name(const char* a,const char* b){size_t i=0;while(a[i]&&b[i]){if(lower(a[i])!=b[i])return false;++i;}return a[i]==b[i];}
Bytes bytes(const char* value){return {reinterpret_cast<const uint8_t*>(value),length(value,128)};}
bool valid_path(const char* value){size_t n=length(value,96);if(!n||n==96||value[0]!='/')return false;for(size_t i=0;i<n;++i){char c=value[i];if(c=='%'||c=='\\'||c=='#'||c=='?'||c<=32||c>=127)return false;if(c=='/'&&i+1<n&&value[i+1]=='/')return false;if(c=='.'&&(i==0||value[i-1]=='/')&&(i+1==n||value[i+1]=='/'||(value[i+1]=='.'&&(i+2==n||value[i+2]=='/'))))return false;}return true;}
}
Result Session::initialize(const Asset* assets,size_t count,uint64_t now,uint64_t timeout){initialized_=false;state_=State::failed;if(!assets||count==0||count>16)return Result::invalid;for(size_t i=0;i<count;++i){if(!valid_path(assets[i].path)||!assets[i].bytes.valid()||assets[i].bytes.size>UINT32_MAX)return Result::invalid;size_t n=length(assets[i].mime,80);if(!n||n==80)return Result::invalid;for(size_t j=0;j<n;++j)if(assets[i].mime[j]<32||assets[i].mime[j]>=127)return Result::invalid;for(size_t j=0;j<i;++j)if(equal(assets[i].path,assets[j].path))return Result::invalid;}assets_=assets;count_=count;initialized_=true;return reset(now,timeout);}
Result Session::reset(uint64_t now,uint64_t timeout){if(!initialized_||!timeout||timeout>UINT64_MAX-now)return Result::invalid;used_=header_size_=header_sent_=body_sent_=0;head_=false;body_={};stats_={};code_=0;deadline_=now+timeout;state_=State::idle;return Result::ok;}
void Session::append(const char* text){for(size_t i=0;text[i];++i){if(header_size_==sizeof(header_)){state_=State::failed;return;}header_[header_size_++]=text[i];}}
void Session::number(size_t value){char digits[24];size_t n=0;do{digits[n++]=char('0'+value%10);value/=10;}while(value);while(n){char one[2]={digits[--n],0};append(one);}}
void Session::respond(unsigned code,const char* reason,Bytes body,const char* mime){code_=code;body_=body;mime_=mime;header_size_=header_sent_=body_sent_=0;state_=State::responding;if(code>=400)static_storage::saturate(stats_.errors);append("HTTP/1.0 ");number(code);append(" ");append(reason);append("\r\nContent-Type: ");append(mime_);append("\r\nContent-Length: ");number(body_.size);append("\r\nConnection: close\r\nCache-Control: no-store\r\n");if(code==405)append("Allow: GET, HEAD\r\n");append("\r\n");}
void Session::parse(){
    auto bad=[&](){respond(400,"Bad Request",bytes("Bad Request\n"),"text/plain");};
    char* line=request_;char* end=nullptr;for(size_t i=0;i+1<used_;++i)if(request_[i]=='\r'&&request_[i+1]=='\n'){end=&request_[i];break;}if(!end){bad();return;}*end=0;
    char* first=nullptr;char* second=nullptr;for(char* p=line;*p;++p)if(*p==' '){if(!first)first=p;else if(!second)second=p;else{bad();return;}}if(!first||!second||first+1==second){bad();return;}*first=0;*second=0;
    if(equal(line,"HEAD"))head_=true;else if(!equal(line,"GET")){respond(405,"Method Not Allowed",bytes("Read-only endpoint\n"),"text/plain");return;}
    bool one_one=equal(second+1,"HTTP/1.1");if(!one_one&&!equal(second+1,"HTTP/1.0")){respond(505,"HTTP Version Not Supported",bytes("Unsupported HTTP version\n"),"text/plain");return;}
    char* path=first+1;for(char* p=path;*p;++p)if(*p=='?'){*p=0;break;}if(!valid_path(path)){bad();return;}
    unsigned hosts=0,fields=0;bool content=false;char* p=end+2;
    while(*p){char* stop=nullptr;for(char* q=p;q+1<request_+used_;++q)if(q[0]=='\r'&&q[1]=='\n'){stop=q;break;}if(!stop){bad();return;}if(stop==p)break;if(++fields>16){bad();return;}*stop=0;char* colon=nullptr;for(char* q=p;*q;++q){if(*q==':'){colon=q;break;}if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='-')){bad();return;}}if(!colon||colon==p){bad();return;}*colon=0;char* value=colon+1;while(*value==' '||*value=='\t')++value;char* tail=stop;while(tail>value&&(tail[-1]==' '||tail[-1]=='\t'))*--tail=0;
        for(char* q=value;*q;++q)if(*q<32&&*q!='\t'){bad();return;}
        if(name(p,"host")){if(++hosts>1||!*value){bad();return;}for(char* q=value;*q;++q)if(*q<=32||*q=='/'||*q=='\\'){bad();return;}}
        if(name(p,"transfer-encoding")){bad();return;}
        if(name(p,"content-length")){if(content||!*value){bad();return;}content=true;for(char* q=value;*q;++q)if(*q!='0'){bad();return;}}
        p=stop+2;
    }
    if(one_one&&hosts!=1){bad();return;}
    for(size_t i=0;i<count_;++i)if(equal(path,assets_[i].path)){respond(200,"OK",assets_[i].bytes,assets_[i].mime);return;}
    respond(404,"Not Found",bytes("Not Found\n"),"text/plain");
}
Result Session::feed(Bytes input,size_t& consumed){consumed=0;if(!input.valid())return Result::invalid;if(state_!=State::idle&&state_!=State::receiving)return Result::busy;state_=State::receiving;while(consumed<input.size){uint8_t c=input.data[consumed++];static_storage::saturate(stats_.received);if((c<32&&c!='\r'&&c!='\n'&&c!='\t')||c>=127){respond(400,"Bad Request",bytes("Invalid octet\n"),"text/plain");return Result::invalid;}if(used_==sizeof(request_)-1){respond(431,"Request Header Fields Too Large",bytes("Header limit\n"),"text/plain");return Result::full;}request_[used_++]=char(c);request_[used_]=0;if(used_>=4&&request_[used_-4]=='\r'&&request_[used_-3]=='\n'&&request_[used_-2]=='\r'&&request_[used_-1]=='\n'){parse();return state_==State::responding?Result::ok:Result::invalid;}}return Result::busy;}
Result Session::pump(Write write,void* context,size_t budget){if(!write)return Result::invalid;if(state_==State::done)return Result::ok;if(state_!=State::responding)return Result::busy;for(unsigned calls=0;calls<16&&budget;++calls){bool headers=header_sent_<header_size_;size_t remaining=headers?header_size_-header_sent_:(head_?0:body_.size-body_sent_);if(!remaining){state_=State::done;return Result::ok;}size_t n=remaining<budget?remaining:budget;if(n>1024)n=1024;const uint8_t* data=headers?reinterpret_cast<const uint8_t*>(header_)+header_sent_:body_.data+body_sent_;int accepted=write(context,data,n);if(accepted<0||size_t(accepted)>n){state_=State::failed;static_storage::saturate(stats_.errors);return Result::invalid;}if(!accepted){static_storage::saturate(stats_.busy);return Result::busy;}if(headers)header_sent_+=size_t(accepted);else body_sent_+=size_t(accepted);budget-=size_t(accepted);for(int i=0;i<accepted;++i)static_storage::saturate(stats_.sent);}if(header_sent_==header_size_&&(head_||body_sent_==body_.size)){state_=State::done;return Result::ok;}return Result::busy;}
Result Session::expire(uint64_t now){if(state_==State::done)return Result::ok;if(now>=deadline_){state_=State::failed;static_storage::saturate(stats_.errors);return Result::timeout;}return Result::busy;}
void JsonWriter::byte(char ch){if(used_>=destination_.size){failed_=true;return;}destination_.data[used_++]=uint8_t(ch);}
// Short string literals return at NUL before any later index is read. Cppcheck
// 2.13 incorrectly extrapolates to index 127; re-review on analyzer upgrade.
// cppcheck-suppress arrayIndexOutOfBounds
void JsonWriter::text(const char* value){if(!value){failed_=true;return;}for(size_t i=0;i<128;++i){if(!value[i])return;byte(value[i]);}failed_=true;}
void JsonWriter::quoted(Bytes value){if(!value.valid()||value.size>96){failed_=true;return;}byte('"');const char* hex="0123456789abcdef";for(size_t i=0;i<value.size;++i){uint8_t c=value.data[i];if(c=='"'||c=='\\'){byte('\\');byte(char(c));}else if(c<32||c>=127){text("\\u00");byte(hex[c>>4]);byte(hex[c&15]);}else byte(char(c));}byte('"');}
void JsonWriter::unsigned_number(uint64_t value){char digits[20];unsigned n=0;do{digits[n++]=char('0'+value%10);value/=10;}while(value);while(n)byte(digits[--n]);}
}
