#include "rtos_web.hpp"
#include "web_assets.hpp"
namespace {rtos::web::Session session;const rtos::web::Asset route[]={ {"/","text/html; charset=utf-8",{rtos::assets::introduction,rtos::assets::introduction_size}} };volatile size_t sent;int write(void*,const uint8_t*,size_t size){sent=sent+size;return int(size);} }
extern "C" int main(){if(session.initialize(route,1,0,1000)!=rtos::web::Result::ok)return 1;const uint8_t request[]="GET / HTTP/1.0\r\n\r\n";size_t consumed;session.feed({request,sizeof(request)-1},consumed);while(session.state()==rtos::web::State::responding)session.pump(write,nullptr,128);return 0;}
