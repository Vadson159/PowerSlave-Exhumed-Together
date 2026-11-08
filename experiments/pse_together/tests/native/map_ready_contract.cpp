// DOC56 offline contract: identity derivation, reader offsets on a synthetic image, Host/Client
// gates and the production Session over real loopback ENet Workers. No KEX process is involved:
// nothing here is native runtime acceptance.
#include "map_reader.hpp"
#include "snapshot_contract.hpp"
#include "network_input.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace pse;
using Clock=std::chrono::steady_clock;
using map56::Fault;
static unsigned checks{},failures{};
static void check(bool ok,const char* name) { ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';} }
struct Entry { std::string event,detail;DWORD tid; };
static std::mutex logMutex;static std::vector<Entry> journal;
static void logger(const char* event,const char* fmt,...) {
    char detail[2048]{};va_list a;va_start(a,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,a);va_end(a);
    std::lock_guard<std::mutex> lock(logMutex);journal.push_back({event,detail,GetCurrentThreadId()});
}
static unsigned logged(const char* event,const char* needle="") {
    std::lock_guard<std::mutex> lock(logMutex);unsigned n{};
    for(const auto& e:journal)if(e.event==event && e.detail.find(needle)!=std::string::npos)++n;
    return n;
}
static bool loggedOffThread(const char* prefix) {
    std::lock_guard<std::mutex> lock(logMutex);
    for(const auto& e:journal)if(e.event.rfind(prefix,0)==0 && e.tid!=GetCurrentThreadId())return true;
    return false;
}
template<class Work,class Pred> static bool until(Work work,Pred pred,unsigned ms=3000) {
    const auto end=Clock::now()+std::chrono::milliseconds(ms);
    do { work();if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1)); }while(Clock::now()<end);
    return pred();
}
static std::uint64_t steadyNs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }

static map56::Observation observation(const char* name,const char* key=nullptr) {
    map56::Observation o;o.state=3;o.next=3;o.pending=0;o.nameReadable=true;
    o.nameLength=std::strlen(name);o.nameCapacity=64;strncpy_s(o.name.data(),o.name.size(),name,_TRUNCATE);
    o.mapInfo=0x7000;o.keyReadable=true;strncpy_s(o.key.data(),o.key.size(),key?key:name,_TRUNCATE);
    o.levelVtable=true;o.version=6;o.countA=1205;o.countB=331;return o;
}
static map56::Identity identity(const char* name) { map56::Identity id;check(map56::derive(observation(name),id)==Fault::None,"fixture identity");return id; }

static void derivation() {
    map56::Identity id;
    check(map56::derive(observation("maps/TOMB.MAP"),id)==Fault::None && id.name=="maps/TOMB.MAP" && id.countA==1205,"stable state3/pending0 named level is an identity");
    check(map56::derive(observation("maps/TOMB.MAP","MAPS/tomb.map"),id)==Fault::None,"mapInfo key matches like native case-insensitive compare");
    auto o=observation("maps/TOMB.MAP");o.state=4;check(map56::derive(o,id)==Fault::NotGameplay && id.name.empty(),"loading state refused");
    o=observation("maps/TOMB.MAP");o.pending=4;check(map56::derive(o,id)==Fault::Transition,"`map` command pending state4 refused");
    o=observation("maps/TOMB.MAP");o.next=1;check(map56::derive(o,id)==Fault::Transition,"StateExit next state refused");
    o=observation("maps/TOMB.MAP");o.nameReadable=false;check(map56::derive(o,id)==Fault::NameUnreadable,"SEH/bounds refusal");
    o=observation("maps/TOMB.MAP");o.nameCapacity=o.nameLength;check(map56::derive(o,id)==Fault::NameInvalid,"capacity must exceed length");
    o=observation("maps/TOMB.MAP");o.name[o.nameLength]='X';check(map56::derive(o,id)==Fault::NameInvalid,"terminator required at length");
    o=observation("maps/TO\"MB.MAP");check(map56::derive(o,id)==Fault::NameInvalid,"quote refused");
    o=observation("");check(map56::derive(o,id)==Fault::NameInvalid,"empty name refused");
    o=observation("maps/ABCDEFGHIJKLMNOPQRSTUVWX.MAP");check(o.nameLength==33 && map56::derive(o,id)==Fault::NameTooLongForWire,"33-byte name refused, never truncated on wire");
    o=observation("maps/ABCDEFGHIJKLMNOPQRSTUVW.MAP");check(o.nameLength==32 && map56::derive(o,id)==Fault::None,"32-byte name exact wire maximum");
    o=observation("maps/TOMB.MAP");o.mapInfo=0;check(map56::derive(o,id)==Fault::NoMapInfo,"no mapInfo entry refused");
    o=observation("maps/TOMB.MAP","maps/KARNAK.MAP");check(map56::derive(o,id)==Fault::MapInfoKeyMismatch,"stale mapInfo entry refused");
    o=observation("maps/TOMB.MAP");o.levelVtable=false;check(map56::derive(o,id)==Fault::LevelVtable,"level object identity refused");
    o=observation("maps/TOMB.MAP");o.countA=0;check(map56::derive(o,id)==Fault::LevelUnloaded,"Unload zeroed counts refused");
    const auto e1=map56::nextEpoch(0,0),e2=map56::nextEpoch(1,0),e3=map56::nextEpoch(77,77);
    check(e1>1 && e2>1 && e3>1 && e3!=77,"epoch never 0, legacy 1 or previous");
    const auto r1=map56::randomEpoch(0),r2=map56::randomEpoch(r1);check(r1>1 && r2>1 && r2!=r1,"BCrypt random epochs");
}
// Synthetic image with the static layout: proves the adapter offsets/bounds, not KEX runtime.
static void reader() {
    std::vector<unsigned char> image(map56::GameInstance+0x400);unsigned char* base=image.data();
    static_assert(map56::LevelObject+0x210<map56::GameInstance);
    auto put=[&](std::size_t off,auto v){std::memcpy(base+off,&v,sizeof(v));};
    char name[32]="maps/TOMB.MAP";char key[32]="MAPS/TOMB.MAP";
    unsigned char entry[0x80]{};const char* keyPtr=key;std::memcpy(entry+map56::MapInfoKey,&keyPtr,sizeof(keyPtr));
    const auto gi=map56::GameInstance;
    put(gi+0x24,3);put(gi+0x20,3);put(gi+0x28,0);
    put(gi+map56::MapNameString,static_cast<const char*>(name));put(gi+map56::MapNameString+8,std::uint64_t{13});put(gi+map56::MapNameString+16,std::uint64_t{32});
    put(gi+map56::MapInfoEntry,reinterpret_cast<std::uintptr_t>(entry));
    put(map56::LevelObject,static_cast<const unsigned char*>(base+map56::LevelVtable));
    image[map56::LevelObject+map56::LevelVersion]=6;put(map56::LevelObject+map56::LevelCountA,std::uint64_t{1205});put(map56::LevelObject+map56::LevelCountB,std::uint64_t{331});
    map56::Identity id;check(map56::derive(map56::observe(base),id)==Fault::None && id.name=="maps/TOMB.MAP" && id.countB==331,"reader: kexStr +1e0/+1e8/+1f0, mapInfo +1f8/+50, level counts");
    put(gi+map56::MapNameString,reinterpret_cast<const char*>(std::uintptr_t{0x10}));
    check(map56::derive(map56::observe(base),id)==Fault::NameUnreadable,"reader: invalid string pointer is an SEH refusal, not a crash");
    put(gi+map56::MapNameString,static_cast<const char*>(name));put(gi+map56::MapNameString+8,std::uint64_t{4096});
    check(map56::derive(map56::observe(base),id)==Fault::NameUnreadable,"reader: oversize length never copied");
    put(gi+map56::MapNameString+8,std::uint64_t{13});put(map56::LevelObject,static_cast<const unsigned char*>(base+1));
    check(map56::derive(map56::observe(base),id)==Fault::LevelVtable,"reader: wrong level vtable");
    put(map56::LevelObject,static_cast<const unsigned char*>(base+map56::LevelVtable));put(gi+0x28,4);
    check(map56::derive(map56::observe(base),id)==Fault::Transition,"reader: pending map change");
}
static void hostGate() {
    using R=map56::HostGate::Ready;map56::HostGate g;const auto tomb=identity("maps/TOMB.MAP");
    check(!g.bind(tomb,0) && !g.bind(tomb,1) && g.bind(tomb,913743) && !g.bind(tomb,913744),"one non-constant binding");
    check(g.mapReady(1,1,913743,true)==R::NoPeer,"MapReady without authenticated peer");
    g.authenticated(1,1);
    check(g.mapReady(1,1,913743,false)==R::LevelNotReady,"MapReady before host level readiness");
    check(g.mapReady(2,1,913743,true)==R::ForeignSlot,"foreign slot");
    check(g.mapReady(1,0,913743,true)==R::StaleGeneration,"old connection generation");
    check(g.mapReady(1,1,913742,true)==R::StaleEpoch,"stale epoch");
    check(!g.snapshotEligible(1,true),"no eligibility before acceptance");
    check(g.mapReady(1,1,913743,true)==R::Accepted && g.snapshotEligible(1,true),"current peer accepted, eligible");
    check(g.mapReady(1,1,913743,true)==R::Duplicate && g.snapshotEligible(1,true),"duplicate not re-promoted");
    check(!g.snapshotEligible(1,false) && !g.snapshotEligible(2,true),"eligibility needs level and generation");
    g.authenticated(1,2);check(!g.snapshotEligible(2,true) && g.mapReady(1,1,913743,true)==R::StaleGeneration,"reconnect: old queued MapReady stale");
    check(g.mapReady(1,2,913743,true)==R::Accepted,"reconnect generation accepted on same world");
    check(g.verify(Fault::None,tomb),"unchanged identity stays verified");
    auto changed=tomb;changed.countA++;
    check(!g.verify(Fault::None,changed) && g.world()==map56::HostGate::World::Invalidated && !g.snapshotEligible(2,true),"identity change invalidates");
    check(g.mapReady(1,2,913743,true)==R::WorldNotVerified,"invalidated world never re-promotes");
    map56::HostGate u;check(u.bind(tomb,55) && !u.verify(Fault::LevelUnloaded,{}),"level unload invalidates");
    map56::HostGate d;d.bind(tomb,56);d.authenticated(1,3);d.left(3);check(d.mapReady(1,3,56,true)==R::NoPeer,"disconnect during handshake");
}
static void clientGate() {
    using S=map56::ClientGate::State;const auto tomb=identity("maps/TOMB.MAP");
    map56::ClientGate g;
    check(!g.welcome(1,1,1,"maps/TOMB.MAP") && !g.welcome(1,0,9,"maps/TOMB.MAP") && !g.welcome(0,1,9,"maps/TOMB.MAP") && !g.welcome(1,1,9,""),"welcome rejects legacy epoch1/gen0/host slot/empty map");
    g.observe(Fault::None,tomb);check(g.state()==S::Disconnected && !g.shouldSendReady(),"no READY before authenticated session");
    check(g.welcome(1,1,734001,"maps/TOMB.MAP") && g.state()==S::AwaitNativeMap,"authenticated host identity received");
    for(unsigned i=1;i<map56::StableTicks;++i)g.observe(Fault::None,tomb);
    check(!g.shouldSendReady(),"no READY before StableTicks");
    g.observe(Fault::Transition,{});check(g.stableTicks()==0 && g.state()==S::AwaitNativeMap,"pre-READY transition resets stability only");
    for(unsigned i=0;i<map56::StableTicks;++i)g.observe(Fault::None,tomb);
    check(g.shouldSendReady() && g.state()==S::IdentityVerified,"READY after StableTicks matching ticks");
    g.readyQueued();check(!g.snapshotEligible(1,734001),"no snapshot before baseline");
    check(!g.baseline(1,734002,"maps/TOMB.MAP") && g.baseline(1,734001,"maps/TOMB.MAP") && g.snapshotEligible(1,734001),"baseline completes contract");
    check(!g.snapshotEligible(1,734000) && !g.snapshotEligible(2,734001),"snapshot of other epoch/connection");
    g.observe(Fault::Transition,{});check(g.state()==S::Invalidated && !g.snapshotEligible(1,734001),"loading after READY invalidates");
    map56::ClientGate u;u.observe(Fault::None,tomb);u.welcome(1,1,9,"maps/TOMB.MAP");for(unsigned i=0;i<map56::StableTicks;++i)u.observe(Fault::None,tomb);u.readyQueued();u.baseline(1,9,"maps/TOMB.MAP");
    u.observe(Fault::LevelUnloaded,{});check(u.state()==S::Invalidated,"level unloaded after READY");
    map56::ClientGate m;m.observe(Fault::None,identity("maps/KARNAK.MAP"));m.welcome(1,1,9,"maps/TOMB.MAP");
    for(unsigned i=0;i<2*map56::StableTicks;++i)m.observe(Fault::None,identity("maps/KARNAK.MAP"));
    check(m.state()==S::Mismatch && !m.shouldSendReady() && !m.welcome(1,2,9,"maps/KARNAK.MAP"),"different map ids: terminal, no READY, no relabel");
    map56::ClientGate c;c.observe(Fault::None,tomb);c.welcome(1,1,9,"maps/TOMB.MAP");
    check(c.mapChange(2,10,"maps/KARNAK.MAP") && c.state()==S::AwaitNativeMap,"other generation control ignored");
    check(!c.mapChange(1,10,"maps/KARNAK.MAP") && c.state()==S::Invalidated,"announced map change fails closed");
    map56::ClientGate r;r.observe(Fault::None,tomb);r.welcome(1,1,9,"maps/TOMB.MAP");r.lost();
    check(!r.welcome(1,1,9,"maps/TOMB.MAP") && r.welcome(1,2,9,"maps/TOMB.MAP") && r.stableTicks()==0,"reconnect needs newer generation and fresh stability");
    map56::ClientGate w;w.observe(Fault::None,tomb);w.observe(Fault::None,identity("maps/KARNAK.MAP"));check(w.state()==S::Invalidated,"locked local identity change invalidates");
}

// Real Host Session + real client Worker, the test thread acting as both SIMULATION owners.
struct Link {
    m12a::Session host{logger};
    ThreadBridge cb;
    ENetWorker client{cb,ENetWorkerMode::CLIENT};
    map56::ClientGate gate;snapshot55::Guard guard;
    map56::Identity hostId,localId;Fault hostFault{Fault::None},localFault{Fault::None};
    std::uint64_t tick{},ordinal{};bool hostLevel=true,autoReady=true,readyQueued{},baseline{},left{};
    std::uint32_t gen{},epoch{};std::string welcomeMap;
    std::vector<BridgePlayerSnapshot> received,accepted;
    std::uint16_t port;
    Link(std::uint16_t p,const char* hostMap,const char* localMap,std::uint32_t mapEpoch):hostId(identity(hostMap)),localId(identity(localMap)),port(p) {
        check(host.configureNativeMap(hostId,mapEpoch) && !host.configureNativeSnapshots(mapEpoch),"configure native map identity once");
        check(host.start(port),"real Host Worker");
        cb.registerSimulationThread();client.setClientInputCadence(true);client.setNativePlayerSnapshots(true);
        ClientConfig c;c.hostPort=port;c.joinToken="M12A_Loopback";c.participantId="doc56_contract";
        check(client.startClient(c),"real Client Worker");
        gate.observe(localFault,localId);
    }
    void sendReady(std::uint32_t g,std::uint32_t e) { BridgeMapReady r;r.slotId=1;r.connectionGeneration=g;r.mapEpoch=e;check(isSuccess(cb.pushOutbound(BridgeMessage{r})),"MapReady queued by client SIMULATION"); }
    void pump() {
        host.verifyNativeMap(hostFault,hostFault==Fault::None?hostId:map56::Identity{});
        host.tick(++tick,hostLevel);
        BridgeMessage m;
        while(cb.pollInbound(m)) {
            if(auto* e=std::get_if<BridgeSessionEvent>(&m)) {
                if(e->type==BridgeSessionEvent::Type::CLIENT_JOINED || e->type==BridgeSessionEvent::Type::RECONNECTED) {
                    if(gate.welcome(e->slotId,e->connectionGeneration,e->mapEpoch,e->mapName)) { gen=e->connectionGeneration;epoch=e->mapEpoch;welcomeMap=e->mapName;guard.reset(gen,epoch);readyQueued=baseline=false; }
                } else if(e->type==BridgeSessionEvent::Type::LOCAL_SESSION_LOST)gate.lost();
            } else if(auto* b=std::get_if<BridgeWorldBaseline>(&m))baseline=gate.baseline(b->connectionGeneration,b->mapEpoch,b->mapName);
            else if(auto* mc=std::get_if<BridgeMapChange>(&m))gate.mapChange(mc->connectionGeneration,mc->newMapEpoch,mc->mapName);
            else if(auto* s=std::get_if<BridgePlayerSnapshot>(&m)) {
                received.push_back(*s);
                if(guard.accept(*s,baseline && gate.snapshotEligible(s->connectionGeneration,s->mapEpoch))==snapshot55::Verdict::Accept)accepted.push_back(*s);
            }
        }
        gate.observe(localFault,localFault==Fault::None?localId:map56::Identity{});
        if(autoReady && !readyQueued && gate.shouldSendReady()) { sendReady(gen,epoch);gate.readyQueued();readyQueued=true; }
        if(baseline) { BridgeClientInputSample s;s.slotId=1;s.connectionGeneration=gen;s.sampleOrdinal=++ordinal;s.sampledSteadyNs=steadyNs();s.simulationTick=tick;s.valid=true;s.moveForward=1.0f;cb.pushOutbound(BridgeMessage{s}); }
    }
    void wait(unsigned ms) { const auto end=Clock::now()+std::chrono::milliseconds(ms);until([&]{pump();},[&]{return Clock::now()>=end;},ms+500); }
    void finish(const char* reason) { if(!left)client.stop();host.stop(reason);check(!cb.getAffinityViolationCount() && !host.bridge.getAffinityViolationCount(),"strict SPSC affinity");cb.unregisterSimulationThread(); }
    BridgePlayerSnapshot snap(std::uint8_t slot,std::uint32_t seq) const {
        BridgePlayerSnapshot s;s.slotId=slot;s.entityId=21760+slot;s.entityGeneration=1;s.sequence=seq;s.serverTick=200+seq;s.mapEpoch=epoch;s.connectionGeneration=gen;
        s.posX=-7420.0f;s.posY=-6775.0f;s.posZ=1152.0f;s.yaw=1.5f;s.pitch=0.0f;return s;
    }
};
static void positive() {
    const auto epoch=map56::randomEpoch(0);Link l(28861,"maps/TOMB.MAP","maps/TOMB.MAP",epoch);
    check(until([&]{l.pump();},[&]{return l.baseline && l.host.wantsPlayer();}),"stable matching identity → MapReady → accepted → baseline");
    check(l.welcomeMap=="maps/TOMB.MAP" && l.epoch==epoch && l.epoch!=1,"WELCOME carries native map name and non-constant epoch");
    check(logged("network_ready","\"map_ready_accepted\":true")==1 && logged("network_ready","client_attested_epoch_echo_v110")==1,"host accepted exactly once, recorded as epoch echo");
    check(l.host.snapshotEligible() && l.host.mapGate().peer()==map56::HostGate::Peer::MapReadyAccepted,"host snapshot eligibility only after acceptance");
    check(l.host.publishNativeSnapshot(l.snap(0,1)) && l.host.publishNativeSnapshot(l.snap(1,2)),"eligible host publishes");
    check(until([&]{l.pump();},[&]{return l.accepted.size()>=2;}),"client gate+guard accept both slots over UDP");
    check(until([&]{l.pump();},[&]{return l.host.nativeButtons()==4u;}),"existing ENet INPUT still reaches native buttons");
    check(!loggedOffThread("map_") && !loggedOffThread("network_ready"),"map/ready decisions only on SIMULATION thread");
    // Host-side unload after READY: terminal, immediate loss of P2/snapshot eligibility.
    l.hostFault=Fault::LevelUnloaded;l.pump();
    check(!l.host.snapshotEligible() && !l.host.wantsPlayer() && !l.host.publishNativeSnapshot(l.snap(0,3)),"host level unload revokes eligibility");
    check(logged("map_identity_invalidated","level_unloaded")==1,"invalidation journaled");
    l.hostFault=Fault::None;l.wait(80);check(!l.host.wantsPlayer() && l.host.mapGate().world()==map56::HostGate::World::Invalidated,"no re-promotion after identity returns");
    l.finish("DOC56_positive");
}
static void premature() {
    Link l(28862,"maps/TOMB.MAP","maps/TOMB.MAP",map56::randomEpoch(0));l.autoReady=false;
    check(until([&]{l.pump();},[&]{return l.gen!=0;}),"authenticated");
    l.hostLevel=false;l.sendReady(l.gen,l.epoch);
    check(until([&]{l.pump();},[&]{return logged("map_ready_rejected","level_not_ready")==1;}),"MapReady before host native readiness rejected");
    l.hostLevel=true;l.sendReady(l.gen,l.epoch+1);
    check(until([&]{l.pump();},[&]{return logged("map_ready_rejected","stale_epoch")==1;}),"stale epoch MapReady rejected");
    check(!l.host.wantsPlayer() && !l.host.snapshotEligible(),"no promotion by rejected MapReady");
    l.sendReady(l.gen,l.epoch);l.sendReady(l.gen,l.epoch);
    check(until([&]{l.pump();},[&]{return logged("map_ready_rejected","duplicate")==1;}),"duplicate MapReady rejected after acceptance");
    check(l.host.wantsPlayer() && logged("network_ready","\"map_ready_accepted\":true")==2,"one acceptance per Link (positive + this)");
    // Snapshot from an older epoch: host Worker drop, client gate refusal.
    auto old=l.snap(0,9);old.mapEpoch=l.epoch-1;old.targetSlot=1;check(isSuccess(l.host.bridge.pushOutbound(BridgeMessage{old})),"old-epoch fixture queued");
    auto forged=l.snap(0,10);forged.mapEpoch=l.epoch-1;
    check(!l.gate.snapshotEligible(forged.connectionGeneration,forged.mapEpoch),"client refuses other-epoch snapshot even if delivered");
    l.wait(150);check(l.received.empty() && l.host.worker.getStats().bestEffortDropped>=1,"old-epoch snapshot never on wire");
    l.finish("DOC56_premature");
}
static void mismatch() {
    Link l(28863,"maps/TOMB.MAP","maps/KARNAK.MAP",map56::randomEpoch(0));
    l.wait(400);
    check(l.gate.state()==map56::ClientGate::State::Mismatch && !l.readyQueued,"different map ids: client never sends MapReady");
    check(!l.host.wantsPlayer() && !l.host.snapshotEligible() && !l.host.publishNativeSnapshot(l.snap(0,1)),"host never eligible");
    // v1.10 limitation made explicit: a client that echoes the epoch regardless is accepted.
    l.sendReady(l.gen,l.epoch);
    check(until([&]{l.pump();},[&]{return l.host.wantsPlayer();}),"LIMITATION: host cannot verify client map from MsgMapReady(slot,epoch)");
    l.finish("DOC56_mismatch");
}
static void clientTransition() {
    Link l(28864,"maps/TOMB.MAP","maps/TOMB.MAP",map56::randomEpoch(0));
    check(until([&]{l.pump();},[&]{return l.baseline;}),"ready");
    l.host.publishNativeSnapshot(l.snap(0,1));check(until([&]{l.pump();},[&]{return l.accepted.size()==1;}),"first snapshot accepted");
    l.localFault=Fault::Transition;l.pump();
    l.host.publishNativeSnapshot(l.snap(0,2));l.wait(150);
    check(l.gate.state()==map56::ClientGate::State::Invalidated && l.accepted.size()==1 && l.received.size()==2,"client loading after READY: delivered snapshot refused");
    l.finish("DOC56_client_transition");
}
static void disconnectAndReconnect() {
    {
        Link l(28865,"maps/TOMB.MAP","maps/TOMB.MAP",map56::randomEpoch(0));l.autoReady=false;
        check(until([&]{l.pump();},[&]{return l.gen!=0;}),"handshake started");
        l.client.stop();l.left=true;
        check(until([&]{l.pump();},[&]{return l.host.mapGate().peer()==map56::HostGate::Peer::Disconnected;},8000),"client disconnect during handshake retires peer");
        check(!l.host.wantsPlayer(),"no READY after disconnect");
        l.finish("DOC56_disconnect");
    }
    Link l(28866,"maps/TOMB.MAP","maps/TOMB.MAP",map56::randomEpoch(0));
    check(until([&]{l.pump();},[&]{return l.baseline;}),"gen1 ready");
    const auto oldGen=l.gen;
    BridgeControl rc;rc.type=BridgeControl::Type::RECONNECT;rc.slotId=1;rc.connectionGeneration=oldGen;
    check(isSuccess(l.cb.pushOutbound(BridgeMessage{rc})),"reconnect requested");
    check(until([&]{l.pump();},[&]{return l.gen>oldGen && l.baseline;},5000),"new generation re-runs full MapReady contract");
    const auto noPeer=l.client.getStats().outboundNoPeer;l.sendReady(oldGen,l.epoch);l.wait(100);
    check(l.client.getStats().outboundNoPeer>noPeer && logged("map_ready_rejected","duplicate")<=1,"old-generation queued MapReady not restamped");
    check(l.host.generation()==l.gen && l.host.wantsPlayer(),"single current session owner");
    l.finish("DOC56_reconnect");
}
static void shutdownPending() {
    Link l(28867,"maps/TOMB.MAP","maps/TOMB.MAP",map56::randomEpoch(0));l.autoReady=false;
    check(until([&]{l.pump();},[&]{return l.gen!=0;}),"authenticated");
    l.sendReady(l.gen,l.epoch);std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const auto before=logged("network_ready","\"map_ready_accepted\":true");
    const auto t0=Clock::now();l.client.stop();l.left=true;l.host.stop("DOC56_pending_shutdown");
    check(Clock::now()-t0<std::chrono::seconds(3) && !l.host.wantsPlayer() && !l.host.snapshotEligible(),"shutdown with pending MapReady: join, no promotion");
    check(logged("network_ready","\"map_ready_accepted\":true")==before,"pending MapReady retired, not accepted");
    check(!l.cb.getAffinityViolationCount() && !l.host.bridge.getAffinityViolationCount(),"strict SPSC affinity");l.cb.unregisterSimulationThread();
}
int main() {
    derivation();reader();hostGate();clientGate();
    positive();premature();mismatch();clientTransition();disconnectAndReconnect();shutdownPending();
    std::cout<<"MAPREADY SUMMARY checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
