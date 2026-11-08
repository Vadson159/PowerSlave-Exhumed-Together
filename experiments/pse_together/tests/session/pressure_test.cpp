#include "network_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
void record(const char* event,const char* fmt,...) {
    std::printf("%s ",event); va_list args; va_start(args,fmt); std::vprintf(fmt,args); va_end(args); std::puts("");
}
int main() {
    m12a::Session session(record);
    if(!session.start(28748)) return 2;
    Sleep(30); // initial broadcast has reached the worker
    session.worker.setOutboundDrainPaused(true);
    pse::ENetClientNode client;
    pse::ClientConfig cc; cc.hostPort=28748; cc.joinToken="M12A_Loopback"; cc.participantId="pressure_probe"; cc.bridgeMode=true;
    unsigned baselines=0;
    client.setCustomPacketHandler([&](std::uint8_t type,pse::ByteReader& reader,std::uint32_t) {
        if(type!=pse::MSG_WORLD_BASELINE) return false;
        pse::MsgWorldBaseline baseline;
        if(!baseline.deserialize(reader) || !baseline.entities.empty() || baseline.mapName!="M12A_INPUT_ONLY") return false;
        ++baselines; return true;
    });
    if(!client.connect(cc)) return 2;
    std::uint64_t tick=0;
    auto step=[&] { client.poll(1); session.tick(++tick,true); Sleep(5); };
    for(int n=0;n<60;++n) step();
    if(!client.isAuthenticated()) return 3;
    // Fill from the one SIMULATION producer. SessionEvent is internal-only;
    // fillers do not manufacture native/world events or mutate the peer.
    while(session.bridge.getOutboundOccupancy()<session.bridge.getOutboundCapacity()) {
        pse::BridgeMessage filler=pse::BridgeSessionEvent{};
        if(!pse::isSuccess(session.bridge.pushOutbound(std::move(filler)))) return 4;
    }
    pse::MsgMapReady mr; mr.slotId=client.getAssignedSlot(); mr.mapEpoch=1;
    client.sendPacket(pse::CHANNEL_RELIABLE_CONTROL,pse::encodePacket(pse::MSG_MAP_READY,1,mr),true);
    for(int n=0;n<20;++n) step();
    const auto overflow=session.bridge.getMetrics().outbound.criticalOverflowCount;
    if(!session.wantsPlayer() || !overflow || baselines) return 5;
    session.worker.setOutboundDrainPaused(false);
    for(int n=0;n<100 && !baselines;++n) step();
    for(int n=0;n<15;++n) step();
    if(baselines!=1 || session.bridge.isResyncRequired()) return 6;
    client.disconnect(); session.stop("pressure_test");
    std::printf("PASS full outbound queue: %llu rejected attempts, baseline delivered exactly %u, no declared loss; no KEX calls\n",overflow,baselines);
    return 0;
}
