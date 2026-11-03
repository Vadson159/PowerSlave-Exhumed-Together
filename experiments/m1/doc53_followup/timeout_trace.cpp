// Real bridge + ENet, bounded A10 diagnostic. No KEX; no timer-resolution change.
#include "bridge_test_harness.h"
#include <iostream>
using namespace pse;
using namespace pse::test;
int main() {
    bool ok=true;
    for(int stall:{0,180}) {
        Cluster c(static_cast<uint16_t>(28530+stall),1);
        ok=c.start() && c.waitSynced(8000) && ok;
        auto& client=*c.clients[0];
        const auto slot=client.sim.getLocalSlot();
        client.sim.submitLocalInput(1,0,0,0,BUTTON_ATTACK);
        ok=waitUntil([&]{return c.hostSim.getPlayerTransform(slot).y>2;},3000) && ok;
        auto begin=std::chrono::steady_clock::now();
        client.sim.stopLocalInput();
        if(stall)c.hostSim.injectTickStall(stall);
        for(int n=0;n<36;++n) {
            auto s=c.hostSim.getStats();
            std::cout<<"TIMEOUT stall="<<stall<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()
                     <<" tick="<<s.totalTicks<<" accepted="<<s.commandsProcessed<<" neutral="<<s.inputNeutralTimeouts
                     <<" y="<<c.hostSim.getPlayerTransform(slot).y<<std::endl;
            sleepMs(20);
        }
        auto a=c.hostSim.getPlayerTransform(slot).y;
        sleepMs(300);
        auto b=c.hostSim.getPlayerTransform(slot).y;
        ok=c.hostSim.getStats().inputNeutralTimeouts>0 && a==b && c.affinityViolations()==0 && ok;
        c.stop();
    }
    return ok?0:1;
}
