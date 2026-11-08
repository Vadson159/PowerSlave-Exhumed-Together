// Actual v1.12 codec + two real NETWORK workers + UDP malformed packet gates.
// No KEX object, synthetic native call, HUD or runtime acceptance claim.
#include "network_input.hpp"
#include "native_map111.hpp"
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <functional>
#include <limits>
#include <thread>
#include <vector>
using namespace pse;
using Clock=std::chrono::steady_clock;
namespace {
unsigned checks{},failures{};
constexpr std::array<uint8_t,2> OldVersions{0x10,0x11};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void logger(const char*,const char*,...){}
map56::Identity identity(){return {"maps/TOMB.MAP",0x1234,1,71,28608};}
template<class Work,class Pred> bool until(Work work,Pred done,unsigned timeoutMs=4000){
    const auto end=Clock::now()+std::chrono::milliseconds(timeoutMs);
    do{work();if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(Clock::now()<end);return false;
}
BridgePlayerSnapshot sample(uint8_t slot=0,uint32_t seq=7,uint32_t epoch=0xfedc0001){
    BridgePlayerSnapshot s;s.slotId=slot;s.entityId=21760+slot;s.entityGeneration=8+slot;
    s.sequence=seq;s.serverTick=1000+seq;s.mapEpoch=epoch;s.connectionGeneration=1;
    s.posX=-7420.125f+slot;s.posY=-6775.5f;s.posZ=1152.25f;s.velX=std::bit_cast<float>(0x80000000u);s.velY=17.25f;s.velZ=-0.125f;
    s.yaw=2.156752f;s.pitch=std::bit_cast<float>(0x80000000u);s.health=slot?137:200;s.armor=0;
    s.activeWeapon=6;s.weaponMask=0xff;s.ammoByWeapon={0,47,113,8,37,55,99,17};s.ammo=s.ammoByWeapon[s.activeWeapon];
    s.keys=9;s.artifacts=0x21;s.abilities=3;s.questItems=0xa5;s.ankhCount=5;s.ankhMask=0x1f;s.teamDolls=0x89abcdef;
    s.pendingWeapon=slot?4:-1;s.stateTimeNs=0x0102030405060708ULL+slot;s.resetToken=0xf1234567;s.discontinuitySequence=23;
    s.buttons=0x104;s.deathSequence=11;return s;
}
std::vector<uint8_t> record(const PlayerStateSnapshot& p){std::vector<uint8_t> bytes;ByteWriter w(bytes);p.serialize(w);return bytes;}
std::vector<uint8_t> packet(const BridgePlayerSnapshot& s){
    MsgPlayerSnapshot m;m.snapshotSequence=s.sequence;m.serverTick=s.serverTick;m.mapEpoch=s.mapEpoch;m.players={s.toNet()};auto bytes=encodePacket(MSG_PLAYER_SNAPSHOT,s.sequence,m);
    // Malicious-wire fixtures must preserve their nonfinite bits explicitly:
    // the inherited generic ByteWriter sanitizes them to zero. The production
    // native host worker rejects them BEFORE that writer is reached.
    const std::array<float,8> floats{s.posX,s.posY,s.posZ,s.velX,s.velY,s.velZ,s.yaw,s.pitch};
    for(std::size_t i=0;i<floats.size();++i)if(!std::isfinite(floats[i])){
        const auto bits=std::bit_cast<uint32_t>(floats[i]);for(unsigned k=0;k<4;++k)bytes[33+i*4+k]=static_cast<uint8_t>(bits>>(8*k));
    }
    return bytes;
}
bool exactDecode(const std::vector<uint8_t>& bytes,MsgPlayerSnapshot& out){
    ByteReader r(bytes.data(),bytes.size());MsgHeader h;
    return h.deserialize(r)&&h.magic==NET_MAGIC&&h.version==NET_PROTOCOL_VERSION&&h.msgType==MSG_PLAYER_SNAPSHOT&&
        out.deserialize(r)&&!r.remaining()&&out.snapshotSequence==h.sequence&&out.mapEpoch&&out.serverTick&&!out.players.empty();
}
using Mutation=std::pair<const char*,std::function<void(BridgePlayerSnapshot&)>>;
std::vector<Mutation> mutations(){return {
    {"slot eight",[](auto& s){s.slotId=8;}},{"zero entity",[](auto& s){s.entityId=0;}},{"zero lifetime",[](auto& s){s.entityGeneration=0;}},
    {"NaN position",[](auto& s){s.posX=std::numeric_limits<float>::quiet_NaN();}},{"infinite velocity",[](auto& s){s.velZ=std::numeric_limits<float>::infinity();}},
    {"health outside int16 high",[](auto& s){s.health=32768;}},{"health outside int16 low",[](auto& s){s.health=-32769;}},
    {"alive zero health",[](auto& s){s.health=0;}},{"dead positive health",[](auto& s){s.lifeState=LIFE_DEAD;}},
    {"dead missing event",[](auto& s){s.lifeState=LIFE_DEAD;s.health=0;s.deathSequence=0;}},{"unsupported dying state",[](auto& s){s.lifeState=LIFE_DYING;}},
    {"selected slot eight",[](auto& s){s.activeWeapon=8;}},{"unknown weapon bit",[](auto& s){s.weaponMask=0x1ff;}},{"selected unowned",[](auto& s){s.weaponMask=1;}},
    {"pending below minus one",[](auto& s){s.pendingWeapon=-2;}},{"pending slot eight",[](auto& s){s.pendingWeapon=8;}},
    {"pending unowned",[](auto& s){s.weaponMask=1u<<6;s.pendingWeapon=4;}},{"ammo mirror mismatch",[](auto& s){++s.ammo;}},
    {"unknown stock key bit",[](auto& s){s.keys=16;}},{"unknown artifact bit",[](auto& s){s.artifacts=64;}},{"unknown ability bit",[](auto& s){s.abilities=4;}},
    {"unknown quest bit",[](auto& s){s.questItems=256;}},{"sixth ankh",[](auto& s){s.ankhCount=6;s.ankhMask=0x3f;}},
    {"corrupt ankh count",[](auto& s){s.ankhMask=0;}},{"missing posttick time",[](auto& s){s.stateTimeNs=0;}},
    {"zero reset token",[](auto& s){s.resetToken=0;}},{"zero discontinuity",[](auto& s){s.discontinuitySequence=0;}}
};}
void codec(){
    static_assert(NET_PROTOCOL_VERSION==0x0112);
    const auto s=sample();const auto bytes=record(s.toNet());check(bytes.size()==121,"v112 record is121 explicit bytes");
    check(bytes[65]==0&&bytes[67]==47&&bytes[81]==9&&bytes[83]==0x21&&bytes[87]==3&&bytes[91]==0xa5,"golden ammo keys artifact ability quest offsets");
    check(bytes[93]==5&&bytes[95]==0x1f&&bytes[97]==0xef&&bytes[98]==0xcd&&bytes[101]==0xff&&bytes[104]==0xff,"golden ankh dolls and signed pending offsets");
    check(bytes[105]==8&&bytes[106]==7&&bytes[112]==1&&bytes[113]==0x67&&bytes[116]==0xf1&&bytes[117]==23,"golden u64 timestamp and reset/discontinuity offsets");
    const auto wire=packet(s);MsgPlayerSnapshot decoded;
    check(wire.size()==145&&wire[4]==0x12&&wire[5]==1&&exactDecode(wire,decoded),"one-record v112 packet exact framed decode");
    const auto dto=BridgePlayerSnapshot::fromNet(decoded.players[0],decoded.snapshotSequence,decoded.serverTick,decoded.mapEpoch,19);
    check(record(dto.toNet())==bytes&&dto.sequence==s.sequence&&dto.serverTick==s.serverTick&&dto.mapEpoch==s.mapEpoch&&dto.connectionGeneration==19,"every scalar and raw float bit survives DTO in both directions");
    for(std::size_t n=0;n<wire.size();++n){std::vector<uint8_t> shortWire(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(n));MsgPlayerSnapshot m;check(!exactDecode(shortWire,m),"every truncated full packet rejected");}
    auto trailing=wire;trailing.push_back(0);check(!exactDecode(trailing,decoded),"trailing byte rejected by exact framing");
    for(uint8_t old:OldVersions){auto mixed=wire;mixed[4]=old;check(!exactDecode(mixed,decoded),"v110/v111 packet cannot downgrade v112 decode");}
    for(const auto& [name,mutate]:mutations()){auto bad=s;mutate(bad);check(!bad.toNet().valid112()&&!exactDecode(packet(bad),decoded),name);}
    for(const auto hp:{-32768,-17,0}){auto dead=s;dead.health=hp;dead.lifeState=LIFE_DEAD;dead.deathSequence=12;check(exactDecode(packet(dead),decoded)&&decoded.players[0].health==hp,"signed native death health preserved exactly");}
    for(uint16_t mask=0;mask<32;++mask){auto a=s;a.ankhMask=mask;a.ankhCount=static_cast<uint16_t>(std::popcount(mask));check(exactDecode(packet(a),decoded),"each unique stock ankh subset supported");}
    MsgPlayerSnapshot batch;batch.snapshotSequence=s.sequence;batch.serverTick=s.serverTick;batch.mapEpoch=s.mapEpoch;
    for(uint8_t slot=0;slot<8;++slot)batch.players.push_back(sample(slot).toNet());
    auto eight=encodePacket(MSG_PLAYER_SNAPSHOT,s.sequence,batch);check(eight.size()==992&&exactDecode(eight,decoded)&&decoded.players.size()==8,"eight full records stay below ENet MTU budget");
    batch.players.resize(2);check(encodePacket(MSG_PLAYER_SNAPSHOT,s.sequence,batch).size()==266,"two full records are266 bytes");
}
void realWorkers(){
    constexpr uint32_t epoch=0xa3710234;const auto id=identity();
    m12a::Session host(logger);check(host.configureNativeMap(id,epoch),"host binds retained native v111 map identity");check(host.start(29087),"v112 Host Worker starts");
    ThreadBridge cb;cb.registerSimulationThread();ENetWorker client(cb,ENetWorkerMode::CLIENT);client.setClientInputCadence(true);client.setNativePlayerSnapshots(true);
    std::atomic<unsigned> observed{},badBytes{};client.setPlayerSnapshotObserver([&](bool,const auto&,std::size_t size){++observed;if(size!=145)++badBytes;});
    ClientConfig config;config.hostPort=29087;config.joinToken="M12A_Loopback";config.participantId="P7_v112_two_workers";
    check(client.startClient(config),"v112 Client Worker starts");
    uint64_t tick{};uint32_t generation{};bool ready{},baseline{},identityMatched{};std::vector<BridgePlayerSnapshot> received;
    const auto pump=[&]{host.verifyNativeMap(map56::Fault::None,id);host.tick(++tick,true);BridgeMessage msg;
        while(cb.pollInbound(msg)){
            if(const auto* e=std::get_if<BridgeSessionEvent>(&msg);e&&e->type==BridgeSessionEvent::Type::CLIENT_JOINED){
                generation=e->connectionGeneration;identityMatched=e->mapEpoch==epoch&&map111::matches(id,e->mapName,e->mapStructure);
                BridgeMapReady mr;mr.slotId=e->slotId;mr.connectionGeneration=generation;mr.mapEpoch=epoch;mr.mapName=id.name;mr.mapStructure=map111::structure(id);
                ready=isSuccess(cb.pushOutbound(BridgeMessage{mr}));
            }else if(const auto* b=std::get_if<BridgeWorldBaseline>(&msg))baseline=b->mapEpoch==epoch&&b->connectionGeneration==generation;
            else if(const auto* s=std::get_if<BridgePlayerSnapshot>(&msg))received.push_back(*s);
        }
    };
    check(until(pump,[&]{return ready&&baseline&&host.snapshotEligible();}),"two real workers finish native identity READY and diagnostic baseline");
    check(identityMatched&&generation,"v112 Welcome retains exact native name structure and authenticated generation");
    for(uint8_t slot=0;slot<2;++slot){const auto sent=sample(slot,30+slot,epoch);check(host.publishNativeSnapshot(sent),"full native scalar snapshot enqueued by simulation owner");
        const auto has=[&]{for(const auto& got:received)if(got.slotId==slot&&got.sequence==sent.sequence)return true;return false;};
        check(until(pump,has),"full scalar snapshot delivered by real UDP worker pair");
        bool exact=false;for(const auto& got:received)if(got.slotId==slot&&got.sequence==sent.sequence)exact=record(got.toNet())==record(sent.toNet())&&got.mapEpoch==epoch&&got.connectionGeneration==generation&&got.entityGeneration==sent.entityGeneration;
        check(exact,"all inventory health weapon life timestamp and raw float bytes match source through two workers");
    }
    const auto before=received.size();auto bad=sample(0,90,epoch);bad.targetSlot=1;bad.connectionGeneration=generation;bad.keys=16;
    const auto dropped=host.worker.getStats().bestEffortDropped;
    check(isSuccess(host.bridge.pushOutbound(BridgeMessage{bad})),"invalid scalar snapshot reaches actual outbound validation fixture");
    const auto settle=Clock::now()+std::chrono::milliseconds(100);until(pump,[&]{return Clock::now()>=settle;},500);
    check(received.size()==before&&host.worker.getStats().bestEffortDropped>dropped,"host worker rejects corrupt native scalar before serialization/send");
    check(observed>=2&&!badBytes,"client observer sees actual145-byte v112 packets");
    client.stop();host.stop("P7_v112_workers_done");check(!cb.getAffinityViolationCount()&&!host.bridge.getAffinityViolationCount(),"both real SPSC bridges retain thread ownership");cb.unregisterSimulationThread();
}
void rawFraming(){
    constexpr uint32_t epoch=0xa3710235;const auto id=identity();
    ENetHostServer host;HostConfig hc;hc.bindAddress="127.0.0.1";hc.port=29088;hc.bridgeMode=true;
    check(host.start(hc),"raw-wire v112 fixture host starts");host.setSessionMap(id.name,epoch,map111::structure(id));
    ThreadBridge cb;cb.registerSimulationThread();ENetWorker client(cb,ENetWorkerMode::CLIENT);client.setNativePlayerSnapshots(true);
    ClientConfig cc;cc.hostPort=29088;cc.participantId="P7_v112_framing";check(client.startClient(cc),"framing client worker starts");
    bool joined{};std::vector<BridgePlayerSnapshot> received;
    const auto pump=[&]{host.poll(0);BridgeMessage msg;while(cb.pollInbound(msg)){
        if(const auto* e=std::get_if<BridgeSessionEvent>(&msg);e&&e->type==BridgeSessionEvent::Type::CLIENT_JOINED)joined=true;
        if(const auto* s=std::get_if<BridgePlayerSnapshot>(&msg))received.push_back(*s);
    }};
    check(until(pump,[&]{return joined;}),"framing peer authenticated");
    const auto good=sample(0,50,epoch);const auto encoded=packet(good);
    const auto send=[&](const std::vector<uint8_t>& bytes,uint8_t channel=CHANNEL_UNRELIABLE_SEQUENCED){host.broadcastPacket(channel,bytes,false);};
    auto trailing=encoded;trailing.push_back(0);send(trailing);auto truncated=encoded;truncated.pop_back();send(truncated);
    for(uint8_t old:OldVersions){auto bytes=encoded;bytes[4]=old;send(bytes);}send(encoded,CHANNEL_WORLD_SNAPSHOT);
    for(const auto& mutation:mutations()){auto bad=good;mutation.second(bad);send(packet(bad));}
    MsgPlayerSnapshot dup;dup.snapshotSequence=50;dup.serverTick=1050;dup.mapEpoch=epoch;dup.players={good.toNet(),good.toNet()};send(encodePacket(MSG_PLAYER_SNAPSHOT,50,dup));
    auto wrongEpoch=good;++wrongEpoch.mapEpoch;send(packet(wrongEpoch));auto wrongSequence=encoded;wrongSequence[7]^=0x40;send(wrongSequence);
    const auto settle=Clock::now()+std::chrono::milliseconds(300);until(pump,[&]{return Clock::now()>=settle;},800);
    check(received.empty(),"actual client rejects both old versions trailing truncated wrong channel identity sequence and every invalid scalar");
    MsgPlayerSnapshot batch;batch.snapshotSequence=51;batch.serverTick=1051;batch.mapEpoch=epoch;batch.players={sample(0,51,epoch).toNet(),sample(1,51,epoch).toNet()};
    const auto bytes=encodePacket(MSG_PLAYER_SNAPSHOT,51,batch);
    bool sent{};check(until([&]{pump();if(!sent){send(bytes);sent=true;}},[&]{return received.size()>=2;}),"actual two-record266-byte UDP packet dispatches both slots");
    bool exact=received.size()==2;for(const auto& got:received)exact=exact&&got.slotId<2&&record(got.toNet())==record(sample(got.slotId,51,epoch).toNet());
    check(exact,"both batched records retain complete authoritative scalar data");
    client.stop();host.stop();check(!cb.getAffinityViolationCount(),"framing worker bridge affinity preserved");cb.unregisterSimulationThread();
}
void hostRejectsOldVersions(){
    const auto id=identity();for(uint8_t old:OldVersions){
        m12a::Session host(logger);const auto epoch=0xa3711000u+old;host.configureNativeMap(id,epoch);const auto port=static_cast<uint16_t>(29089+old-0x10);
        check(host.start(port),"host mixed-version fixture starts");ENetClientNode client;bool welcomed{},rejected{};MsgWelcome welcome;
        client.onWelcomeCallback=[&](const MsgWelcome& w){welcome=w;welcomed=true;};
        client.onRejectCallback=[&](const MsgReject& r){rejected=r.reasonCode==REJECT_PROTOCOL_MISMATCH;};
        ClientConfig cc;cc.hostPort=port;cc.joinToken="M12A_Loopback";cc.participantId="P7_v112_no_downgrade";cc.bridgeMode=true;check(client.connect(cc),"mixed-version peer connects with112 hello");
        uint64_t tick{};const auto pump=[&]{host.verifyNativeMap(map56::Fault::None,id);host.tick(++tick,true);client.poll(1);};
        check(until(pump,[&]{return welcomed;}),"112 Welcome before deliberately old-version READY");
        check(welcome.protocolVersion==0x112&&map111::matches(id,welcome.mapName,welcome.mapStructure),"Welcome advertises112 and unchanged native identity contract");
        MsgMapReady mr;mr.slotId=welcome.assignedSlot;mr.mapEpoch=epoch;mr.mapName=id.name;mr.mapStructure=map111::structure(id);auto bytes=encodePacket(MSG_MAP_READY,0,mr);bytes[4]=old;
        client.sendPacket(CHANNEL_RELIABLE_CONTROL,bytes,true);
        check(until(pump,[&]{return rejected;}),"host explicitly rejects110/111 READY rather than downgrading");
        check(!host.wantsPlayer()&&!host.snapshotEligible(),"mixed version receives no native simulation or snapshot admission");
        client.disconnect();host.stop("P7_old_version_done");
    }
}
}
int main(){codec();realWorkers();rawFraming();hostRejectsOldVersions();std::printf("P7 WIRE112 checks=%u failures=%u; OFFLINE ENET, NOT KEX\n",checks,failures);return failures?1:0;}
