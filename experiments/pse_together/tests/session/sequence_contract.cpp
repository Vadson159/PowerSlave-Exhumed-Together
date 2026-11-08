// White-box inspection is restricted to this test TU. All dependencies are
// included before the access override. No live background owner in this fixture.
#include "thread_bridge.h"
#include "enet_host.h"
#include "enet_client.h"
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <mutex>
#define private public
#include "network_input.hpp"
#undef private
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <limits>
// MSVC decorates member access into symbol names. Compile these unchanged
// implementation files in the inspection TU as well; the threaded live test
// links the normal libraries without this access override.
#include "enet_worker.cpp"
#include "network_input.cpp"
#if __has_include("input_sequence.h")
#include "input_sequence.h"
#define HARDENED 1
#endif
using namespace pse;
static int failures{},checks{};
static void check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
static void record(const char* e,const char* fmt,...){std::printf("SESSION %s ",e);va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts("");}
struct Fixture {
    m12a::Session s{record}; ENetClientNode c; ClientConfig cfg; std::uint64_t tick{};
    Fixture() {
        // Use actual Host/Worker callbacks and SPSC; run the network pump on
        // this test thread so mailbox observations have no data races.
        s.bridge.registerSimulationThread();s.bridge.registerNetworkThread();s.started=true;
        HostConfig hc;hc.bindAddress="127.0.0.1";hc.port=28850;hc.maxClients=1;hc.maxClientSlots=1;hc.bridgeMode=true;hc.joinToken="M12A_Loopback";
        s.worker.m_hostServer=std::make_unique<ENetHostServer>();s.worker.setupHostCallbacks();
        s.worker.m_hostServer->setCustomPacketHandler([&](ENetPeer* p,uint8_t t,ByteReader& r,uint32_t q){return s.worker.handleHostCustomPacket(p,t,r,q);});
        check(s.worker.m_hostServer->start(hc),"fixture host start");s.worker.m_hostServer->setSessionMap("M12A_INPUT_ONLY",1);
        cfg.hostPort=hc.port;cfg.joinToken=hc.joinToken;cfg.participantId="sequence50";
        check(c.connect(cfg),"fixture connect");until([&]{return s.wantsPlayer();});
        check(c.isAuthenticated()&&c.getAssignedSlot()==1&&s.generation()==1,"actual authenticated participant slot1/gen1");
    }
    void step(){c.poll(0);s.worker.drainOutboundQueue();s.worker.m_hostServer->poll(0);s.worker.flushInboundPending();s.worker.flushInputMailboxes();s.tick(++tick,true);Sleep(1);}
    template<class P> void until(P p){for(int i=0;i<1500&&!p();++i)step();check(p(),"condition before bound");}
    void send(uint32_t q,uint32_t gen=0,uint8_t slot=1,uint32_t buttons=0) {
        MsgInputCommand cmd;cmd.slotId=slot;cmd.connectionGeneration=gen?gen:s.generation();cmd.commandSequence=q;cmd.clientTick=q;cmd.moveForward=1;cmd.buttons=buttons;
        const auto n=s.worker.getStats().packetsReceived;
        // Reliable delivery for the deterministic acceptance matrix only. The
        // live stress executable uses ordinary unreliable transport unchanged.
        c.sendPacket(CHANNEL_UNRELIABLE_SEQUENCED,encodePacket(MSG_INPUT_COMMAND,q,cmd),true);
        until([&]{return s.worker.getStats().packetsReceived>n;});step();
        snapshot(q,gen?gen:s.generation(),slot);
    }
    void snapshot(uint32_t q,uint32_t gen,uint8_t slot){const auto& m=s.worker.m_inputs[1];auto st=s.worker.getStats();
        std::printf("STATE sent_slot=%u sent_gen=%u sent_seq=%u mailbox_gen=%u hasSeq=%d lastSeq=%u latest_seq=%u dirty=%d lastButtons=%u edges=%u attack=%u use=%u session_seq=%u received=%llu stale=%llu foreign=%llu",
          slot,gen,q,m.generation,m.hasSeq,m.lastSeq,m.latest.sequence,m.dirty,m.lastButtons,m.pressedEdges,m.attackPresses,m.usePresses,s.sequence(),st.inputsReceived,st.inputsRejectedStale,st.inputsRejectedForeign);
#ifdef HARDENED
        std::printf(" generation=%llu jump=%llu",st.inputsRejectedGeneration,st.inputsRejectedJump);
#endif
        std::puts("");
    }
    void reconnect(){cfg.reconnectKey=c.getReconnectKey();const auto gen=s.generation();c.disconnect();until([&]{return !s.wantsPlayer();});check(c.connect(cfg),"reconnect connect");until([&]{return s.wantsPlayer()&&s.generation()==gen+1;});check(!s.hasSeq&&!s.worker.m_inputs[1].hasSeq,"generation resets Worker and Session sequence");}
    ~Fixture(){c.disconnect();for(int i=0;i<4;++i)step();s.worker.m_hostServer->stop();s.worker.m_hostServer.reset();s.stop("contract");s.bridge.unregisterNetworkThread();}
};
int main(int argc,char**) {
    const bool before=argc>1;
    {Fixture f;auto& w=f.s.worker;auto& m=w.m_inputs[1];
    if(before){f.send(1);f.send(UINT32_MAX,0,1,BUTTON_ATTACK);f.send(2);check(m.lastSeq==UINT32_MAX&&m.latest.sequence==UINT32_MAX&&f.s.sequence()==UINT32_MAX,"before MAX poisons mailbox and Session");check(w.getStats().inputsReceived==2&&w.getStats().inputsRejectedStale==1,"before normal2 rejected stale");}
#ifdef HARDENED
    else {
        f.send(UINT32_MAX);check(!m.hasSeq&&m.lastSeq==0&&!m.dirty&&m.lastButtons==0&&m.latest.sequence==0,"initial MAX changes no mailbox fields");
        f.send(0);check(m.hasSeq&&m.lastSeq==0&&f.s.hasSeq,"initial zero accepted once");
        f.send(0);f.send(1);f.send(2);f.send(2);f.send(1);
        check(m.lastSeq==2&&f.s.sequence()==2&&w.getStats().inputsRejectedStale==3,"duplicates and reordering rejected");
        const auto old=m;f.send(UINT32_MAX,0,1,BUTTON_ATTACK|BUTTON_USE);
        check(m.lastSeq==old.lastSeq&&m.latest.sequence==old.latest.sequence&&m.lastButtons==old.lastButtons&&m.pressedEdges==old.pressedEdges&&m.attackPresses==old.attackPresses&&m.usePresses==old.usePresses&&m.dirty==old.dirty,"MAX changes no mailbox or edges");
        f.send(3);check(m.lastSeq==3&&f.s.sequence()==3,"normal after MAX accepted same generation");
        f.send(33);f.send(1833);check(m.lastSeq==1833,"moderate loss and exact jump boundary accepted");
        f.send(3634);check(m.lastSeq==1833,"jump boundary plus one rejected");f.send(1834);
        auto st=w.getStats();f.send(UINT32_MAX,f.s.generation(),2);f.send(UINT32_MAX,f.s.generation()+1);
        auto end=w.getStats();check(end.inputsRejectedForeign==st.inputsRejectedForeign+1&&end.inputsRejectedGeneration==st.inputsRejectedGeneration+1&&end.inputsRejectedJump==st.inputsRejectedJump&&end.inputsRejectedStale==st.inputsRejectedStale,"foreign and generation counters exclusive before sequence");
        const auto gen=f.s.generation();f.reconnect();f.send(UINT32_MAX,gen);check(!m.hasSeq,"extreme old generation cannot seed new mailbox");
        f.send(10801);check(!m.hasSeq,"initial boundary plus one rejected");f.send(10800);check(m.lastSeq==10800&&f.s.sequence()==10800,"continued sender reconnect initial10800 accepted");
        f.reconnect();f.send(1);check(m.lastSeq==1&&f.s.sequence()==1,"fresh reconnect accepts low1");
        // Arithmetic edge fixture: a legitimate prior high-water mark is seeded
        // in owned test objects, not obtained by billions of actual commands.
        m.lastSeq=UINT32_MAX-2;f.s.lastSeq=UINT32_MAX-2;
        f.send(UINT32_MAX-1);f.send(UINT32_MAX);f.send(0);f.send(1);f.send(UINT32_MAX-1);
        check(m.lastSeq==UINT32_MAX&&f.s.sequence()==UINT32_MAX,"nonmodular boundary accepts up to MAX; wrapped/old rejected");
        f.reconnect();f.send(0);check(m.lastSeq==0&&f.s.hasSeq,"new generation recovers from exhaustion");
        // Dirty mailbox: do not flush, verify every pending edge survives reject.
        auto* peer=w.m_hostServer->getPeerForSlot(1);
        auto raw=[&](uint32_t q,uint32_t b){MsgInputCommand c;c.slotId=1;c.connectionGeneration=f.s.gen;c.commandSequence=q;c.buttons=b;std::vector<uint8_t> bytes;ByteWriter bw(bytes);c.serialize(bw);ByteReader br(bytes.data(),bytes.size());w.handleHostInput(peer,br);};
        // Simulate a long simulation pause by accumulating 1901 validated
        // commands in the mailbox, with zero bridge queue pressure/overflow.
        for(uint32_t q=1;q<=1901;++q)raw(q,0);
        check(m.lastSeq==1901&&f.s.sequence()==0,"Worker validated chain while Session paused");
        w.flushInputMailboxes();f.s.tick(++f.tick,true);
        check(f.s.sequence()==1901,"Session accepts coalesced gap above per-packet limit");
        BridgeInputCommand cmd;cmd.slotId=1;cmd.connectionGeneration=f.s.gen-1;cmd.sequence=UINT32_MAX;cmd.moveForward=1;
        BridgeMessage msg(cmd);const auto at=f.s.acceptedAt();f.s.consume(msg);
        check(f.s.sequence()==1901&&f.s.acceptedAt()==at,"Session generation check before state");
        cmd.connectionGeneration=f.s.gen;cmd.sequence=1;msg=cmd;f.s.consume(msg);
        check(f.s.sequence()==1901&&f.s.acceptedAt()==at,"Session stale check before timestamps");
        raw(1902,BUTTON_ATTACK);const auto dirty=m;raw(UINT32_MAX,BUTTON_USE);
        check(m.dirty&&m.lastSeq==dirty.lastSeq&&m.latest.sequence==dirty.latest.sequence&&m.lastButtons==dirty.lastButtons&&m.pressedEdges==dirty.pressedEdges&&m.attackPresses==dirty.attackPresses&&m.usePresses==dirty.usePresses,"dirty pending mailbox unchanged on huge");
        f.snapshot(UINT32_MAX,f.s.gen,1);
        check(f.c.isAuthenticated()&&f.s.wantsPlayer()&&f.s.bridge.getAffinityViolationCount()==0,"invalid inputs never kick/resync or break affinity");
        w.resetStats();st=w.getStats();check(st.inputsRejectedJump==0&&st.inputsRejectedGeneration==0&&st.inputsRejectedStale==0&&st.inputsRejectedForeign==0,"all rejection counters reset");
    }
#endif
    }
#ifdef HARDENED
    // Independent reference using uint64, covering every distance around both
    // limits at many high water marks including both sides of 2^31 and MAX.
    if(!before){for(uint32_t last:{0u,1u,1799u,1800u,10799u,10800u,0x7fffffffu,0x80000000u,UINT32_MAX-1800,UINT32_MAX-1,UINT32_MAX}){
      for(bool has:{false,true})for(uint32_t next:{0u,1u,last,static_cast<uint32_t>(last+1u),static_cast<uint32_t>(last+1800u),static_cast<uint32_t>(last+1801u),10800u,10801u,UINT32_MAX-1,UINT32_MAX}){
        const auto result=validateInputSequence(has,last,next);
        const bool expected=has ? (uint64_t(next)>last&&uint64_t(next)-last<=1800) : next<=10800;
        check((result==InputSequenceResult::ACCEPT)==expected,"reference arithmetic matrix");
      }
    }}
#endif
    std::printf("CONTRACT checks=%d failures=%d before=%d\n",checks,failures,before);return failures?1:0;
}
