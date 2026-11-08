#include "network_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
void record(const char* event, const char* fmt, ...) {
    std::printf("%s ",event);
    va_list args; va_start(args,fmt); std::vprintf(fmt,args); va_end(args);
    std::puts("");
}
int main() {
    m12a::Session session(record);
    if (!session.start(28747)) return 2;
    pse::ENetClientNode client;
    pse::ClientConfig cc; cc.hostPort=28747; cc.joinToken="M12A_Loopback"; cc.participantId="adapter_probe";
    if(!client.connect(cc)) return 2;
    std::uint64_t tick=0;
    auto step=[&] { client.poll(1); session.tick(++tick,true); Sleep(5); };
    for(int n=0;n<150 && !session.wantsPlayer();++n) step();
    if(!session.wantsPlayer()) return 3;
    client.sendInputCommand(1,0,0,0,0,0);
    for(int n=0;n<40 && !session.nativeButtons();++n) step();
    if(session.nativeButtons()!=4) return 4;
    const auto accepted=tick;
    while(tick<accepted+5) step();
    if(session.nativeButtons()!=4) return 5;
    step();
    if(session.nativeButtons()) return 6;
    client.sendInputCommand(1,0,0,0,0,0);
    for(int n=0;n<40 && !session.nativeButtons();++n) step();
    client.sendInputCommand(0,0,0,0,0,0);
    for(int n=0;n<40 && session.nativeButtons();++n) step();
    if(session.nativeButtons()) return 7;
    session.bridge.requestResync(pse::RESYNC_EXTERNAL,"test terminal resync");
    step();
    if(session.wantsPlayer() || session.nativeButtons()) return 8;
    for(int n=0;n<30;++n) step();
    session.stop("adapter_test");
    client.disconnect();
    if(session.worker.isRunning() || session.bridge.getAffinityViolationCount()) return 9;
    std::puts("PASS adapter timeout/release/resync/join; no KEX calls");
    return 0;
}
