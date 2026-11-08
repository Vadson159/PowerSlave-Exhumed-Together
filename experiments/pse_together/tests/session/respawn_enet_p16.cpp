// Real ENet workers and existing v1.12 DTOs; native completion is explicitly
// a fixture. This executable never starts KEX and cannot attest native respawn.
#include "respawn_p16.hpp"
#include "enet_worker.h"
#include "native_map111.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
namespace rp=pse::respawn_p16;
using namespace pse;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
template<class Pump,class Done> bool until(Pump pump,Done done){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{pump();if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    while(std::chrono::steady_clock::now()<deadline);
    return false;
}
}
int main(){
    ThreadBridge hb,cb;hb.registerSimulationThread();cb.registerSimulationThread();
    ENetWorker host(hb,ENetWorkerMode::HOST),client(cb,ENetWorkerMode::CLIENT);
    constexpr std::uint32_t epoch=0x16070001;
    const map56::Identity map{"maps/TOMB.MAP",0x1234,1,71,28608}; // numeric fixture, not a live map witness
    host.setInitialSessionMap(map.name,epoch,map111::structure(map));
    HostConfig hc;hc.port=29116;hc.bindAddress="127.0.0.1";hc.maxClientSlots=1;hc.joinToken="P16_OFFLINE";
    check(host.startHost(hc),"real host worker starts on loopback only");
    ClientConfig cc;cc.hostPort=hc.port;cc.joinToken=hc.joinToken;cc.participantId="p16_policy_fixture";
    check(client.startClient(cc),"real client worker starts");
    rp::Team team;team.map(epoch,true);const rp::Identity primary{0,1,epoch};team.connect(primary,1);
    rp::Identity remote{};bool hostJoined{},clientJoined{},left{};
    unsigned accepted{},replay{},rejected{},nativeFixtureCalls{};
    std::vector<BridgeRespawnConfirm> confirmed;
    const auto pump=[&]{
        BridgeMessage m;
        while(hb.pollInbound(m)){
            if(const auto* e=std::get_if<BridgeSessionEvent>(&m)){
                if(e->type==BridgeSessionEvent::Type::CLIENT_JOINED){
                    remote={e->slotId,e->connectionGeneration,epoch};hostJoined=team.connect(remote,1)==rp::Result::Accepted;
                }else if(e->type==BridgeSessionEvent::Type::CLIENT_LEFT){left=team.disconnect(remote)==rp::Result::Accepted;}
            }else if(const auto* r=std::get_if<BridgeRespawnRequest>(&m)){
                rp::Plan p;rp::Confirmation c;
                const auto result=team.request({{r->slotId,r->connectionGeneration,r->mapEpoch},r->deathSequence},{true,true,true,true},p,c);
                if(result==rp::Result::Accepted){
                    ++accepted;++nativeFixtureCalls;
                    // The fixture supplies the receipt. This is deliberately
                    // NOT labelled a native readback or an engine operation.
                    check(team.complete(p,{true,true,true,true},true,{},c)==rp::Result::Accepted,"fixture completion commits one plan");
                }else if(result==rp::Result::Replay)++replay;
                else{++rejected;continue;}
                BridgeRespawnConfirm out;out.slotId=c.request.who.slot;out.mapEpoch=c.request.who.epoch;
                out.deathSequence=c.request.death;out.newEntityId=0x5500+out.slotId;out.newGeneration=c.generation;
                out.defaultWeapons=c.loadout.weapons;out.ammo=c.loadout.ammo[1];
                out.posX=-7420;out.posY=-6775;out.posZ=1152;out.yaw=-3.137605f;
                check(isSuccess(hb.pushOutbound(BridgeMessage{out})),"confirmation enqueued through real host SPSC bridge");
            }
        }
        while(cb.pollInbound(m)){
            if(const auto* e=std::get_if<BridgeSessionEvent>(&m);e&&e->type==BridgeSessionEvent::Type::CLIENT_JOINED)
                clientJoined=e->mapEpoch==epoch&&e->slotId==1&&e->connectionGeneration!=0;
            if(const auto* c=std::get_if<BridgeRespawnConfirm>(&m))confirmed.push_back(*c);
        }
    };
    check(until(pump,[&]{return hostJoined&&clientJoined;}),"real authenticated Welcome agrees on epoch and slot");
    if(hostJoined&&clientJoined){
        check(team.observeDeath(primary,1)==rp::Result::Accepted&&team.observeDeath(remote,1)==rp::Result::Accepted&&team.counts().totalPartyKill(),"two-slot TPK policy fixture established");
        rp::DeadInput input;input.bind(remote,1);input.sample(false,1,true);input.observe(true,1);
        const auto request=input.sample(true,2,true);
        check(request.has_value(),"client E policy produces request after death");
        BridgeRespawnRequest req;req.slotId=remote.slot;req.connectionGeneration=remote.connection;req.mapEpoch=epoch;req.deathSequence=1;
        check(isSuccess(cb.pushOutbound(BridgeMessage{req})),"request enqueued through real client SPSC bridge");
        check(until(pump,[&]{return !confirmed.empty();}),"existing reliable UDP request and confirm make full round trip");
        if(!confirmed.empty()){
            const auto& c=confirmed.front();
            check(c.slotId==remote.slot&&c.mapEpoch==epoch&&c.deathSequence==1&&c.newGeneration==2&&c.defaultWeapons==3&&c.ammo==60,"real wire carries fresh generation and native starter ammo");
            check(c.posX==-7420&&c.posY==-6775&&c.posZ==1152,"real confirm preserves supplied spawn coordinates");
            check(c.health==DEFAULT_HEALTH,"v112 confirm does NOT carry native HP; default is never used as native proof");
        }
        check(nativeFixtureCalls==1&&team.counts().alive==1&&team.counts().dead==1,"one E does not automatically revive teammate");
        for(unsigned i=0;i<20;++i)check(isSuccess(cb.pushOutbound(BridgeMessage{req})),"duplicate reliable request enqueued");
        check(until(pump,[&]{return replay==20&&confirmed.size()==21;}),"all repeated requests replay through actual worker pair");
        check(accepted==1&&nativeFixtureCalls==1,"replayed requests never repeat fixture native operation");
        auto stale=req;stale.mapEpoch=epoch-1;
        check(isSuccess(cb.pushOutbound(BridgeMessage{stale})),"old-epoch request reaches host policy");
        stale=req;stale.deathSequence=2;check(isSuccess(cb.pushOutbound(BridgeMessage{stale})),"future death request reaches host policy");
        check(until(pump,[&]{return rejected==2;}),"actual transported stale and future requests rejected");
        check(nativeFixtureCalls==1&&confirmed.size()==21,"invalid requests produce neither fixture mutation nor confirm");
        check(team.observeDeath(remote,2)==rp::Result::Accepted,"second incarnation death in fixture");
        req.deathSequence=2;check(isSuccess(cb.pushOutbound(BridgeMessage{req})),"second life E request enqueued");
        check(until(pump,[&]{return confirmed.size()==22;}),"second-life round trip delivered");
        check(nativeFixtureCalls==2&&confirmed.back().newGeneration==3,"second life gets distinct native-generation plan");
    }
    client.stop();check(until(pump,[&]{return left;}),"real disconnect retires policy occupant");host.stop();
    check(!hb.getAffinityViolationCount()&&!cb.getAffinityViolationCount(),"both actual SPSC bridges retain thread ownership");
    check(!host.isRunning()&&!client.isRunning(),"both worker threads joined");
    hb.unregisterSimulationThread();cb.unregisterSimulationThread();
    std::printf("P16 ENET checks=%u failures=%u; REAL LOOPBACK, NATIVE COMPLETION IS A FIXTURE\n",checks,failures);
    return failures?1:0;
}
