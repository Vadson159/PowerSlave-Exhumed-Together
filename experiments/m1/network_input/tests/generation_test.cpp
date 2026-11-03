#include "enet_worker.h"
#include <chrono>
#include <thread>
#include <iostream>
using namespace pse;
int main() {
    ThreadBridge bridge;
    bridge.registerSimulationThread();
    ENetWorker worker(bridge, ENetWorkerMode::HOST);
    HostConfig hc; hc.port = 28746; hc.maxClients = 1;
    if (!worker.startHost(hc)) return 2;
    ENetClientNode client;
    ClientConfig cc; cc.hostPort = hc.port; cc.participantId = "generation_probe";
    if (!client.connect(cc)) return 2;
    auto pump = [&](int ms, auto callback) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        do {
            client.poll(1);
            BridgeMessage msg;
            while (bridge.pollInbound(msg)) callback(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
    };
    pump(300, [](const BridgeMessage&) {});
    if (!client.isAuthenticated()) return 2;
    auto send = [&](std::uint8_t slot, std::uint32_t gen, std::uint32_t seq) {
        MsgInputCommand c;
        c.slotId=slot; c.connectionGeneration=gen; c.commandSequence=seq; c.moveForward=1;
        client.sendPacket(CHANNEL_UNRELIABLE_SEQUENCED, encodePacket(MSG_INPUT_COMMAND,seq,c),false);
    };
    unsigned wrong = 0, good = 0;
    const auto gen = client.getConnectionGeneration();
    send(client.getAssignedSlot(),gen+10,900);
    pump(150,[&](const BridgeMessage& m) { if (const auto* c=std::get_if<BridgeInputCommand>(&m)) {
        ++wrong; std::cout << "FOREIGN forwarded seq=" << c->sequence << " dto_gen=" << c->connectionGeneration << " actual_wire_gen=" << gen+10 << '\n';
    }});
    send(0,gen,800);
    pump(100,[&](const BridgeMessage& m) { if (std::holds_alternative<BridgeInputCommand>(m)) ++wrong; });
    send(client.getAssignedSlot(),gen,1);
    pump(150,[&](const BridgeMessage& m) { if (const auto* c=std::get_if<BridgeInputCommand>(&m)) {
        if(c->sequence==1 && c->connectionGeneration==gen) ++good;
    }});
    send(client.getAssignedSlot(),gen,1); // duplicate must not be forwarded
    pump(100,[&](const BridgeMessage& m) { if (std::holds_alternative<BridgeInputCommand>(m)) ++wrong; });
    client.disconnect();
    worker.stop();
    bridge.unregisterSimulationThread();
    std::cout << "foreign_or_duplicate_forwarded=" << wrong << " correct_low_sequence_forwarded=" << good << '\n';
    return wrong==0 && good==1 ? 0 : 1;
}
