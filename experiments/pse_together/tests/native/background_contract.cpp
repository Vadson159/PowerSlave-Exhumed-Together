// Policy / restoration and actual Session+ENet lifecycle. No KEX runtime claims.
#include "background_policy.hpp"
#include "network_input.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
static unsigned checks{};
static void check(bool ok,const char* name) { ++checks; std::printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)throw std::runtime_error(name); }
static bool until(const std::function<bool()>& predicate,const std::function<void()>& step) {
    auto deadline=Clock::now()+std::chrono::seconds(3);
    do { step(); if(predicate())return true; } while(Clock::now()<deadline); return predicate();
}
static void logger(const char* e,const char* fmt,...) { std::printf("SESSION %s ",e); va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts(""); }
static background::Conditions ready() { return {true,true,true,true,true,false,false,false,true,true}; }
static void policy() {
    const auto c=ready(); check(background::eligible(c),"READY background host allowed without P2 existence");
    for (auto field : {&background::Conditions::simulationThread,&background::Conditions::running,&background::Conditions::ready,
        &background::Conditions::gameplay,&background::Conditions::stableWorld,&background::Conditions::focusKnown,&background::Conditions::focusLost}) {
        auto x=c; x.*field=false; check(!background::eligible(x),"each required condition independently fail closed");
    }
    for (auto field : {&background::Conditions::menu,&background::Conditions::stopping,&background::Conditions::failed}) {
        auto x=c; x.*field=true; check(!background::eligible(x),"manual menu / teardown / failure prevent override");
    }
    background::FocusLease lease; unsigned char focus=1;
    check(!lease.acquire(focus,1,false)&&focus==1,"singleplayer/server-no-ready retains native focus pause");
    check(lease.acquire(focus,1,true)&&focus==0,"only native focus byte leased for StateTick");
    check(!lease.acquire(focus,1,true),"nested lease refused");
    check(lease.release(focus,1)&&focus==1&&!lease.applied,"normal Tick restores pause");
    check(!lease.release(focus,1)&&focus==1,"release idempotent");
    check(lease.acquire(focus,2,true),"second legitimate tick lease");
    focus=0;check(!lease.release(focus,3)&&focus==0,"focus gain during Tick remains owner of new value");
    focus=1;check(lease.acquire(focus,4,true),"third legitimate tick lease");
    focus=1;check(!lease.release(focus,5)&&focus==1,"new focus loss during Tick preserved");
    focus=2;check(!lease.acquire(focus,6,true)&&focus==2,"unknown byte never rewritten");
    focus=0;check(!lease.acquire(focus,6,true),"foreground native state never overridden");
}
static void session() {
    m12a::Session s(logger);check(s.start(28853),"real host worker started");
    std::uint64_t tick{};
    auto allowed=[&] { auto c=ready(); c.running=s.running();c.ready=s.wantsPlayer();return background::eligible(c); };
    auto idle=[&] { s.tick(++tick,true);Sleep(2); };
    check(!allowed(),"listening alone does not activate simulation override");
    pse::ClientConfig cfg;cfg.hostPort=28853;cfg.joinToken="M12A_Loopback";cfg.participantId="background_contract";cfg.bridgeMode=true;
    pse::ENetClientNode a;check(a.connect(cfg),"client begins connecting");check(!allowed(),"transport connecting does not activate override");
    auto step=[&] { a.poll(1);idle(); };
    check(until([&] { return a.isAuthenticated()&&s.generation()==1; },step),"authenticated generation1 consumed by Session");
    check(!s.wantsPlayer()&&!allowed(),"authenticated without MAP_READY remains paused");
    const auto key=a.getReconnectKey();
    auto sendReady=[](pse::ENetClientNode& client) { pse::MsgMapReady r;r.slotId=client.getAssignedSlot();r.mapEpoch=1;client.sendPacket(pse::CHANNEL_RELIABLE_CONTROL,pse::encodePacket(pse::MSG_MAP_READY,10,r),true); };
    sendReady(a);check(until([&]{return allowed();},step),"READY activates without native P2; no circular spawn gate");
    a.disconnect();check(until([&]{return !s.wantsPlayer();},idle)&&!allowed(),"last disconnect returns to native pause policy");
    pse::ENetClientNode b;cfg.reconnectKey=key;check(b.connect(cfg),"same participant reconnects");
    auto rejoin=[&]{b.poll(1);idle();};
    check(until([&]{return b.isAuthenticated()&&s.generation()==2;},rejoin),"new authenticated generation2");
    check(!allowed(),"old READY not inherited by generation2");
    sendReady(b);check(until([&]{return allowed();},rejoin),"new matching READY activates generation2");
    auto c=ready();c.gameplay=false;check(!background::eligible(c),"loading/world-exit cannot activate lease");
    s.stop("background_contract");check(!allowed()&&!s.worker.isRunning(),"shutdown stops override with actual joined worker");
    b.disconnect();check(s.bridge.getAffinityViolationCount()==0,"actual Session bridge affinity violations zero");
}
int main() { try { policy();session();std::printf("BACKGROUND SUMMARY checks=%u failures=0; KEX runtime scenarios deferred\n",checks);return 0; }
    catch(const std::exception& e){std::printf("BACKGROUND FAIL checks=%u reason=%s\n",checks,e.what());return 1;} }
