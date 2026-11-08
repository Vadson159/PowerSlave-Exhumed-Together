#include "ui/protocol.hpp"
#include "ui/shared_p23.hpp"
#include "ui/opening_text.hpp"
#include <cstdio>
#include <limits>

using namespace pse;
using namespace pse::ui;
int checks{}, failures{};
void check(bool b, const char* why) { ++checks; if (!b) { ++failures; std::printf("FAIL %s\n",why); } }
template<class T> void codec(const T& value, NetMessageType type) {
    const auto wire = encodePacket(type,1,value);
    for (std::size_t length = 0; length < wire.size(); ++length) {
        ByteReader reader(wire.data(),length); MsgHeader h; T v;
        check(!(h.deserialize(reader) && complete(reader,v)),"truncated packet");
    }
    ByteReader reader(wire.data(),wire.size()); MsgHeader h; T v;
    check(h.deserialize(reader) && h.version == 0x113 && h.msgType == type && complete(reader,v),"wire roundtrip");
    std::vector<std::uint8_t> again; ByteWriter writer(again); v.serialize(writer);
    check(again == std::vector<std::uint8_t>(wire.begin()+11,wire.end()),"exact payload roundtrip");
    auto trailing = wire; trailing.push_back(0); ByteReader tr(trailing.data(),trailing.size());
    check(h.deserialize(tr) && !complete(tr,v),"trailing garbage");
}
int main() {
    check(!nativeCapture(false,true,true) && !nativeCapture(true,false,true) && nativeCapture(true,true,true),"public UI capture cannot grant native Player writes before gameplay permit");
    check(name("Аня") && name("Player 8"),"Unicode names");
    check(!name("") && !name("   ") && !name(std::string(32,'x')),"invalid names");
    check(participant("client_2-ABC") && !participant("a\nb") && !participant("Аня"),"participant ID log safety");
    const std::string nul("hi\0bad",6);
    for (const auto& s : std::vector<std::string>{nul,"a\nb","a\rb","\t","\xC0\xAF","\xED\xA0\x80","\xF4\x90\x80\x80","\xE2\x82","\xEF\xBB\xBF","\xE2\x80\xAE","\x80","\x7f"})
        check(!chat(s),"unsafe Unicode");
    std::string unicode; for (int i=0;i<90;++i) unicode += "🦂";
    check(unicode.size()==360 && chat(unicode) && !chat(unicode+"x"),"90 scalar 360 byte boundary");
    check(chat(std::string(90,'a')) && !chat(std::string(91,'a')),"ASCII chat boundary");
    check(port("1")==1 && port("65535")==65535,"valid ports");
    for (const char* s : {"0","65536","-1","1.0"," 1","","9999999999999999999"}) check(!port(s),"invalid port");
    check(ipv4("127.0.0.1") && ipv4("26.10.0.1"),"LAN and Radmin IPv4");
    for (const char* s : {"0.0.0.0","255.255.255.255","1.2.3.","1.2.3.256","01.2.3.4","1.2.3.4.5","127.0.0.1:1"}) check(!ipv4(s),"invalid address");
    Menu menu; menu.host=true; menu.displayName="Host";
    check(menu.valid(),"host form"); menu.host=false; menu.address="bad"; check(!menu.valid(),"join form");
    Request request{1,2,"Привет 🦂"}; codec(request,MSG_CHAT_REQUEST);
    PosePacket position{{1,2,55,123,-7420,-6775,1152,0.75f,0}};codec(position,MSG_UI_POSE);
    MsgWelcome lobbyWelcome; lobbyWelcome.mapName="LOBBY";lobbyWelcome.sessionId=123;lobbyWelcome.assignedSlot=1;lobbyWelcome.reconnectKey=1;
    codec(lobbyWelcome,MSG_WELCOME);
    MsgWelcome nativeWelcome=lobbyWelcome;nativeWelcome.mapName="TOMB";nativeWelcome.mapStructure={1,123,456};codec(nativeWelcome,MSG_WELCOME);
    auto rejectWelcome=[](const std::vector<std::uint8_t>& bytes) {
        ByteReader r(bytes.data(),bytes.size());MsgHeader h;MsgWelcome w;
        return !h.deserialize(r) || !complete(r,w);
    };
    const auto welcomeBytes=encodePacket(MSG_WELCOME,1,lobbyWelcome);
    for (const auto offset : {11,17,37}) {
        auto bytes=welcomeBytes; bytes[static_cast<std::size_t>(offset)]=offset==37?2:0;
        check(rejectWelcome(bytes),"invalid Welcome version/slot/stage");
    }
    auto bytes=welcomeBytes;bytes[17]=8;check(rejectWelcome(bytes),"invalid Welcome slot8");
    for(const std::size_t offset : {13u,18u,22u}) {
        bytes=welcomeBytes;for(std::size_t i=0;i<4;++i)bytes[offset+i]=0;
        check(rejectWelcome(bytes),"invalid Welcome session/generation/epoch");
    }
    bytes=encodePacket(MSG_WELCOME,1,nativeWelcome);bytes[36]=0;
    check(rejectWelcome(bytes),"native TOMB cannot use lobby flag");
    bytes=encodePacket(MSG_WELCOME,1,nativeWelcome);
    for(std::size_t i=38;i<46;++i)bytes[i]=0;
    check(rejectWelcome(bytes),"native structure cannot be zero");
    Broadcast broadcast{{123,7,42,"Аня","Привет"}}; codec(broadcast,MSG_CHAT_BROADCAST);
    Roster roster{1,55,{}};
    for (std::uint8_t s=0;s<8;++s) roster.entries.push_back({s,1,"P"+std::to_string(s+1),State::Alive,static_cast<std::uint16_t>(s*100)});
    codec(roster,MSG_UI_ROSTER);
    auto bad = roster; bad.entries[7].slot=0; check(!bad.valid(),"duplicate slot");
    bad=roster; bad.entries[7].generation=0; check(!bad.valid(),"generation zero");
    bad=roster; bad.entries[7].state=static_cast<State>(99); check(!bad.valid(),"invalid state");
    bad=roster; bad.entries.erase(bad.entries.begin()); check(!bad.valid(),"host absent");
    std::vector<std::uint8_t> invalidBytes; ByteWriter invalidWriter(invalidBytes);
    bool threw=false; try { Request{1,1,std::string(91,'a')}.serialize(invalidWriter); } catch(const std::invalid_argument&) { threw=true; }
    check(threw,"serializer rejects rather than truncates");
    nativeWelcome.mapStructure={};threw=false;
    try { nativeWelcome.serialize(invalidWriter); } catch(const std::invalid_argument&) { threw=true; }
    check(threw,"native welcome never fabricates missing identity");
    Model model; model.reset(3);
    check(model.roster(1,55,roster.entries),"full eight roster");
    check(model.admitted(55,3,1) && !model.admitted(54,3,1) && !model.admitted(55,3,2) && !model.admitted(55,0,1),"UI roster bound to native admission tuple");
    check(model.notices().size()==8 && !model.roster(1,55,roster.entries),"join notices only once");
    check(!model.roster(2,55,bad.entries) && model.rosterSequence()==1,"transactional invalid roster");
    check(model.message({1,0,1,"P1","hello"}),"host chat");
    check(!model.message({2,0,2,"P1","fake"}) && !model.message({2,0,1,"fake","text"}),"spoof generation/name");
    check(!model.message({1,0,1,"P1","repeat"}),"broadcast dedup");
    for(std::uint64_t seq=2;seq<=100;++seq) check(model.message({seq,7,1,"P8","text"}),"bounded history insert");
    check(model.messages().size()==64 && model.messages().front().sequence==37,"history bounded FIFO");
    for(std::uint8_t slot=0;slot<8;++slot) check(model.pose({slot,1,55,1,100.0f+slot,200,0,1,1000}),"authoritative poses");
    check(model.markers(1000,0,true).size()==7,"client sees seven allies including host");
    check(model.markers(1000,0,true,1000000000).empty(),"receive timestamp cannot refresh missing host sample time");
    check(model.pose({0,1,55,2,100,200,0,1,1000,1000000000}),"explicit host sample clock");
    check(model.markers(1000,0,true,1000000000).size()==1 && model.markers(1000,0,true,2000000001).empty(),"host marker rejects stale sample in its own clock");
    check(model.markers(1000,0,true).size()==7 && model.markers(2001,0,true).empty(),"client receive TTL independent of remote host clock");
    check(model.markers(1000,0,false).empty() && model.markers(2001,0,true).empty(),"map closed and stale");
    check(!model.pose({1,1,54,2,1,2,3,1,1001}),"stale map epoch");
    check(!model.pose({1,2,55,2,1,2,3,1,1001}),"stale generation");
    check(!model.pose({1,1,55,1,1,2,3,1,1001}),"pose out of order");
    check(!model.pose({1,1,55,2,(std::numeric_limits<float>::quiet_NaN)(),2,3,1,1001}),"nonfinite pose");
    auto changed = roster.entries; changed[1].state=State::Dead; changed[2].state=State::Waiting;
    check(model.roster(2,55,changed),"status changes");
    check(model.pose({1,1,55,2,1,2,-257,0.5f,1001}),"lower ally");
    auto markers=model.markers(1001,0,true);
    check(markers.size()==6 && markers[1].rgba==std::array<std::uint8_t,4>{128,128,128,180} && markers[1].radius==12,"dead lower alpha and scale");
    changed[1].generation=2;
    check(model.roster(3,55,changed) && model.markers(1001,0,true).size()==5,"reconnect clears old pose");
    changed.erase(changed.begin()+7);
    check(model.roster(4,55,changed) && model.markers(1001,0,true).size()==4 && model.notices().back()=="P8 left","disconnect clears marker");
    check(model.roster(5,56,changed) && model.markers(1001,0,true).empty(),"map change clears all poses");
    model.reset(); check(model.messages().empty() && model.notices().empty() && model.markers(1001,0,true).empty(),"session loss clears UI");
    OpeningText opening;
    opening.open();check(!opening.accept(10),"opening T character before keyup suppressed");
    opening.release(11);opening.nextKey();check(opening.accept(11),"legitimate first physical key at same timestamp preserved");
    opening.open();opening.release(20);check(!opening.accept(20),"queued opening character after keyup suppressed");
    opening.nextKey();check(opening.accept(20),"legitimate first key after queued opener preserved");
    opening.open();opening.release(30);check(opening.accept(63),"direct text beyond bounded opening tail preserved");
    opening.open();opening.reset();check(opening.accept(0),"focus reset clears opening suppression");
    opening.open();opening.release(0xfffffff0);check(!opening.accept(0),"SDL timestamp wrap inside opening tail");
    ChatLimit limit;
    check(limit.accept(1,0) && !limit.accept(1,500) && !limit.accept(2,499) && !limit.accept(2,500) && limit.accept(3,500),"rate and request replay");
    check(!limit.accept(4,499) && limit.accept(5,1000),"clock rollback rejected");
    std::printf("P23_UI_CONTRACT checks=%d failures=%d\n",checks,failures); return failures?1:0;
}
