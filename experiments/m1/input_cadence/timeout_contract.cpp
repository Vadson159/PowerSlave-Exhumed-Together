// Tests the actual Session, Worker and ENet, with all three candidate policies.
#include "network_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
static int failures{},expired{};static const char* lastReason="";
static void check(bool yes,const char* name){std::printf("%s %s\n",yes?"PASS":"FAIL",name);if(!yes)++failures;}
static void record(const char* event,const char* fmt,...) {
    char b[1800]{};va_list a;va_start(a,fmt);vsnprintf_s(b,sizeof(b),_TRUNCATE,fmt,a);va_end(a);
    if(std::strcmp(event,"network_input_expired")==0)++expired;
    if(std::strcmp(event,"network_neutral")==0){if(std::strstr(b,"input_timeout"))lastReason="timeout";else if(std::strstr(b,"disconnect"))lastReason="disconnect";else lastReason="other";}
    if(std::strcmp(event,"network_input_accepted")!=0)std::printf("SESSION %s %s\n",event,b);
}
int main(){
 for(std::uint64_t threshold:{6u,8u,10u}){
    std::printf("POLICY %llu /150\n",threshold);m12a::Session s(record,{threshold,150});
    const auto port=static_cast<std::uint16_t>(28810+threshold);check(s.start(port),"host starts");
    pse::ENetClientNode c;pse::ClientConfig cfg;cfg.hostPort=port;cfg.joinToken="M12A_Loopback";cfg.participantId="policy49";
    check(c.connect(cfg),"client starts");std::uint64_t tick=0;
    auto step=[&](bool game=true){c.poll(0);s.tick(++tick,game);Sleep(2);};
    auto until=[&](auto predicate){for(int i=0;i<400&&!predicate();++i)step();check(predicate(),"condition delivered before bound");};
    until([&]{return s.wantsPlayer();});
    auto hold=[&]{c.sendInputCommand(1,0,0,0,0,0);until([&]{return s.nativeButtons()==4;});};
    hold();const auto at=tick;while(tick<at+threshold-1)step();check(s.nativeButtons()==4,"hold before tick threshold");step();check(!s.nativeButtons()&&tick==at+threshold,"silence/lost-state expires at exact tick threshold");
    hold();const auto before=expired;c.sendInputCommand(0,0,0,0,0,0);until([&]{return s.nativeButtons()==0;});check(expired==before,"delivered release bypasses timeout");
    hold();Sleep(175);step();check(s.nativeButtons()==0&&tick-s.acceptedAt()<threshold,"wall backstop with few ticks");
    hold();step(false);check(s.nativeButtons()==0,"level not running neutralizes immediately");
    hold();cfg.reconnectKey=c.getReconnectKey();const auto oldgen=s.generation();c.disconnect();until([&]{return !s.wantsPlayer();});check(!s.nativeButtons()&&std::strcmp(lastReason,"disconnect")==0,"disconnect holding W uses disconnect path");
    check(c.connect(cfg),"same identity reconnect starts");until([&]{return s.wantsPlayer();});check(s.generation()==oldgen+1&&!s.nativeButtons(),"new generation starts neutral");
    pse::MsgInputCommand stale;stale.slotId=1;stale.connectionGeneration=oldgen;stale.commandSequence=60000;stale.clientTick=60000;stale.moveForward=1;
    c.sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED,pse::encodePacket(pse::MSG_INPUT_COMMAND,60000,stale),false);
    for(int i=0;i<12;++i)step();check(!s.nativeButtons(),"stale generation hold rejected");hold();check(s.sequence()<60000,"stale high sequence did not poison new peer");
    const auto b=expired;s.stop("policy_contract");s.stop("idempotence");check(!s.nativeButtons()&&!s.worker.isRunning()&&expired==b,"stop immediately neutral, joined, idempotent");
    check(s.bridge.getAffinityViolationCount()==0,"SPSC affinity intact");c.disconnect();
 }
 std::printf("TIMEOUT_CONTRACT failures=%d\n",failures);return failures?1:0;
}
