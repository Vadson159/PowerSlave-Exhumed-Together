// Actual background Worker + Session + pristine ENet. No private access.
#include "network_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <thread>
static int failures{},checks{},accepted{};static uint32_t lastAccepted{};
static void check(bool b,const char* s){++checks;std::printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++failures;}
static void record(const char* e,const char* fmt,...){std::printf("SESSION %s ",e);va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts("");if(std::string(e)=="network_input_accepted")++accepted;}
int main(int argc,char**) {
    const bool before=argc>1;m12a::Session s(record);check(s.start(28851),"host starts");
    pse::ENetClientNode c;pse::ClientConfig cfg;cfg.hostPort=28851;cfg.joinToken="M12A_Loopback";cfg.participantId="sequence_live50";
    check(c.connect(cfg),"connect starts");uint64_t tick{};
    auto step=[&]{c.poll(0);s.tick(++tick,true);Sleep(1);};
    auto until=[&](auto p){for(int i=0;i<1500&&!p();++i)step();check(p(),"condition before bound");};
    until([&]{return s.wantsPlayer();});
    auto send=[&](uint32_t q,uint32_t gen=0){pse::MsgInputCommand cmd;cmd.slotId=c.getAssignedSlot();cmd.connectionGeneration=gen?gen:c.getConnectionGeneration();cmd.commandSequence=q;cmd.clientTick=q;cmd.moveForward=1;c.sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED,pse::encodePacket(pse::MSG_INPUT_COMMAND,q,cmd),false);};
    send(1);until([&]{return s.sequence()==1;});
    send(UINT32_MAX);for(int i=0;i<60;++i)step();send(2);for(int i=0;i<60;++i)step();
    auto st=s.worker.getStats();std::printf("LIVE_REPRO slot=%u generation=%u seq=%u received=%llu stale=%llu foreign=%llu connected=%d ready=%d\n",c.getAssignedSlot(),s.generation(),s.sequence(),st.inputsReceived,st.inputsRejectedStale,st.inputsRejectedForeign,c.isAuthenticated(),s.wantsPlayer());
    if(before) check(s.sequence()==UINT32_MAX&&st.inputsReceived==2&&st.inputsRejectedStale==1,"before valid2 blocked by MAX");
    else {
        check(s.sequence()==2&&st.inputsReceived==2,"after MAX rejected and valid2 accepted");
        // 60Hz grid for 12 seconds, deterministic upstream omissions, variable
        // delayed payload sends, duplicate/reordered application sequences.
        // ENet throttle/channel policies remain untouched. No queue saturation.
        const auto begin=std::chrono::steady_clock::now();int deliveredWindows=0;uint32_t q=2;
        struct Delayed {int due;uint32_t seq,gen;};std::vector<Delayed> delayed;
        int dropped=0,delays=0,duplicates=0;
        for(int i=1;i<=720;++i){++q;
            for(auto it=delayed.begin();it!=delayed.end();) {
                if(it->due<=i){send(it->seq,it->gen);it=delayed.erase(it);}else ++it;
            }
            if(i%11!=0 && !(i%180>=100&&i%180<105)) {
                if(i%13==0){delayed.push_back({i+2+i%3,q,c.getConnectionGeneration()});++delays;}
                else send(q);
                if(i%17==0){send(q);++duplicates;}if(i%29==0&&q>3)send(q-2);
            }else ++dropped;
            if(i==360){
                cfg.reconnectKey=c.getReconnectKey();const auto gen=s.generation();c.disconnect();until([&]{return !s.wantsPlayer();});check(c.connect(cfg),"authenticated reconnect");until([&]{return s.wantsPlayer()&&s.generation()==gen+1;});
                send(UINT32_MAX,gen);for(int n=0;n<25;++n)step();send(q);until([&]{return s.sequence()==q;});
            }
            const auto due=begin+std::chrono::microseconds(int64_t(i)*1000000/60);
            while(std::chrono::steady_clock::now()<due)step();
            if(i%60==0){const auto now=s.sequence();std::printf("STRESS_WINDOW i=%d gen=%u sent_seq=%u session_seq=%u accepted=%d\n",i,s.generation(),q,now,accepted);if(now>lastAccepted){++deliveredWindows;lastAccepted=now;}}
        }
        send(q+1);until([&]{return s.sequence()==q+1;});
        std::printf("IMPAIR generated=720 omitted=%d delayed_2_to_4_slots=%d duplicates=%d pending_payloads=%zu\n",dropped,delays,duplicates,delayed.size());
        st=s.worker.getStats();check(deliveredWindows==12&&accepted>400,"long valid stream survives poison/loss/duplicates/reconnect");
        check(st.pendingEscalations==0&&st.pendingPeak==0&&s.bridge.getAffinityViolationCount()==0&&c.isAuthenticated()&&s.wantsPlayer(),"no overflow manufactured, no kick/resync, affinity intact");
        std::printf("LIVE_STRESS accepted=%d received=%llu stale=%llu foreign=%llu coalesced=%llu pending_peak=%llu gen=%u seq=%u\n",accepted,st.inputsReceived,st.inputsRejectedStale,st.inputsRejectedForeign,st.inputsCoalesced,st.pendingPeak,s.generation(),s.sequence());
    }
    s.stop("sequence_live");c.disconnect();std::printf("LIVE checks=%d failures=%d before=%d\n",checks,failures,before);return failures?1:0;
}
