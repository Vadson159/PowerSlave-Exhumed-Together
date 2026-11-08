// DOC-46: real ENet receipt + controlled SPSC handoff. No KEX/DLL/game calls.
#include "network_input.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
using namespace pse;
static unsigned checks=0;
static void check(bool ok,const char* name) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",name); ++checks;
    if(!ok) throw std::runtime_error(name);
}
static bool until(const std::function<bool()>& pred,const std::function<void()>& step,int ms=2500) {
    auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do { step(); if(pred()) return true; } while(std::chrono::steady_clock::now()<end);
    return pred();
}
static void pump(const std::function<void()>& step,int ms=100) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do { step(); } while(std::chrono::steady_clock::now()<end);
}
static ClientConfig config(uint16_t port,const char* id,uint64_t key=0) {
    ClientConfig c; c.hostPort=port; c.participantId=id; c.joinToken="M12A_Loopback"; c.reconnectKey=key; c.bridgeMode=true; return c;
}
struct Host {
    ThreadBridge bridge;
    ENetWorker worker{bridge,ENetWorkerMode::HOST};
    std::vector<BridgeMessage> received;
    explicit Host(uint16_t port) {
        bridge.registerSimulationThread(); bridge.setMaxBackpressureRetries(0);
        HostConfig hc; hc.port=port; hc.maxClients=1; hc.joinToken="M12A_Loopback";
        check(bridge.areThreadChecksEnabled(),"host real thread checks enabled");
        check(worker.startHost(hc),"host start");
    }
    void drain() { BridgeMessage m; while(bridge.pollInbound(m)) received.push_back(std::move(m)); }
    bool session(BridgeSessionEvent::Type type,uint32_t gen) const {
        for(const auto& m:received) if(const auto* e=std::get_if<BridgeSessionEvent>(&m)) if(e->type==type && e->slotId==1 && e->connectionGeneration==gen) return true;
        return false;
    }
    void send(BridgeMessage m) { check(isSuccess(bridge.pushOutbound(std::move(m))),"critical outbound accepted (owner retains on rejection)"); }
    ~Host() { worker.stop(); bridge.unregisterSimulationThread(); }
};
struct Receipts {
    std::vector<std::string> maps;
    std::vector<uint32_t> baselines;
    bool malformed=false;
    void attach(ENetClientNode& c) {
        c.setCustomPacketHandler([this](uint8_t type,ByteReader& r,uint32_t) {
            if(type==MSG_MAP_CHANGE) { MsgMapChange m; if(!m.deserialize(r)) malformed=true; else { maps.push_back(m.mapName); std::printf("WIRE_RECEIVED MAP_CHANGE name=%s epoch=%u\n",m.mapName.c_str(),m.newMapEpoch); } return true; }
            if(type==MSG_WORLD_BASELINE) { MsgWorldBaseline b; if(!b.deserialize(r)) malformed=true; else { baselines.push_back(b.serverTick); std::printf("WIRE_RECEIVED BASELINE marker=%u epoch=%u\n",b.serverTick,b.mapEpoch); } return true; }
            return false;
        });
    }
};
static BridgeMapChange map(uint32_t gen,const char* name,uint8_t slot=1) {
    BridgeMapChange m; m.targetSlot=slot; m.connectionGeneration=gen; m.mapName=name; return m;
}
static BridgeWorldBaseline baseline(uint32_t gen,uint32_t marker,uint8_t slot=1) {
    BridgeWorldBaseline b; b.targetSlot=slot; b.connectionGeneration=gen; b.serverTick=marker; b.mapName="ROUTING_TEST"; return b;
}
static BridgeControl kick(uint32_t gen) {
    BridgeControl k; k.slotId=1; k.connectionGeneration=gen; k.reasonCode=LEAVE_KICKED; return k;
}
static void unicast() {
    std::puts("CASE host_unicast"); Host host(28763);
    ENetClientNode a,b; Receipts receipts; receipts.attach(b);
    check(a.connect(config(28763,"routing_peer")),"gen1 connect");
    auto sa=[&] { a.poll(1); host.drain(); Sleep(1); };
    check(until([&]{return a.isAuthenticated() && host.session(BridgeSessionEvent::Type::CLIENT_JOINED,1);},sa),"authenticated gen1 observed by host");
    const auto key=a.getReconnectKey();
    host.worker.setOutboundDrainPaused(true);
    host.send(map(1,"STALE_GEN1")); host.send(baseline(1,111)); host.send(kick(1));
    host.send(map(0,"UNSTAMPED")); host.send(baseline(0,0)); host.send(kick(0));
    a.disconnect();
    check(until([&]{return host.session(BridgeSessionEvent::Type::CLIENT_LEFT,1);},[&]{host.drain();Sleep(1);}),"gen1 left");
    check(b.connect(config(28763,"routing_peer",key)),"gen2 connect with same participant/key");
    auto sb=[&] { b.poll(1); host.drain(); Sleep(1); };
    check(until([&]{return b.isAuthenticated() && b.getConnectionGeneration()==2 && host.session(BridgeSessionEvent::Type::RECONNECTED,2);},sb),"authenticated gen2 before outbound drain");
    host.send(map(2,"CURRENT_GEN2")); host.send(baseline(2,222));
    // Broadcast deliberately has an old generation; it is not a unicast target.
    host.send(map(1,"BROADCAST",BridgeMapChange::BROADCAST));
    host.send(baseline(1,333,BridgeWorldBaseline::BROADCAST));
    host.worker.setOutboundDrainPaused(false);
    check(until([&]{return receipts.maps.size()>=2 && receipts.baselines.size()>=2 && host.worker.getStats().outboundDrained>=10;},sb),"current and broadcast packets decoded by gen2");
    pump(sb);
    check(!receipts.malformed,"decoded packets valid v1.10");
    check(receipts.maps==std::vector<std::string>{"CURRENT_GEN2","BROADCAST"},"stale/unstamped MapChange absent; current+broadcast FIFO exact");
    check(receipts.baselines==std::vector<uint32_t>{222,333},"stale/unstamped Baseline absent; current+broadcast FIFO exact");
    check(b.isConnected() && b.isAuthenticated(),"KICK gen1 and gen0 do not disconnect gen2");
    check(host.worker.getStats().outboundNoPeer==6,"six stale/unstamped unicast actions rejected by worker");
    std::printf("UNICAST_EVIDENCE client_gen=%u connected=%d authenticated=%d rejected=%llu maps=%zu baselines=%zu\n",b.getConnectionGeneration(),b.isConnected(),b.isAuthenticated(),host.worker.getStats().outboundNoPeer,receipts.maps.size(),receipts.baselines.size());
    host.send(kick(2));
    check(until([&]{return !b.isConnected() && host.session(BridgeSessionEvent::Type::CLIENT_LEFT,2);},sb),"KICK current gen2 still disconnects actual peer");
    std::printf("CURRENT_KICK_EVIDENCE connected=%d authenticated=%d host_left_gen2=%d\n",b.isConnected(),b.isAuthenticated(),host.session(BridgeSessionEvent::Type::CLIENT_LEFT,2));
    check(host.bridge.getAffinityViolationCount()==0,"unicast affinity violations zero");
}
static void pendingReady() {
    std::puts("CASE authenticated_ready_pending_FIFO"); Host host(28764);
    ENetClientNode a,b;
    check(a.connect(config(28764,"pending_peer")),"pending gen1 connect");
    auto sa=[&] { a.poll(1); host.drain(); Sleep(1); };
    check(until([&]{return a.isAuthenticated() && host.session(BridgeSessionEvent::Type::CLIENT_JOINED,1);},sa),"pending gen1 authenticated");
    host.received.clear(); const auto key=a.getReconnectKey();
    for(uint32_t n=1;n<=140;++n) {
        MsgMapReady r; r.slotId=1; r.mapEpoch=n;
        a.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_MAP_READY,n,r),true);
    }
    check(until([&]{return host.worker.getStats().pendingCurrent>=12 && host.worker.getStats().packetsReceived>=140;},[&]{a.poll(1);Sleep(1);}),"all 140 reliable MAP_READY received, inbound full and pending retained");
    a.disconnect();
    // Keep simulation paused while worker receives leave and the new WELCOME.
    Sleep(30);
    check(b.connect(config(28764,"pending_peer",key)),"pending gen2 reconnect");
    check(until([&]{return b.isAuthenticated() && b.getConnectionGeneration()==2;},[&]{b.poll(1);Sleep(1);}),"gen2 authenticated while gen1 events remain pending");
    MsgMapReady current; current.slotId=1; current.mapEpoch=200;
    b.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_MAP_READY,200,current),true);
    auto sb=[&]{b.poll(1);host.drain();Sleep(1);};
    check(until([&]{return !host.received.empty() && std::holds_alternative<BridgeMapReady>(host.received.back()) && std::get<BridgeMapReady>(host.received.back()).mapEpoch==200 && host.worker.getStats().pendingCurrent==0;},sb),"all pending criticals reach simulation");
    std::vector<uint32_t> epochs; bool identities=true; int oldLast=-1,left=-1,rejoined=-1,newReady=-1;
    for(size_t i=0;i<host.received.size();++i) {
        const auto& m=host.received[i];
        if(const auto* r=std::get_if<BridgeMapReady>(&m)) {
            epochs.push_back(r->mapEpoch);
            if(r->mapEpoch<=140) {identities &= r->connectionGeneration==1; oldLast=static_cast<int>(i);}
            else {identities &= r->connectionGeneration==2; newReady=static_cast<int>(i);}
        } else if(const auto* e=std::get_if<BridgeSessionEvent>(&m)) {
            if(e->type==BridgeSessionEvent::Type::CLIENT_LEFT && e->connectionGeneration==1) left=static_cast<int>(i);
            if(e->type==BridgeSessionEvent::Type::RECONNECTED && e->connectionGeneration==2) rejoined=static_cast<int>(i);
        }
    }
    std::vector<uint32_t> expected; for(uint32_t n=1;n<=140;++n) expected.push_back(n); expected.push_back(200);
    check(epochs==expected,"141 received reliable MAP_READY in exact wire order");
    check(identities,"MAP_READY stamped from authenticated sending peer, not current drain generation");
    check(oldLast<left && left<rejoined && rejoined<newReady,"pending lifecycle FIFO preserved across reconnect");
    std::printf("PENDING_EVIDENCE ready_count=%zu old_last_index=%d left_index=%d rejoined_index=%d current_ready_index=%d deferred=%llu pending=%llu stamped_identities_match=%d\n",epochs.size(),oldLast,left,rejoined,newReady,host.worker.getStats().criticalDeferred,host.worker.getStats().pendingCurrent,identities);
    check(host.worker.getStats().criticalDeferred>0 && !host.bridge.isResyncRequired(),"pending retry with no declared loss/resync");
    check(host.bridge.getAffinityViolationCount()==0,"pending FIFO affinity violations zero");
}
static void clientReady() {
    std::puts("CASE stale_client_outbound_ready"); Host host(28765);
    ThreadBridge cb; cb.registerSimulationThread(); ENetWorker worker(cb,ENetWorkerMode::CLIENT);
    ClientConfig cc=config(28765,"client_worker_peer");
    check(worker.startClient(cc),"client worker gen1 start");
    uint64_t key=0; uint32_t generation=0;
    auto step=[&] {
        host.drain(); BridgeMessage m;
        while(cb.pollInbound(m)) if(const auto* e=std::get_if<BridgeSessionEvent>(&m)) if(e->reconnectKey) {key=e->reconnectKey; generation=e->connectionGeneration;}
        Sleep(1);
    };
    check(until([&]{return generation==1 && key!=0;},step),"client gen1 WELCOME via bridge");
    worker.setOutboundDrainPaused(true);
    BridgeMapReady old; old.slotId=1; old.mapEpoch=1; old.connectionGeneration=1;
    check(isSuccess(cb.pushOutbound(BridgeMessage(old))),"old ready queued by gen1 owner");
    worker.stop();
    check(until([&]{return host.session(BridgeSessionEvent::Type::CLIENT_LEFT,1);},step),"client gen1 worker stopped and peer left");
    cc.reconnectKey=key; generation=0;
    check(worker.startClient(cc),"client gen2 start on same bridge (old ready retained)");
    check(until([&]{return generation==2;},step),"client gen2 WELCOME before old outbound drain");
    worker.setOutboundDrainPaused(false);
    check(until([&]{return worker.getStats().outboundNoPeer==1;},step),"old client ready rejected before wire send");
    BridgeMapReady current=old; current.connectionGeneration=2; current.mapEpoch=2;
    check(isSuccess(cb.pushOutbound(BridgeMessage(current))),"current client ready queued");
    check(until([&]{for(const auto& m:host.received) if(const auto* r=std::get_if<BridgeMapReady>(&m)) if(r->mapEpoch==2 && r->connectionGeneration==2) return true;return false;},step),"current ready delivered and peer-stamped gen2");
    pump(step);
    size_t readyCount=0; for(const auto& m:host.received) if(std::holds_alternative<BridgeMapReady>(m)) ++readyCount;
    check(readyCount==1,"stale client ready never laundered into gen2 wire session");
    std::printf("CLIENT_READY_EVIDENCE current_generation=%u host_ready_count=%zu stale_rejected=%llu\n",generation,readyCount,worker.getStats().outboundNoPeer);
    worker.stop(); cb.unregisterSimulationThread();
    check(cb.getAffinityViolationCount()==0 && host.bridge.getAffinityViolationCount()==0,"worker restart and ready routing affinity zero");
}
static void record(const char* event,const char* fmt,...) {
    std::printf("SESSION %s ",event); va_list a; va_start(a,fmt); std::vprintf(fmt,a); va_end(a); std::puts("");
}
static void sessionReady() {
    std::puts("CASE Session_ready_and_outbox_owner_generation"); m12a::Session s(record);
    check(s.start(28766),"Session starts without KEX");
    s.worker.stop(); // deliberate owner handoff, join before taking NETWORK role
    s.bridge.registerNetworkThread();
    std::vector<BridgeMessage> output;
    auto take=[&]{output.clear();BridgeMessage m;while(s.bridge.pollOutbound(m))output.push_back(std::move(m));}; take();
    uint64_t tick=0;
    auto deliver=[&](BridgeMessage m){check(isSuccess(s.bridge.pushInbound(std::move(m))),"single NETWORK fixture push after worker join");s.tick(++tick,true);};
    auto joined=[](uint32_t gen){BridgeSessionEvent e;e.slotId=1;e.connectionGeneration=gen;e.type=gen==1?BridgeSessionEvent::Type::CLIENT_JOINED:BridgeSessionEvent::Type::RECONNECTED;return e;};
    while(s.bridge.getOutboundOccupancy()<s.bridge.getOutboundCapacity()) check(isSuccess(s.bridge.pushOutbound(BridgeMessage(BridgeSessionEvent{}))),"pressure filler accepted by sole SIMULATION producer");
    deliver(joined(1)); deliver(joined(2));
    check(!s.wantsPlayer() && s.generation()==2 && s.bridge.getMetrics().outbound.criticalOverflowCount>0,"two MapChange events retained in Session outbox while bridge full");
    take(); s.tick(++tick,true); take();
    check(output.size()==2 && std::get<BridgeMapChange>(output[0]).connectionGeneration==1 && std::get<BridgeMapChange>(output[1]).connectionGeneration==2,"retry preserves FIFO and captured generations; no late retagging");
    std::printf("OUTBOX_EVIDENCE current_generation=%u deferred_map_generations=[%u,%u] critical_rejections=%llu\n",s.generation(),std::get<BridgeMapChange>(output[0]).connectionGeneration,std::get<BridgeMapChange>(output[1]).connectionGeneration,s.bridge.getMetrics().outbound.criticalOverflowCount);
    BridgeMapReady old; old.slotId=1; old.mapEpoch=1; old.connectionGeneration=1;
    deliver(old); take();
    check(!s.wantsPlayer() && output.empty(),"old generation MAP_READY cannot set new Session ready or emit baseline");
    BridgeRespawnRequest rr; rr.slotId=1; rr.connectionGeneration=1;
    BridgeInteractionRequest ir; ir.slotId=1; ir.connectionGeneration=1;
    deliver(rr); take(); check(output.empty(),"stale refused respawn cannot create gen2 KICK");
    deliver(ir); take(); check(output.empty(),"stale refused interaction cannot create gen2 KICK");
    BridgeMapReady valid=old; valid.connectionGeneration=2;
    deliver(valid); take();
    check(s.wantsPlayer() && output.size()==1 && std::get<BridgeWorldBaseline>(output[0]).connectionGeneration==2,"current MAP_READY sets ready and baseline captures gen2");
    s.bridge.requestResync(RESYNC_EXTERNAL,"routing test recover gen2"); s.tick(++tick,true); take();
    check(output.size()==1 && std::get<BridgeControl>(output[0]).connectionGeneration==2,"recovery KICK captures gen2 when created");
    const auto saved=std::get<BridgeControl>(output[0]);
    deliver(joined(3)); take();
    check(saved.connectionGeneration==2 && std::get<BridgeMapChange>(output.at(0)).connectionGeneration==3,"new Session generation cannot rewrite an owned old KICK");
    rr.connectionGeneration=3; deliver(rr); take();
    check(output.size()==1 && std::get<BridgeControl>(output[0]).connectionGeneration==3,"current refused request still KICKs its own generation (no respawn implementation)");
    s.bridge.unregisterNetworkThread(); s.stop("routing_test");
    check(s.bridge.getAffinityViolationCount()==0 && !s.bridge.isResyncRequired(),"quiescent handoff, terminal cleanup and affinity valid");
}
static void wire() {
    std::puts("CASE internal_only_fields");
    auto a=map(1,"SAME");auto b=a;b.connectionGeneration=2;
    check(encodePacket(MSG_MAP_CHANGE,0,a.toNet())==encodePacket(MSG_MAP_CHANGE,0,b.toNet()),"MapChange generation not serialized");
    auto c=baseline(1,42);auto d=c;d.connectionGeneration=2;
    check(encodePacket(MSG_WORLD_BASELINE,0,c.toNet())==encodePacket(MSG_WORLD_BASELINE,0,d.toNet()),"Baseline generation not serialized");
    BridgeMapReady e;e.slotId=1;e.connectionGeneration=1;auto f=e;f.connectionGeneration=2;
    const auto bytes=encodePacket(MSG_MAP_READY,0,e.toNet());
    check(bytes==encodePacket(MSG_MAP_READY,0,f.toNet()) && bytes.size()==16,"MAP_READY remains exact v1.10 header11 + slot1 + epoch4");
}
int main() {
    unsigned failed=0;
    for(const auto test:{unicast,pendingReady,clientReady,sessionReady,wire}) {
        try{test();}catch(const std::exception& e){++failed;std::printf("CASE FAILED: %s\n",e.what());}
    }
    std::printf("ROUTING SUMMARY cases=%u failed=%u checks=%u; no KEX/game; affinity enforced\n",5u,failed,checks);
    return failed?1:0;
}
