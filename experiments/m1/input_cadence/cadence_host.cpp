// Real Session/Worker/Bridge host, no KEX. Shadow candidates use the same
// InputTimeout predicate and the SAME accepted stream; tick/wall OR preserved.
#include "network_input.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <array>
#include <cstring>
using Clock=std::chrono::steady_clock;
static FILE* journal{};static std::uint64_t tick{};
struct Candidate { m12a::InputTimeout policy;unsigned buttons{};std::uint64_t at{},ms{}; };
static std::array<Candidate,3> candidates={Candidate{{6,150}},Candidate{{8,150}},Candidate{{10,150}}};
static long long us(){return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();}
static unsigned value(const char* s,const char* key){const char* p=std::strstr(s,key);return p?static_cast<unsigned>(std::strtoul(p+std::strlen(key),nullptr,10)):0;}
static void record(const char* event,const char* fmt="",...) {
    char detail[7000]{};va_list a;va_start(a,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,a);va_end(a);
    std::fprintf(journal,"{\"event\":\"%s\",\"us\":%lld,\"wall_ms\":%llu,\"tid\":%lu,\"host_tick\":%llu%s%s}\n",event,us(),GetTickCount64(),GetCurrentThreadId(),tick,detail[0]?",":"",detail);
    if(std::strcmp(event,"network_input_accepted")==0)for(auto& c:candidates){c.buttons=value(detail,"\"native_buttons\":");c.at=tick;c.ms=GetTickCount64();}
    if(std::strcmp(event,"network_neutral")==0 && !std::strstr(detail,"input_timeout"))for(auto& c:candidates)c.buttons=0;
}
int main(int argc,char** argv) {
    const auto port=static_cast<std::uint16_t>(argc>1?std::atoi(argv[1]):28746);
    if(fopen_s(&journal,"host.jsonl","wx"))return 2;
    m12a::Session session(record,{6,150});if(!session.start(port))return 3;
    const auto start=Clock::now();auto ctrl=start;bool done=false,readyMarked=false;
    while(!done && Clock::now()-start<std::chrono::seconds(150)) {
        auto now=Clock::now();
        if(now>=ctrl){std::ifstream f("host_control.txt");std::string s;f>>s;done=s=="quit";ctrl=now+std::chrono::milliseconds(10);}
        const auto due=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now-start).count()*60/1000000000);
        if(due>tick) {
            tick=due;session.tick(tick,true);
            if(session.wantsPlayer()&&!readyMarked){std::ofstream f("ready.flag");f<<tick;readyMarked=true;}
            for(auto& c:candidates)if(c.buttons && c.policy.expired(tick-c.at,GetTickCount64()-c.ms)) {
                const auto dt=tick-c.at,dm=GetTickCount64()-c.ms;
                record("candidate_timeout","\"candidate_ticks\":%llu,\"candidate_ms\":%llu,\"elapsed_ticks\":%llu,\"elapsed_ms\":%llu,\"tick_trigger\":%s,\"wall_trigger\":%s",c.policy.ticks,c.policy.wallMs,dt,dm,dt>=c.policy.ticks?"true":"false",dm>=c.policy.wallMs?"true":"false");c.buttons=0;
            }
            record("state","\"buttons\":%u,\"generation\":%u,\"sequence\":%u,\"ready\":%s",session.nativeButtons(),session.generation(),session.sequence(),session.wantsPlayer()?"true":"false");
            std::fflush(journal);
        }
        Sleep(1);
    }
    session.stop("qa_host_stop");std::fclose(journal);journal=nullptr;return 0;
}
