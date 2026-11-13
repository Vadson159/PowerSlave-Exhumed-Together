#pragma once
// Прямое подключение по IP через интернет (без Radmin VPN): хост просит свой роутер открыть UDP-порт игры (UPnP IGD) и узнаёт
// у него внешний IP — его друг вводит в F7 в поле адреса. Перенесено из кооп-мода PowerSlave EX 2015 года (net_udp.cpp, UPnPThread),
// где так играли по интернету: SSDP M-SEARCH -> описание роутера (LOCATION) -> controlURL службы WANIPConnection/WANPPPConnection ->
// SOAP AddPortMapping (UDP, тот же внешний и внутренний порт, на адрес, с которого мы достучались до роутера) -> GetExternalIPAddress.
// При выходе из игры — DeletePortMapping. Всё в своём потоке: игра не ждёт роутер. PSE_UPNP=0 выключает.
// Если роутер не ответил или отказал — порт открывают руками (UDP, не TCP); если внешний адрес роутера «серый» (10.x, 100.64-127.x,
// 172.16-31.x, 192.168.x) — роутер сам за NAT провайдера, прямое подключение невозможно, остаётся Radmin VPN.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace pse::upnp {
enum class State : int { Idle, Working, Open, Failed };

inline bool privateIpv4(const std::string& ip) {
    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) return true;
    const unsigned a = addr.S_un.S_un_b.s_b1, b = addr.S_un.S_un_b.s_b2;
    return a == 10 || a == 127 || (a == 100 && b >= 64 && b < 128) || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) || (a == 169 && b == 254) || a == 0;
}

class PortMapper {
    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> stopping_{};
    mutable std::mutex mutex_;
    std::string message_, external_, local_, control_, service_;
    std::uint16_t port_{};

    void say(const std::string& m) { std::lock_guard lock(mutex_); message_ = m; }

    static bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path) {
        auto p = url.find("://"); p = p == std::string::npos ? 0 : p + 3;
        auto slash = url.find('/', p); if (slash == std::string::npos) slash = url.size();
        const auto colon = url.find(':', p);
        const auto hostEnd = colon != std::string::npos && colon < slash ? colon : slash;
        if (hostEnd <= p || hostEnd - p > 63) return false;
        host = url.substr(p, hostEnd - p);
        port = hostEnd == colon ? std::atoi(url.c_str() + colon + 1) : 80;
        path = slash < url.size() ? url.substr(slash) : "/";
        return port > 0 && port < 65536;
    }
    // One HTTP/1.0 request (3 s timeouts). Body of a 2xx answer, or false. Remembers the local address that reaches the router:
    // that is where the forward must point (not a VPN adapter).
    bool http(const std::string& url, const char* soapAction, const std::string* body, std::string& out) {
        std::string host, path; int port = 0;
        if (!splitUrl(url, host, port, path)) return false;
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return false;
        const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        DWORD timeout = 3000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
        sockaddr_in me{}; int meLen = sizeof(me);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &meLen) == 0) {
            char ip[16]{}; inet_ntop(AF_INET, &me.sin_addr, ip, sizeof(ip)); std::lock_guard lock(mutex_); local_ = ip;
        }
        std::string req;
        const std::string hostLine = host + ":" + std::to_string(port);
        if (body) req = "POST " + path + " HTTP/1.0\r\nHost: " + hostLine + "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" +
                        soapAction + "\"\r\nContent-Length: " + std::to_string(body->size()) + "\r\nConnection: close\r\n\r\n" + *body;
        else req = "GET " + path + " HTTP/1.0\r\nHost: " + hostLine + "\r\nConnection: close\r\n\r\n";
        ::send(s, req.data(), static_cast<int>(req.size()), 0);
        out.clear(); char buf[4096];
        for (int r; out.size() < 65536 && (r = ::recv(s, buf, sizeof(buf), 0)) > 0;) out.append(buf, static_cast<std::size_t>(r));
        closesocket(s);
        if (out.size() < 12 || out[9] != '2') return false; // "HTTP/1.x 2xx": SOAP faults are 500
        const auto headerEnd = out.find("\r\n\r\n");
        if (headerEnd != std::string::npos) out.erase(0, headerEnd + 4);
        return true;
    }
    bool soap(const char* action, const std::string& args, std::string& out) {
        std::string service, control; { std::lock_guard lock(mutex_); service = service_; control = control_; }
        const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + std::string(action) +
            " xmlns:u=\"urn:schemas-upnp-org:service:" + service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
        const std::string soapAction = "urn:schemas-upnp-org:service:" + service + "#" + action;
        return http(control, soapAction.c_str(), &body, out);
    }
    bool findGateway() {
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) { say("UPnP: no socket"); return false; }
        DWORD timeout = 2500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        // The search must leave through the internet-facing adapter, not Radmin/Hamachi/ZeroTier: the local address that routes to a
        // public one (a connected UDP socket sends nothing).
        if (const SOCKET probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); probe != INVALID_SOCKET) {
            sockaddr_in pub{}; pub.sin_family = AF_INET; pub.sin_port = htons(53); inet_pton(AF_INET, "8.8.8.8", &pub.sin_addr);
            sockaddr_in local{}; int localLen = sizeof(local);
            if (::connect(probe, reinterpret_cast<sockaddr*>(&pub), sizeof(pub)) == 0 && getsockname(probe, reinterpret_cast<sockaddr*>(&local), &localLen) == 0) {
                local.sin_port = 0; ::bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local));
                setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local.sin_addr), sizeof(local.sin_addr));
            }
            closesocket(probe);
        }
        static constexpr char search[] = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                                         "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
        std::string location; char buf[4096];
        for (int tries = 0; tries < 2 && location.empty() && !stopping_.load(); ++tries) {
            sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(1900); inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
            ::sendto(s, search, static_cast<int>(sizeof(search) - 1), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
            for (;;) {
                sockaddr_in from{}; int fromLen = sizeof(from);
                const int r = ::recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (r <= 0) break;
                buf[r] = 0; std::string answer(buf, static_cast<std::size_t>(r)), lower = answer;
                for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                auto at = lower.find("\nlocation:"); if (at == std::string::npos) continue;
                at += 10; while (at < answer.size() && answer[at] == ' ') ++at;
                const auto end = answer.find_first_of("\r\n", at);
                location = answer.substr(at, end == std::string::npos ? std::string::npos : end - at);
                break;
            }
        }
        closesocket(s);
        if (location.empty()) { say("UPnP: the router did not answer (UPnP off in the router?)"); return false; }
        std::string description;
        if (!http(location, nullptr, nullptr, description)) { say("UPnP: could not read the router description"); return false; }
        for (const char* service : {"WANIPConnection:2", "WANPPPConnection:2", "WANIPConnection:1", "WANPPPConnection:1"}) {
            const auto svc = description.find(std::string("urn:schemas-upnp-org:service:") + service + "<");
            if (svc == std::string::npos) continue;
            auto ctl = description.find("<controlURL>", svc); if (ctl == std::string::npos) continue;
            ctl += 12; const auto end = description.find('<', ctl); if (end == std::string::npos) continue;
            std::string control = description.substr(ctl, end - ctl);
            if (control.rfind("http", 0) != 0) { // relative to scheme://host:port of the description
                const auto p = location.find("://"); const auto slash = p == std::string::npos ? std::string::npos : location.find('/', p + 3);
                control = location.substr(0, slash) + (control.empty() || control[0] != '/' ? "/" : "") + control;
            }
            std::lock_guard lock(mutex_); control_ = control; service_ = service; return true;
        }
        say("UPnP: the router has no WAN connection service"); return false;
    }
    void run() {
        WSADATA d{}; if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { say("UPnP: winsock"); state_ = State::Failed; return; }
        if (!findGateway()) { state_ = State::Failed; return; }
        const std::string p = std::to_string(port_);
        std::string out, local; { std::lock_guard lock(mutex_); local = local_; }
        bool added = false;
        for (const char* lease : {"0", "86400"}) { // some routers take only permanent mappings, IGDv2 ones only limited
            if (stopping_.load()) break;
            const std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
                "<NewInternalPort>" + p + "</NewInternalPort><NewInternalClient>" + local + "</NewInternalClient><NewEnabled>1</NewEnabled>"
                "<NewPortMappingDescription>PowerSlave Exhumed Together</NewPortMappingDescription><NewLeaseDuration>" + lease + "</NewLeaseDuration>";
            if ((added = soap("AddPortMapping", args, out))) break;
        }
        if (!added) { say("UPnP: the router refused to open UDP " + p); state_ = State::Failed; return; }
        std::string ip;
        if (soap("GetExternalIPAddress", "", out)) {
            auto at = out.find("<NewExternalIPAddress>");
            if (at != std::string::npos) { at += 22; const auto end = out.find('<', at); ip = out.substr(at, end == std::string::npos ? 0 : end - at); }
        }
        { std::lock_guard lock(mutex_); external_ = ip; }
        state_ = State::Open; // Open before the message: stop() must delete the mapping whatever the message says
        if (ip.empty()) say("UPnP: router opened UDP " + p + " -> " + local + " (public IP unknown)");
        else if (privateIpv4(ip)) say("Router WAN " + ip + " is private: provider NAT, direct IP impossible - use Radmin VPN");
        else say("Router opened UDP " + p + " -> " + local + " (UPnP)");
    }
public:
    ~PortMapper() { if (thread_.joinable()) thread_.detach(); }
    bool start(std::uint16_t port) {
        if (state_.load() != State::Idle || !port) return false;
        char v[8]{};
        if (GetEnvironmentVariableA("PSE_UPNP", v, sizeof(v)) && v[0] == '0') { say("UPnP off (PSE_UPNP=0)"); state_ = State::Failed; return false; }
        port_ = port; state_ = State::Working; say("UPnP: asking the router to open UDP " + std::to_string(port) + "...");
        thread_ = std::thread([this] { run(); });
        return true;
    }
    // Quitting: close the router port again (one request, at most 3 s). A search still running is left to the process exit: joining it
    // could hold the quitting game for ~15 s when the router stays silent (the object lives to process exit, as the whole UI context).
    void stop() {
        stopping_.store(true);
        if (state_.load() == State::Working) { if (thread_.joinable()) thread_.detach(); return; }
        if (thread_.joinable()) thread_.join();
        if (state_.load() == State::Open) {
            std::string out;
            soap("DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port_) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", out);
        }
        state_ = State::Idle;
    }
    State state() const { return state_.load(); }
    std::string message() const { std::lock_guard lock(mutex_); return message_; }
    // Public address to tell a friend (empty while unknown or when the router itself is behind provider NAT).
    std::string external() const { std::lock_guard lock(mutex_); return state_.load() == State::Open && !privateIpv4(external_) ? external_ : std::string(); }
};
} // namespace pse::upnp
