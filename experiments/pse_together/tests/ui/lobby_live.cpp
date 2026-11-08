#include "ui/lobby.hpp"
#include <windows.h>
#include <cstdio>
#include <memory>
#include <thread>

using namespace pse;
using namespace pse::ui;
int checks{},failures{};
void check(bool b,const char* why) { ++checks; if(!b) { ++failures;std::printf("FAIL %s\n",why); } }
template<class P> bool until(LobbyHost& host,std::vector<std::unique_ptr<LobbyClient>>& clients,P predicate,unsigned ms=3000) {
    const auto end=nowMs()+ms;
    do {
        host.poll(); for(auto& c:clients) c->poll();
        if(predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(nowMs()<end);
    return predicate();
}
std::size_t size(const Model& m) { std::size_t n=0;for(const auto& p:m.players()) n+=p.has_value();return n; }
int main() {
    LobbyHost host; Menu hm;hm.host=true;hm.displayName="Хост";hm.token="P23_test";
    std::uint16_t selected{};
    for(unsigned i=0;i<16;++i) {
        auto p=static_cast<std::uint16_t>(30000+(GetCurrentProcessId()%20000)+i);
        if(host.start(hm,p)) { selected=p;break; }
    }
    check(selected!=0,"real ENet host started"); if(!selected)return 1;
    std::vector<std::unique_ptr<LobbyClient>> clients;
    Menu cm;cm.portText=std::to_string(selected);cm.token=hm.token;
    for(unsigned i=1;i<=7;++i) {
        cm.displayName="Игрок "+std::to_string(i+1);
        auto c=std::make_unique<LobbyClient>();
        check(c->connect(cm,"p23_native_free_"+std::to_string(i)),"connect initiated"); clients.push_back(std::move(c));
        check(!clients.back()->connect(cm,"p23_duplicate_pending"),"duplicate pending connect rejected");
        check(until(host,clients,[&] { return size(host.view())==i+1 && size(clients.back()->view())==i+1; }),"auth + roster for late join");
        if(i==1) {
            MsgHello hello;hello.displayName="Spoof";hello.participantId="p23_repeat_hello";hello.joinToken=hm.token;
            clients[0]->transport().sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_HELLO,2,hello),true);
            until(host,clients,[] { return false; },80);
            check(size(host.view())==2 && host.view().players()[1]->displayName=="Игрок 2","repeat HELLO cannot acquire another slot");
        }
    }
    const bool ready=until(host,clients,[&] { for(const auto& c:clients) if(size(c->view())!=8)return false;return true; });
    check(ready,"all eight full rosters");
    if(!ready) { for(auto& c:clients)c->stop();host.stop();std::printf("P23_LOBBY_LIVE admission_failed checks=%d failures=%d\n",checks,failures);return 1; }
    check(host.view().players()[0]->displayName=="Хост" && clients[0]->view().players()[7]->displayName=="Игрок 8","Unicode names on real UDP");
    const PosePacket wrongPose{{1,1,1,1000,999,999,300,0,0,123}};
    host.transport().broadcastPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_UI_POSE,1,wrongPose),true);
    host.transport().broadcastPacket(CHANNEL_UNRELIABLE_SEQUENCED,encodePacket(MSG_UI_POSE,2,wrongPose),true);
    const auto badBytes=encodePacket(MSG_UI_POSE,3,wrongPose);
    enet_peer_send(host.transport().getPeerForSlot(1),CHANNEL_UNRELIABLE_SEQUENCED,enet_packet_create(badBytes.data(),badBytes.size(),ENET_PACKET_FLAG_UNSEQUENCED));
    host.transport().broadcastPacket(CHANNEL_DIAGNOSTIC,encodePacket(MSG_UI_POSE,4,wrongPose),false);
    until(host,clients,[] {return false;},80);
    check(!clients[0]->view().localHeight(nowMs()),"pose rejects reliable/wrong channel/unsequenced flag");
    check(host.nativePose({0,1,1,1,100,200,300,0.25f,0},State::Alive),"host authoritative pose publication");
    check(host.nativePose({1,1,1,2,120,220,300,0.5f,0},State::Alive),"remote FullPlayer authoritative pose publication");
    check(until(host,clients,[&] {return clients[0]->view().localHeight(nowMs()).has_value() && clients[0]->view().markers(nowMs(),300,true).size()==1;}),"roster state and remote pose delivered to map model");
    const auto marker=clients[0]->view().markers(nowMs(),300,true);
    check(!marker.empty() && marker[0].slot==0 && marker[0].x==100 && marker[0].yawRadians==0.25f,"host marker exact scalar attribution");
    host.transport().getNetSimulator().setLatencyMs(25);
    check(host.nativePose({0,1,1,2,101,200,300,0.25f,0},State::Alive),"delayed sequenced pose queued");
    check(until(host,clients,[&] {auto v=clients[0]->view().markers(nowMs(),300,true);return !v.empty() && v[0].x==101;}),"latency simulator retains sequenced transport flag");
    host.transport().getNetSimulator().setLatencyMs(0);
    for(const auto& c:clients)check(c->view().messages().empty(),"empty initial chat");
    check(clients[0]->say("Привет 🦂"),"client requests chat");
    check(until(host,clients,[&] { for(const auto& c:clients)if(c->view().messages().size()!=1)return false;return host.view().messages().size()==1; }),"host relays chat to seven clients");
    for(const auto& c:clients) {
        const auto& line=c->view().messages().back();
        check(line.message=="Привет 🦂" && line.displayName=="Игрок 2" && line.slot==1 && line.generation==1,"authenticated sender attribution");
    }
    check(host.say("Host message"),"host chat");
    check(until(host,clients,[&] { for(const auto& c:clients)if(c->view().messages().size()!=2)return false;return true; }),"host chat broadcast");
    auto& first=clients[0]->transport();
    const auto baseline=host.view().messages().size();
    auto forged=Request{100,999,"forged generation"};
    first.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_REQUEST,100,forged),true);
    first.sendPacket(CHANNEL_UNRELIABLE_SEQUENCED,encodePacket(MSG_CHAT_REQUEST,101,Request{101,1,"wrong channel"}),true);
    first.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_REQUEST,102,Request{102,1,"unreliable"}),false);
    first.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_BROADCAST,103,Broadcast{{999,0,1,"Хост","spoof host"}}),true);
    auto trailing=encodePacket(MSG_CHAT_REQUEST,104,Request{104,1,"trailing"});trailing.push_back(1);
    first.sendPacket(CHANNEL_RELIABLE_CONTROL,trailing,true);
    until(host,clients,[] { return false; },120);
    check(host.view().messages().size()==baseline,"forged generation/direction/channel/reliability/trailer rejected");
    // Request replay and per-generation rate bound; accepted message is followed
    // by a duplicate sequence with a different body over the real socket.
    auto& second=clients[1]->transport();
    const auto data=encodePacket(MSG_CHAT_REQUEST,1,Request{1,1,"one only"});
    second.sendPacket(CHANNEL_RELIABLE_CONTROL,data,true);
    second.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_REQUEST,2,Request{1,1,"duplicate"}),true);
    second.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_REQUEST,3,Request{2,1,"flood"}),true);
    check(until(host,clients,[&] { return host.view().messages().size()==baseline+1; }),"first rate-limited message accepted");
    until(host,clients,[] { return false; },100);
    check(host.view().messages().size()==baseline+1 && host.view().messages().back().message=="one only","replay and flood rejected");
    clients[6]->stop();
    check(until(host,clients,[&] { return size(host.view())==7 && size(clients[0]->view())==7; }),"leave visible");
    check(clients[6]->view().messages().empty() && size(clients[6]->view())==0,"leaver clears UI");
    cm.displayName="Игрок 8";
    check(clients[6]->connect(cm,"p23_native_free_7"),"reconnect request with retained key");
    check(until(host,clients,[&] { return size(host.view())==8 && size(clients[6]->view())==8; }),"reconnect roster");
    check(clients[6]->transport().getConnectionGeneration()==2,"new connection generation");
    check(until(host,clients,[&] { return clients[6]->view().messages().size()==host.view().messages().size(); }),"late-join current-generation history replay");
    check(clients[6]->say("New generation"),"fresh generation chat request");
    check(until(host,clients,[&] { return !host.view().messages().empty() && host.view().messages().back().message=="New generation"; }),"fresh chat attribution");
    // Explicit protocol mismatch closes admission, never falls back to 1.12.
    auto older=encodePacket(MSG_CHAT_REQUEST,999,Request{999,2,"old wire"});older[4]=0x12;older[5]=0x01;
    clients[6]->transport().sendPacket(CHANNEL_RELIABLE_CONTROL,older,true);
    check(until(host,clients,[&] { return !clients[6]->error().empty(); }),"v1.12 rejected");
    // Invalid token cannot obtain a roster even after an available slot exists.
    clients[5]->stop();until(host,clients,[&] { return size(host.view())<=6; });
    cm.token="wrong";cm.displayName="Untrusted";
    auto rejected=std::make_unique<LobbyClient>();check(rejected->connect(cm,"p23_bad_token"),"bad token initiated");clients.push_back(std::move(rejected));
    check(until(host,clients,[&] { return !clients.back()->error().empty(); }),"bad token rejected");
    check(size(clients.back()->view())==0,"rejected peer gets no roster");
    host.stop();
    const auto end=nowMs()+3000;
    while(nowMs()<end) {
        bool all=true;for(auto& c:clients) {c->poll();all &= size(c->view())==0;}
        if(all)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for(std::size_t i=0;i<clients.size();++i) {check(size(clients[i]->view())==0,"server shutdown clears each client");if(i)clients[i]->stop();}
    check(size(host.view())==0,"host shutdown clears UI");
    const auto oldSession=host.transport().getSessionId();
    check(host.start(hm,selected) && size(host.view())==1 && host.transport().getSessionId()!=oldSession,"restart uses fresh ledger and session");
    cm.token=hm.token;cm.displayName="Fresh";
    check(clients[0]->connect(cm,"p23_brand_new_participant"),"new connect after remote shutdown without local stop");
    // clearReconnectKey must happen before a different identity reaches HELLO.
    check(clients[0]->transport().getReconnectKey()==0,"reconnect secret cleared on identity change");
    check(until(host,clients,[&] { return size(host.view())==2 && size(clients[0]->view())==2; }),"fresh client after restart");
    check(clients[0]->transport().getConnectionGeneration()==1 && host.view().messages().empty(),"restart has no old generation or history");
    for(auto& c:clients)c->stop();host.stop();
    hm.address="0.0.0.0";
    check(host.start(hm,selected),"explicit any-interface LAN bind");
    // Windows/ENet peer matching cannot use an unassigned loopback alias:
    // wildcard UDP responses may originate from 127.0.0.1. Use an explicitly
    // selected, actually assigned interface for this one-PC connectivity gate.
    char assigned[64]{};const auto length=GetEnvironmentVariableA("P23_TEST_LOCAL_IP",assigned,sizeof(assigned));
    cm.address=length && length<sizeof(assigned)?assigned:"127.0.0.1";
    check(length && length<sizeof(assigned) && ipv4(cm.address) && cm.address!="127.0.0.1","assigned non-loopback interface selected");
    check(clients[0]->connect(cm,"p23_brand_new_participant") && clients[0]->transport().getReconnectKey()==0,"changed endpoint clears reconnect secret");
    check(until(host,clients,[&] {return size(clients[0]->view())==2;}),"any-interface bind reachable through assigned local interface");
    for(auto& c:clients)c->stop();host.stop();
    std::printf("P23_LOBBY_LIVE checks=%d failures=%d native_game=0 real_enet=1\n",checks,failures);return failures?1:0;
}
