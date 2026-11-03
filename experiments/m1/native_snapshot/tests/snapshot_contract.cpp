// Actual codec, guard, Session, SPSC and loopback ENet Worker tests. No KEX mock
// is used to claim native acceptance; that requires the separate two-KEX run.
#include "../src/snapshot_contract.hpp"
#include "network_input.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace pse;
using Clock=std::chrono::steady_clock;
static unsigned checks{},failures{};
static void check(bool ok,const char* name) { ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';} }
static void logger(const char*,const char*,...) {}
template<class Work,class Pred> static bool until(Work work,Pred pred,unsigned ms=3000) {
    const auto end=Clock::now()+std::chrono::milliseconds(ms);
    do { work();if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1)); }while(Clock::now()<end);
    return pred();
}
static BridgePlayerSnapshot sample(uint8_t slot,uint32_t seq,uint32_t epoch,uint32_t connection=1) {
    BridgePlayerSnapshot s;s.slotId=slot;s.entityId=21760+slot;s.entityGeneration=7+slot;
    s.sequence=seq;s.serverTick=100+seq;s.mapEpoch=epoch;s.connectionGeneration=connection;
    s.posX=-7420.125f+slot;s.posY=-6775.5f;s.posZ=1152;s.yaw=2.156752f;s.pitch=.125f;
    return s;
}
static void codecAndGuard() {
    auto s=sample(0,1,734923);auto p=s.toNet();std::vector<uint8_t> record;ByteWriter w(record);p.serialize(w);check(record.size()==65,"65 wire bytes per player");
    MsgPlayerSnapshot m;m.snapshotSequence=s.sequence;m.serverTick=s.serverTick;m.mapEpoch=s.mapEpoch;m.players={p};
    auto packet=encodePacket(MSG_PLAYER_SNAPSHOT,s.sequence,m);
    check(packet.size()==89 && packet[6]==0x20,"89 bytes and opcode20");
    ByteReader r(packet.data(),packet.size());MsgHeader h;MsgPlayerSnapshot decoded;
    check(h.deserialize(r) && h.version==0x0110 && decoded.deserialize(r) && !r.remaining(),"v1.10 framed roundtrip");
    auto dto=BridgePlayerSnapshot::fromNet(decoded.players[0],decoded.snapshotSequence,decoded.serverTick,decoded.mapEpoch,9);
    check(dto.mapEpoch==734923 && dto.connectionGeneration==9 && dto.entityGeneration==7,"epoch and distinct entity/connection generations");
    check(snapshot55::bits(dto.posX)==snapshot55::bits(s.posX) && snapshot55::bits(dto.pitch)==snapshot55::bits(s.pitch),"exact float bits");
    m.players.push_back(sample(1,2,s.mapEpoch).toNet());check(encodePacket(MSG_PLAYER_SNAPSHOT,1,m).size()==154,"154 bytes for two records");
    snapshot55::Guard g;g.reset(1,s.mapEpoch);
    check(g.accept(s,false)==snapshot55::Verdict::NotReady,"before READY rejected");
    check(g.accept(s,true)==snapshot55::Verdict::Accept,"slot0 accepted");
    auto b=sample(1,2,s.mapEpoch);check(g.accept(b,true)==snapshot55::Verdict::Accept,"slot1 not suppressed by slot0");
    check(g.accept(s,true)==snapshot55::Verdict::Stale,"duplicate rejected");
    b.sequence=1;check(g.accept(b,true)==snapshot55::Verdict::Stale,"older packet rejected");
    s.sequence=3;s.mapEpoch++;check(g.accept(s,true)==snapshot55::Verdict::Identity,"wrong map rejected");s.mapEpoch--;
    s.connectionGeneration++;check(g.accept(s,true)==snapshot55::Verdict::Identity,"wrong connection rejected");s.connectionGeneration--;
    s.entityGeneration++;check(g.accept(s,true)==snapshot55::Verdict::EntityMismatch,"unexplained entity generation rejected");s.entityGeneration--;
    s.posX=std::numeric_limits<float>::quiet_NaN();check(g.accept(s,true)==snapshot55::Verdict::Nonfinite,"nonfinite rejected");s.posX=0;
    s.serverTick=1;check(g.accept(s,true)==snapshot55::Verdict::Stale,"tick regression rejected");
    g.reset(2,734924);s=sample(0,1,734924,2);
    check(g.accept(s,true)==snapshot55::Verdict::Accept && s.entityGeneration!=s.connectionGeneration,"new connection resets sequence without relabeling entity generation");
}
static void workerRoundTrip() {
    constexpr uint32_t epoch=913743;
    m12a::Session host(logger);check(host.configureNativeSnapshots(epoch),"configure nonconstant authoritative epoch");
    check(host.start(28855),"real Host Worker start");
    ThreadBridge cb;cb.registerSimulationThread();ENetWorker client(cb,ENetWorkerMode::CLIENT);
    client.setClientInputCadence(true);client.setNativePlayerSnapshots(true);
    std::atomic<DWORD> observedTid{};std::atomic<unsigned> badObserverBytes{};
    client.setPlayerSnapshotObserver([&](bool,const BridgePlayerSnapshot&,size_t bytes){observedTid=GetCurrentThreadId();if(bytes!=89)++badObserverBytes;});
    ClientConfig config;config.hostPort=28855;config.joinToken="M12A_Loopback";config.participantId="doc55_contract";
    check(client.startClient(config),"real Client Worker start");
    uint64_t tick{};bool ready{},baseline{};uint32_t generation{};std::vector<BridgePlayerSnapshot> received;
    auto pump=[&] {
        host.tick(++tick,true);BridgeMessage msg;
        while(cb.pollInbound(msg)) {
            if(auto* e=std::get_if<BridgeSessionEvent>(&msg);e && e->type==BridgeSessionEvent::Type::CLIENT_JOINED) {
                generation=e->connectionGeneration;
                check(e->mapEpoch==epoch && e->mapName==snapshot55::Domain,"WELCOME agrees authoritative epoch/domain");
                BridgeMapReady mr;mr.slotId=e->slotId;mr.connectionGeneration=generation;mr.mapEpoch=epoch;
                ready=isSuccess(cb.pushOutbound(BridgeMessage{mr}));
            } else if(auto* b=std::get_if<BridgeWorldBaseline>(&msg))baseline=b->mapEpoch==epoch && b->connectionGeneration==generation;
            else if(auto* s=std::get_if<BridgePlayerSnapshot>(&msg))received.push_back(*s);
        }
    };
    check(until(pump,[&]{return ready && baseline && host.wantsPlayer();}),"matching READY and empty diagnostic baseline");
    check(host.publishNativeSnapshot(sample(0,1,epoch)) && host.publishNativeSnapshot(sample(1,2,epoch)),"two independent scalar snapshots queued");
    check(until(pump,[&]{return received.size()>=2;}),"two actual UDP snapshots delivered to SIMULATION");
    snapshot55::Guard guard;guard.reset(generation,epoch);
    bool slots[2]{};
    for(const auto& s:received)if(s.slotId<2) {
        slots[s.slotId]=guard.accept(s,baseline)==snapshot55::Verdict::Accept;
        check(s.entityGeneration==7u+s.slotId && s.connectionGeneration==generation && s.mapEpoch==epoch,"metadata survives Worker path");
        check(snapshot55::bits(s.posX)==snapshot55::bits(sample(s.slotId,s.sequence,epoch).posX),"UDP float correlation");
    }
    check(slots[0] && slots[1],"both slot sequence streams accepted");
    auto stale=sample(0,50,epoch,generation+1);stale.targetSlot=1;
    check(isSuccess(host.bridge.pushOutbound(BridgeMessage{stale})),"stale recipient-generation fixture queued by SIMULATION");
    auto wrongEpoch=sample(0,51,epoch+1,generation);wrongEpoch.targetSlot=1;
    check(isSuccess(host.bridge.pushOutbound(BridgeMessage{wrongEpoch})),"wrong epoch fixture queued");
    host.publishNativeSnapshot(sample(0,52,epoch));host.publishNativeSnapshot(sample(1,53,epoch));
    check(until(pump,[&]{return received.size()>=4;}),"valid packet advances after invalid route/epoch");
    bool staleSeen=false;for(const auto& s:received)if(s.sequence==50 || s.sequence==51)staleSeen=true;
    check(!staleSeen && host.worker.getStats().outboundNoPeer>=1 && host.worker.getStats().bestEffortDropped>=1,"old recipient/epoch not restamped or delivered");
    check(observedTid && observedTid!=GetCurrentThreadId(),"observer and network dispatch on separate NETWORK thread");
    check(!badObserverBytes,"observed real packets are89 bytes");
    client.stop();host.stop("DOC55_contract");
    check(!cb.getAffinityViolationCount() && !host.bridge.getAffinityViolationCount(),"strict SPSC affinity preserved");cb.unregisterSimulationThread();
}
static void malformedPackets() {
    constexpr uint32_t epoch=173456;
    ENetHostServer host;HostConfig hc;hc.bindAddress="127.0.0.1";hc.port=28856;hc.bridgeMode=true;
    check(host.start(hc),"raw-wire fixture Host");host.setSessionMap(snapshot55::Domain,epoch);
    ThreadBridge cb;cb.registerSimulationThread();ENetWorker client(cb,ENetWorkerMode::CLIENT);client.setNativePlayerSnapshots(true);
    ClientConfig cc;cc.hostPort=28856;cc.participantId="doc55_framing";check(client.startClient(cc),"framing Client Worker");
    bool joined=false;std::vector<BridgePlayerSnapshot> received;
    auto pump=[&]{host.poll(0);BridgeMessage msg;while(cb.pollInbound(msg)) {
        if(auto* e=std::get_if<BridgeSessionEvent>(&msg);e && e->type==BridgeSessionEvent::Type::CLIENT_JOINED)joined=true;
        if(auto* s=std::get_if<BridgePlayerSnapshot>(&msg))received.push_back(*s);
    }};
    check(until(pump,[&]{return joined;}),"framing peer authenticated");
    MsgPlayerSnapshot m;m.snapshotSequence=30;m.serverTick=100;m.mapEpoch=epoch;m.players={sample(0,30,epoch).toNet()};
    const auto encoded=encodePacket(MSG_PLAYER_SNAPSHOT,30,m);
    auto send=[&](std::vector<uint8_t> bytes,uint8_t channel=CHANNEL_UNRELIABLE_SEQUENCED){host.broadcastPacket(channel,bytes,false);};
    auto trailing=encoded;trailing.push_back(0);send(trailing);
    auto truncated=encoded;truncated.pop_back();send(truncated);
    auto version=encoded;version[4]=0;version[5]=1;send(version);
    send(encoded,CHANNEL_WORLD_SNAPSHOT);
    auto nan=encoded;nan[33]=0;nan[34]=0;nan[35]=0xc0;nan[36]=0x7f;send(nan);
    m.mapEpoch=epoch+1;send(encodePacket(MSG_PLAYER_SNAPSHOT,30,m));m.mapEpoch=epoch;
    m.players[0].entityGeneration=0;send(encodePacket(MSG_PLAYER_SNAPSHOT,30,m));
    const auto deadline=Clock::now()+std::chrono::milliseconds(250);
    until(pump,[&]{return Clock::now()>=deadline;},500);
    check(received.empty(),"trailing/truncated/version/channel/NaN/epoch/generation wire rejection");
    m.snapshotSequence=31;m.players={sample(0,31,epoch).toNet(),sample(1,31,epoch).toNet()};send(encodePacket(MSG_PLAYER_SNAPSHOT,31,m));
    check(until(pump,[&]{return received.size()==2;}),"existing two-record wire format dispatches both slots");
    snapshot55::Guard guard;guard.reset(1,epoch);bool valid=received.size()==2;
    for(const auto& s:received)valid=guard.accept(s,true)==snapshot55::Verdict::Accept && valid;
    check(valid,"same batch sequence accepted independently per slot");
    client.stop();host.stop();cb.unregisterSimulationThread();
}
int main() { codecAndGuard();workerRoundTrip();malformedPackets();std::cout<<"SNAPSHOT SUMMARY checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0; }
