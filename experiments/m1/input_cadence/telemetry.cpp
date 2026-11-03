// Linked only to standalone QA executables, never to the production DLL/client.
#include <enet/enet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
struct Telemetry {
    std::mutex lock;std::vector<std::string> records;
    ~Telemetry() {
        FILE* f{};fopen_s(&f,"qa.jsonl","wx");if(!f)return;
        for(const auto& s:records)std::fputs(s.c_str(),f);std::fclose(f);
    }
    void add(std::string s) { std::lock_guard<std::mutex> guard(lock);if(records.size()>=1000000)std::abort();records.push_back(std::move(s)); }
} telemetry;
static long long nowUs(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::string hex(const unsigned char* p,size_t n){std::string out;out.reserve(n*2);const char* digits="0123456789abcdef";for(size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;}
extern "C" int qa_flush_enabled(){static const bool enabled=std::getenv("PSE_QA_FLUSH")!=nullptr;return enabled;}
extern "C" void qa_packet(const char* kind,const ENetPacket* b,const ENetPeer* p,int r) {
    if(!b || b->dataLength!=45 || b->data[6]!=0x10)return;
    char prefix[400]{};sprintf_s(prefix,"{\"kind\":\"%s\",\"us\":%lld,\"result\":%d,\"flags\":%u,\"throttle\":%u,\"limit\":%u,\"rtt\":%u,\"data\":\"",kind,nowUs(),r,b->flags,p?p->packetThrottle:0,p?p->packetThrottleLimit:0,p?p->roundTripTime:0);
    telemetry.add(std::string(prefix)+hex(b->data,b->dataLength)+"\"}\n");
}
extern "C" void qa_socket(const ENetBuffer* b,size_t n,int r) {
    std::string bytes;size_t size=0;for(size_t i=0;i<n;++i){size+=b[i].dataLength;bytes+=hex(static_cast<const unsigned char*>(b[i].data),b[i].dataLength);}
    char prefix[180]{};sprintf_s(prefix,"{\"kind\":\"udp_send\",\"us\":%lld,\"result\":%d,\"bytes\":%zu,\"data\":\"",nowUs(),r,size);
    telemetry.add(std::string(prefix)+bytes+"\"}\n");
}
