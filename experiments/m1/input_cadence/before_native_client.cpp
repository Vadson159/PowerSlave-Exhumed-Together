// Headless loopback test controller. Uses the accepted v1.10 serializers and
// ENetClientNode; contains no KEX API, native DLL, world loader or renderer.
#include "enet_client.h"
#include "replication_protocol.h"
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstdarg>
namespace fs=std::filesystem;
static FILE* journal{};
static void record(const char* event,const char* fmt="",...) {
    char detail[1500]{}; va_list args; va_start(args,fmt); vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args); va_end(args);
    SYSTEMTIME t{}; GetSystemTime(&t);
    std::fprintf(journal,"{\"event\":\"%s\",\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"pid\":%lu%s%s}\n",event,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),detail[0]?",":"",detail);
    std::fflush(journal);
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const fs::path run=fs::absolute(argv[1]);
    if(!fs::is_directory(run) || fs::exists(run/"client.jsonl")) return 2;
    if(_wfopen_s(&journal,(run/"client.jsonl").c_str(),L"wx")) return 2;
    pse::ENetClientNode client;
    pse::ClientConfig config; config.hostPort=28745; config.joinToken="M12A_Loopback"; config.participantId="m12a_native_p2";
    bool baseline=false;
    client.onWelcomeCallback=[&](const pse::MsgWelcome& w) {
        baseline=false; config.reconnectKey=w.reconnectKey;
        record("welcome","\"slot\":%u,\"generation\":%u,\"protocol\":%u",w.assignedSlot,w.connectionGeneration,w.protocolVersion);
    };
    client.onDisconnectedCallback=[&] { baseline=false; record("disconnected"); };
    client.setCustomPacketHandler([&](std::uint8_t type,pse::ByteReader& r,std::uint32_t) {
        if(type==pse::MSG_WORLD_BASELINE) {
            pse::MsgWorldBaseline b;
            if(!b.deserialize(r) || b.totalBatches!=1 || b.mapName!="M12A_INPUT_ONLY" || !b.entities.empty()) return false;
            baseline=true; record("control_baseline","\"epoch\":%u,\"entities\":0,\"headless\":true",b.mapEpoch);
            return true;
        }
        return false;
    });
    if(!client.connect(config)) return 3;
    std::string mode="neutral",last;
    std::uint32_t sequence=0;
    auto next=GetTickCount64(); const auto deadline=next+180000;
    while(GetTickCount64()<deadline) {
        std::ifstream control(run/"client_control.txt"); std::string command; control>>command;
        if(!command.empty() && command!=last) {
            last=command; record("mode","\"mode\":\"%s\"",command.c_str());
            if(command=="quit") break;
            if(command=="disconnect") { client.disconnect(); baseline=false; }
            else if(command=="reconnect") { client.disconnect(); baseline=false; client.connect(config); mode="neutral"; }
            else if(command=="hold" || command=="neutral" || command=="silent") mode=command;
        }
        client.poll(1);
        const auto now=GetTickCount64();
        if(baseline && client.isAuthenticated() && now>=next && mode!="silent") {
            const float forward=mode=="hold" ? 1.0f : 0.0f;
            pse::MsgInputCommand c; c.slotId=client.getAssignedSlot(); c.connectionGeneration=client.getConnectionGeneration(); c.commandSequence=++sequence; c.clientTick=sequence; c.moveForward=forward;
            client.sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED,pse::encodePacket(pse::MSG_INPUT_COMMAND,sequence,c),false);
            record("sent","\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"forward\":%.1f",c.slotId,c.connectionGeneration,sequence,forward);
            next=now+16;
        }
        Sleep(1);
    }
    client.disconnect(); record("client_exit"); std::fclose(journal);
    return 0;
}
