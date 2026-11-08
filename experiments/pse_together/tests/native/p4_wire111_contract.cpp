// Real loopback ENet + production P4 session. No KEX and no mocked promotion.
#include "network_input.hpp"
#include "native_map111.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstdarg>
using namespace pse;
int checks=0,failures=0;unsigned rejected=0;
void check(bool ok,const char* label) { ++checks;if(!ok) { ++failures;std::printf("FAIL %s\n",label); } }
void journal(const char* event,const char* format,...) {
    if(std::string(event)=="map_ready_rejected")++rejected;
    char s[1600];va_list args;va_start(args,format);vsnprintf_s(s,sizeof(s),_TRUNCATE,format,args);va_end(args);
    if(std::string(event)=="map_ready_rejected" || std::string(event)=="network_ready")std::printf("%s %s\n",event,s);
}
map56::Identity identity() { return {"maps/TOMB.MAP",0x1234,1,71,28608}; }
template<class T> void codec(const T& input) {
    std::vector<uint8_t> bytes;ByteWriter w(bytes);input.serialize(w);T output;
    ByteReader r(bytes.data(),bytes.size());check(output.deserialize(r)&&!r.remaining(),"v111 payload round trip");
    check(output.mapName==input.mapName&&output.mapStructure==input.mapStructure,"native name and u64 structure preserved");
    bool refuses=true;for(size_t n=0;n<bytes.size();++n) { ByteReader shortReader(bytes.data(),n);T s;refuses &= !s.deserialize(shortReader); }
    check(refuses,"every truncated native identity payload refused");
}
struct Link {
    m12a::Session host{journal};ENetClientNode client;map56::Identity id=identity();MsgWelcome welcome;
    uint64_t tick{};unsigned baselines{},changes{},protocolRejects{};bool welcomed{},badChange{};
    explicit Link(unsigned index) {
        check(host.configureNativeMap(id,900+index),"bind native host identity");check(host.start(static_cast<uint16_t>(28972+index)),"real host worker start");
        client.onWelcomeCallback=[this](const MsgWelcome& w) { welcome=w;welcomed=true; };
        client.onRejectCallback=[this](const MsgReject& r) { if(r.reasonCode==REJECT_PROTOCOL_MISMATCH)++protocolRejects; };
        client.setCustomPacketHandler([this](uint8_t type,ByteReader& r,uint32_t) {
            if(type==MSG_MAP_CHANGE) { MsgMapChange m;if(m.deserialize(r)&&!r.remaining()) { ++changes;badChange|=!map111::matches(id,m.mapName,m.mapStructure); }return true; }
            if(type==MSG_WORLD_BASELINE) { MsgWorldBaseline b;if(b.deserialize(r))++baselines;return true; }return false;
        });
        ClientConfig c;c.hostPort=static_cast<uint16_t>(28972+index);c.joinToken="M12A_Loopback";c.participantId="P4_v111_"+std::to_string(index);c.bridgeMode=true;
        check(client.connect(c),"real client connect");check(until([&] {return welcomed&&changes;}),"Welcome and targeted MapChange delivered");
        check(welcome.protocolVersion==0x111&&map111::matches(id,welcome.mapName,welcome.mapStructure)&&!badChange,"Welcome and MapChange have exact native host structure");
    }
    void pump(bool level=true) { host.verifyNativeMap(map56::Fault::None,id);host.tick(++tick,level);client.poll(1);std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    template<class F> bool until(F done) {
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        do {pump();if(done())return true;}while(std::chrono::steady_clock::now()<end);return false;
    }
    MsgMapReady ready() const { MsgMapReady r;r.slotId=1;r.mapEpoch=welcome.mapEpoch;r.mapName=id.name;r.mapStructure=map111::structure(id);return r; }
    void send(const MsgMapReady& r) { client.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_MAP_READY,0,r),true); }
    void settle() { for(unsigned i=0;i<80;++i)pump(); }
    ~Link() { client.disconnect();host.stop("P4_wire111_test_end");check(!host.bridge.getAffinityViolationCount(),"host bridge affinity preserved"); }
};
int main() {
    static_assert(NET_PROTOCOL_VERSION==0x111);
    auto id=identity();MsgWelcome w;w.mapName=id.name;w.mapStructure=map111::structure(id);codec(w);
    MsgMapChange m;m.mapName=id.name;m.mapStructure=map111::structure(id);codec(m);
    MsgMapReady r;r.mapName=id.name;r.mapStructure=map111::structure(id);codec(r);
    r.mapName=std::string(32,'x');codec(r);
    bool throws=false;try { r.mapName+= 'x';std::vector<uint8_t> b;ByteWriter bw(b);r.serialize(bw); }catch(const std::invalid_argument&) {throws=true;}
    check(throws,"oversized name cannot silently truncate");
    r.mapName="maps/TOMB.MAP";auto dto=BridgeMapReady::fromNet(r);auto net=dto.toNet();check(net.mapName==r.mapName&&net.mapStructure==r.mapStructure,"DTO preserves native identity in both directions");
    {
        Link l(0);l.send(l.ready());check(l.until([&] {return l.host.wantsPlayer()&&l.baselines==1;}),"exact identity promotes once and sends one baseline");
        auto stale=l.ready();stale.mapEpoch--;l.send(stale);l.send(l.ready());l.settle();
        check(l.host.wantsPlayer()&&l.baselines==1,"duplicate and stale READY never repeat baseline");
    }
    for(unsigned n=1;n<=4;++n) {
        Link l(n);auto bad=l.ready();if(n==1)bad.mapName="maps/KARNAK.MAP";if(n==2)++bad.mapStructure.psmVersion;if(n==3)++bad.mapStructure.countA;if(n==4)++bad.mapStructure.countB;
        auto before=rejected;l.send(bad);check(l.until([&] {return rejected>before;}),"real host rejects mismatching client identity");
        check(!l.host.wantsPlayer()&&!l.host.snapshotEligible()&&!l.baselines,"mismatch has zero promotion baseline and eligibility");
        l.send(l.ready());l.settle();check(!l.host.wantsPlayer()&&!l.baselines,"identity mismatch cannot repromote within the same generation");
    }
    {
        Link l(5);auto packet=encodePacket(MSG_MAP_READY,0,l.ready());packet[4]=0x10;packet[5]=0x01;
        l.client.sendPacket(CHANNEL_RELIABLE_CONTROL,packet,true);check(l.until([&] {return l.protocolRejects!=0;}),"mixed v110 header explicitly rejected by v111 host");
        check(!l.host.wantsPlayer()&&!l.baselines,"mixed version never promotes or downgrades");
    }
    {
        Link l(6);auto packet=encodePacket(MSG_MAP_READY,0,l.ready());packet.push_back(0xff);
        l.client.sendPacket(CHANNEL_RELIABLE_CONTROL,packet,true);l.settle();check(!l.host.wantsPlayer()&&!l.baselines,"trailing bytes rejected before READY DTO");
        packet=encodePacket(MSG_MAP_READY,0,l.ready());packet.resize(packet.size()-1);l.client.sendPacket(CHANNEL_RELIABLE_CONTROL,packet,true);l.settle();
        check(!l.host.wantsPlayer()&&!l.baselines,"truncated wire identity never promotes");
        l.send(l.ready());check(l.until([&] {return l.host.wantsPlayer();}),"malformed packets do not corrupt later valid identity");
    }
    std::printf("P4 WIRE111 checks=%d failures=%d; OFFLINE ENET, NOT KEX\n",checks,failures);return failures?1:0;
}
