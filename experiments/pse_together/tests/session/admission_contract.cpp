#include "network_input.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <stdexcept>
#include <vector>
using namespace pse;
static unsigned checks=0;
static void check(bool b,const char* name){++checks;std::printf("%s %s\n",b?"PASS":"FAIL",name);if(!b)throw std::runtime_error(name);}
static bool until(const std::function<bool()>& test,const std::function<void()>& step,int ms=1800){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{step();if(test())return true;}while(std::chrono::steady_clock::now()<end);return test();}
static void pump(const std::function<void()>& step,int ms){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{step();}while(std::chrono::steady_clock::now()<end);}
static ClientConfig config(uint16_t port,const std::string& id,uint64_t key=0){ClientConfig c;c.hostPort=port;c.participantId=id;c.joinToken="M12A_Loopback";c.reconnectKey=key;return c;}
static void record(const char* e,const char* fmt,...){std::printf("SESSION %s ",e);va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts("");}
static void session(){
    std::puts("CASE real_Session_repeated_rejections_and_input");m12a::Session s(record);check(s.start(28771),"Session start");uint64_t tick=0;
    ENetClientNode alpha;check(alpha.connect(config(28771,"alpha")),"alpha connect");
    auto first=[&]{alpha.poll(1);s.tick(++tick,true);Sleep(2);};
    check(until([&]{return alpha.isAuthenticated()&&s.wantsPlayer();},first),"alpha ready as first remote");
    const auto key=alpha.getReconnectKey();check(alpha.getAssignedSlot()==1&&key!=0,"alpha owns slot1 and secret");
    alpha.sendInputCommand(1,0,0,0,0,0);check(until([&]{return s.nativeButtons()==4;},first),"alpha input applied to Session");
    alpha.disconnect();auto idle=[&]{s.tick(++tick,true);Sleep(2);};check(until([&]{return !s.wantsPlayer()&&s.nativeButtons()==0;},idle),"leave clears Session input");
    for(unsigned i=0;i<8;++i){
        ENetClientNode beta;uint8_t rejected=0;beta.onRejectCallback=[&](const MsgReject& r){rejected=r.reasonCode;};
        check(beta.connect(config(28771,"beta_"+std::to_string(i),key)),"beta attempts with foreign ID, even with alpha key");
        auto step=[&]{beta.poll(1);s.tick(++tick,true);Sleep(2);};
        check(until([&]{return rejected && !beta.isConnected();},step),"rejected beta transport actually closes");
        check(rejected==REJECT_SERVER_FULL&&!beta.isAuthenticated()&&!s.wantsPlayer()&&s.generation()==i+1&&s.nativeButtons()==0,"beta not admitted; no stale Session/input");
        ENetClientNode forged;uint8_t bad=0;forged.onRejectCallback=[&](const MsgReject& r){bad=r.reasonCode;};
        check(forged.connect(config(28771,"alpha",key^1u)),"forged alpha key connect");
        auto badStep=[&]{forged.poll(1);s.tick(++tick,true);Sleep(2);};
        check(until([&]{return bad && !forged.isConnected();},badStep),"forged-key peer closes");
        check(bad==REJECT_HIJACK_ATTEMPT&&!forged.isAuthenticated()&&s.generation()==i+1,"forged key cannot mutate reserved generation");
        ENetClientNode rejoin;check(rejoin.connect(config(28771,"alpha",key)),"legitimate rejoin after beta/forged attempts");
        auto good=[&]{rejoin.poll(1);s.tick(++tick,true);Sleep(2);};
        check(until([&]{return rejoin.isAuthenticated()&&s.wantsPlayer();},good),"alpha ready again");
        check(rejoin.getAssignedSlot()==1&&rejoin.getConnectionGeneration()==i+2&&rejoin.getReconnectKey()==key&&s.nativeButtons()==0,"same reservation/key, exactly next generation, neutral input");
        std::printf("REJOIN_EVIDENCE cycle=%u beta_reject=%u forged_reject=%u alpha_slot=%u alpha_generation=%u neutral=%u\n",i,rejected,bad,rejoin.getAssignedSlot(),rejoin.getConnectionGeneration(),s.nativeButtons());
        rejoin.disconnect();check(until([&]{return !s.wantsPlayer();},idle),"rejoin leaves cleanly for next attempt");
    }
    s.stop("admission_test");check(s.bridge.areThreadChecksEnabled()&&s.bridge.getAffinityViolationCount()==0,"Session SPSC affinity enabled/zero");
}
static void ledger(bool limited,uint16_t port){
    std::printf("CASE ledger limited=%d\n",limited);ENetHostServer host;HostConfig hc;hc.port=port;hc.maxClients=1;hc.bridgeMode=true;hc.joinToken="M12A_Loopback";
    if(limited){hc.maxClientSlots=1;hc.rejectedPeerTimeoutMs=250;}
    unsigned auth=0,ended=0,custom=0;host.onClientAuthenticatedCallback=[&](ENetPeer*,uint8_t,uint32_t,bool){++auth;};host.onClientSessionEndedCallback=[&](uint8_t,uint32_t){++ended;};
    host.setCustomPacketHandler([&](ENetPeer*,uint8_t,ByteReader&,uint32_t){++custom;return true;});
    check(host.start(hc),"direct host starts on sole I/O owner");ENetClientNode a;check(a.connect(config(port,"alpha")),"ledger alpha connect");
    auto step=[&]{host.poll(1);a.poll(1);Sleep(1);};check(until([&]{return a.isAuthenticated();},step),"ledger alpha authenticated");
    auto key=a.getReconnectKey();a.disconnect();check(until([&]{return !host.getSimulation().isSlotActive(1);},[&]{host.poll(1);Sleep(1);}),"ledger alpha disconnected");
    const auto gen=host.getSimulation().getSlot(1).connectionGeneration;ENetClientNode b;uint8_t reject=0;b.onRejectCallback=[&](const MsgReject& r){reject=r.reasonCode;};check(b.connect(config(port,"beta")),"ledger beta connect");
    auto betaStep=[&]{host.poll(1);b.poll(1);Sleep(1);};
    if(!limited){check(until([&]{return b.isAuthenticated();},betaStep),"general default still admits other remote slots");check(b.getAssignedSlot()==2,"default has no permanent P2-only rule");b.disconnect();host.stop();return;}
    check(until([&]{return reject&&!b.isConnected();},betaStep),"restricted beta rejection delivered, peer closed");
    bool empty=true;for(uint8_t n=2;n<MAX_PLAYERS;++n){const auto& p=host.getSimulation().getSlot(n);empty &= p.participantId.empty()&&!p.connected&&p.peer==nullptr&&p.inboundCommandQueue.empty()&&p.buttons==0;}
    const auto& reserved=host.getSimulation().getSlot(1);
    check(empty&&reserved.participantId=="alpha"&&reserved.reconnectKey==key&&reserved.connectionGeneration==gen&&!reserved.connected&&reserved.peer==nullptr,"allocation rejection did not commit beta or touch alpha reservation");
    check(host.getConnectedClientCount()==0&&auth==1&&ended==1&&custom==0,"rejection creates no session callbacks/input/active ledger entry");
    std::printf("LEDGER_EVIDENCE reject=%u extra_slots_empty=%d auth_events=%u ended_events=%u custom_packets=%u reserved_generation=%u\n",reject,empty,auth,ended,custom,gen);
    host.stop();
}
static void stalled(){
    std::puts("CASE rejected_peer_without_ACK");ENetHostServer host;HostConfig hc;hc.port=28774;hc.maxClients=1;hc.maxClientSlots=1;hc.rejectedPeerTimeoutMs=250;hc.bridgeMode=true;hc.joinToken="M12A_Loopback";
    check(host.start(hc),"bounded-reject host start");ENetClientNode a;check(a.connect(config(28774,"alpha")),"expiry alpha connect");
    auto as=[&]{host.poll(1);a.poll(1);Sleep(1);};check(until([&]{return a.isAuthenticated();},as),"expiry alpha authenticated");const auto key=a.getReconnectKey();a.disconnect();
    check(until([&]{return !host.getSimulation().isSlotActive(1);},[&]{host.poll(1);Sleep(1);}),"expiry alpha left");
    ENetClientNode beta;check(beta.connect(config(28774,"beta_silent")),"silent beta transport starts");
    auto bs=[&]{host.poll(1);beta.poll(1);Sleep(1);};check(until([&]{return beta.isConnected();},bs),"silent beta sent HELLO");beta.poll(0);
    // Do not service this client again: it cannot ACK REJECT. Host deadline must
    // free the sole peer without needing beta.disconnect() or a new participant.
    pump([&]{host.poll(1);Sleep(1);},600);
    ENetClientNode rejoin;check(rejoin.connect(config(28774,"alpha",key)),"alpha reconnect while beta socket remains unserviced");
    auto good=[&]{host.poll(1);rejoin.poll(1);Sleep(1);};
    check(until([&]{return rejoin.isAuthenticated();},good),"deadline freed sole peer without beta ACK");
    check(rejoin.getAssignedSlot()==1&&rejoin.getConnectionGeneration()==2&&host.getSimulation().getSlot(2).participantId.empty(),"expired rejection left no registry reservation/orphan");
    beta.disconnect();pump(good,80);check(rejoin.isAuthenticated()&&host.getSimulation().getSlot(1).connected,"late rejected-peer cleanup cannot disconnect alpha generation2");
    std::printf("EXPIRY_EVIDENCE alpha_authenticated=%d slot=%u generation=%u ledger_connected=%d\n",rejoin.isAuthenticated(),rejoin.getAssignedSlot(),rejoin.getConnectionGeneration(),host.getSimulation().getSlot(1).connected);host.stop();
}
int main(){unsigned failures=0;try{session();}catch(const std::exception& e){++failures;std::printf("CASE FAILED %s\n",e.what());}
    for(bool limited:{true,false})try{ledger(limited,limited?28772:28773);}catch(const std::exception& e){++failures;std::printf("CASE FAILED %s\n",e.what());}
    try{stalled();}catch(const std::exception& e){++failures;std::printf("CASE FAILED %s\n",e.what());}
    std::printf("ADMISSION SUMMARY scenarios=4 failures=%u checks=%u; no KEX\n",failures,checks);return failures?1:0;}
