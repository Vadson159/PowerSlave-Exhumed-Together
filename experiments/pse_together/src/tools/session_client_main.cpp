// Headless loopback test controller. Uses the accepted v1.10 serializers and
// ENetClientNode; contains no KEX API, native DLL, world loader or renderer.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "enet_client.h"
#include "replication_protocol.h"
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <thread>
#include <algorithm>
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
static FILE* journal{};
static std::uint64_t logUs{};
static long long micros() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }
static void record(const char* event,const char* fmt="",...) {
    const auto begin=micros();
    char detail[1500]{}; va_list args; va_start(args,fmt); vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args); va_end(args);
    SYSTEMTIME t{}; GetSystemTime(&t);
    std::fprintf(journal,"{\"event\":\"%s\",\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"steady_us\":%lld,\"pid\":%lu%s%s}\n",event,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,begin,GetCurrentProcessId(),detail[0]?",":"",detail);
    logUs+=static_cast<std::uint64_t>(micros()-begin);
}
// A wait, not a spin. Older Windows falls back to an ordinary waitable timer,
// then sleep_until if timer setup fails; the steady grid is unchanged.
class Sleeper {
    HANDLE timer{};
public:
    bool highResolution{};
    Sleeper() {
        timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        highResolution=timer!=nullptr;
        if(!timer) timer=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
    }
    ~Sleeper() { if(timer) CloseHandle(timer); }
    void until(Clock::time_point deadline) {
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-Clock::now()).count();
        if(ns<=0) return;
        LARGE_INTEGER due{};due.QuadPart=-((ns+99)/100);
        if(timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) && WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0) return;
        std::this_thread::sleep_until(deadline);
    }
};
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const fs::path run=fs::absolute(argv[1]);
    if(!fs::is_directory(run) || fs::exists(run/"client.jsonl")) return 2;
    if(_wfopen_s(&journal,(run/"client.jsonl").c_str(),L"wx")) return 2;
    // Retain every command/timestamp; batch CRT writes rather than flushing each
    // send. Lifecycle/control records become durable at most 100 ms later.
    static char journalBuffer[65536];std::setvbuf(journal,journalBuffer,_IOFBF,sizeof(journalBuffer));
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
    Sleeper sleeper;
    const auto epoch=Clock::now(),deadline=epoch+180s;
    // Rational 1/60 s slots, calculated from a fixed epoch. Rounding never
    // accumulates from the actual wake time, even after a long scheduler stall.
    const auto grid=[&](std::int64_t slot) { return epoch+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<std::int64_t,std::ratio<1,60>>(slot)); };
    std::int64_t nextSlot=0;
    auto next=epoch,nextControl=epoch,nextPoll=epoch,nextFlush=epoch+100ms;
    std::uint64_t controlReads=0,controlUs=0,pollUs=0,flushUs=0,queueUs=0,skipped=0,loops=0;
    bool wasEligible=false;
    record("scheduler","\"target_hz\":60,\"fixed_grid\":true,\"spin\":false,\"high_resolution_wait\":%s,\"control_period_ms\":8,\"flush_period_ms\":100",sleeper.highResolution?"true":"false");
    while(Clock::now()<deadline) {
        ++loops;
        auto now=Clock::now();
        std::string command;
        if(now>=nextControl) {
            const auto begin=micros();
            { std::ifstream control(run/"client_control.txt");control>>command; }
            ++controlReads;controlUs+=static_cast<std::uint64_t>(micros()-begin);
            nextControl=Clock::now()+8ms;
        }
        if(!command.empty() && command!=last) {
            last=command; record("mode","\"mode\":\"%s\"",command.c_str());
            if(command=="quit") break;
            if(command=="disconnect") { client.disconnect(); baseline=false; }
            else if(command=="reconnect") { client.disconnect(); baseline=false; client.connect(config); mode="neutral"; }
            else if(command=="hold" || command=="neutral" || command=="silent") mode=command;
        }
        now=Clock::now();
        if(now>=nextPoll) {
            const auto begin=micros();client.poll(0);pollUs+=static_cast<std::uint64_t>(micros()-begin);
            nextPoll=Clock::now()+4ms;
        }
        now=Clock::now();
        const bool eligible=baseline && client.isAuthenticated() && mode!="silent";
        if(eligible && !wasEligible) {
            nextSlot=std::chrono::duration_cast<std::chrono::nanoseconds>(now-epoch).count()*60/1000000000;
            next=grid(nextSlot);
        }
        wasEligible=eligible;
        if(eligible && now>=next) {
            const auto dueSlot=std::chrono::duration_cast<std::chrono::nanoseconds>(now-epoch).count()*60/1000000000;
            const auto missed=std::max<std::int64_t>(0,dueSlot-nextSlot);skipped+=missed;
            const float forward=mode=="hold" ? 1.0f : 0.0f;
            pse::MsgInputCommand c; c.slotId=client.getAssignedSlot(); c.connectionGeneration=client.getConnectionGeneration(); c.commandSequence=++sequence; c.clientTick=sequence; c.moveForward=forward;
            const auto generated=micros();
            client.sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED,pse::encodePacket(pse::MSG_INPUT_COMMAND,sequence,c),false);
            queueUs+=static_cast<std::uint64_t>(micros()-generated);
            // 'sent' is local dispatch, not transport acceptance or delivery.
            record("sent","\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"forward\":%.1f,\"generated_us\":%lld,\"grid_slot\":%lld,\"skipped_slots\":%lld",c.slotId,c.connectionGeneration,sequence,forward,generated,static_cast<long long>(dueSlot),static_cast<long long>(missed));
            // Normal nonblocking ENet service sends queued traffic now, with its
            // existing batching/throttle rules. No explicit flush/throttle patch.
            const auto begin=micros();client.poll(0);pollUs+=static_cast<std::uint64_t>(micros()-begin);
            nextPoll=Clock::now()+4ms;
            nextSlot=std::max(dueSlot+1,std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-epoch).count()*60/1000000000+1);
            next=grid(nextSlot);
        }
        now=Clock::now();
        if(now>=nextFlush) { const auto begin=micros();std::fflush(journal);flushUs+=static_cast<std::uint64_t>(micros()-begin);nextFlush=Clock::now()+100ms; }
        auto wake=std::min({nextControl,nextPoll,nextFlush,deadline});
        if(eligible) wake=std::min(wake,next);
        sleeper.until(wake);
    }
    client.disconnect();
    record("scheduler_cost","\"commands\":%u,\"loops\":%llu,\"control_reads\":%llu,\"control_us\":%llu,\"log_us\":%llu,\"flush_us\":%llu,\"poll_us\":%llu,\"dispatch_us\":%llu,\"skipped_slots\":%llu",sequence,loops,controlReads,controlUs,logUs,flushUs,pollUs,queueUs,skipped);
    record("client_exit"); std::fclose(journal);
    return 0;
}
