#pragma once
// Лобби в локальной сети (LAN, Radmin VPN, Hamachi, ZeroTier ...): хост сам объявляет игру короткими UDP-маяками, клиент в меню мода
// видит всё, что слышит. Адрес для подключения — адрес отправителя маяка (его знает сам сокет), в пакете его нет.
//
// Маяк (раз в секунду): "PSEL", u16 версия(1), u16 порт игры, u8 игроков, u8 мест, u8 открыто (1 = хост уже в уровне и принимает
// подключения), u8 длина имени + имя хоста, u8 длина карты + имя карты. Рассылка: 255.255.255.255 и направленный broadcast каждого
// поднятого IPv4-интерфейса (так маяк выходит и в Radmin-адаптер 26.x.x.x, а не только в основной). Игровой трафик — ENet, как раньше.
// Всё ограничено по размеру; чужие строки проходят ту же проверку UTF-8, что имена игроков.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "model.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace pse::discovery {
constexpr std::uint16_t BeaconPort = 28746;
constexpr std::size_t MaxLobbies = 16;
constexpr std::uint64_t ExpireMs = 5000, PeriodMs = 1000;

struct Lobby {
    std::string host, address, map;
    std::uint16_t port{};
    std::uint8_t players{}, capacity{};
    bool open{};
    std::uint64_t seenMs{};
};

inline std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline bool mapText(const std::string& s) {
    if (s.size() > 40) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7e && c != '"' && c != '\\'; });
}
inline std::string encode(const Lobby& l) {
    std::string out("PSEL", 4);
    auto u16 = [&](std::uint16_t v) { out.push_back(static_cast<char>(v & 0xff)); out.push_back(static_cast<char>(v >> 8)); };
    u16(1); u16(l.port);
    out.push_back(static_cast<char>(l.players)); out.push_back(static_cast<char>(l.capacity)); out.push_back(l.open ? 1 : 0);
    out.push_back(static_cast<char>(l.host.size())); out += l.host;
    out.push_back(static_cast<char>(l.map.size())); out += l.map;
    return out;
}
// false — не наш/битый маяк (строки не обрезаются в другое значение, а отвергаются)
inline bool decode(const char* data, int size, Lobby& out) {
    if (size < 14 || std::memcmp(data, "PSEL", 4) != 0) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(data);
    if ((p[4] | (p[5] << 8)) != 1) return false;
    out = {};
    out.port = static_cast<std::uint16_t>(p[6] | (p[7] << 8)); out.players = p[8]; out.capacity = p[9]; out.open = p[10] == 1;
    std::size_t at = 11;
    const std::size_t nameLen = p[at++];
    if (at + nameLen + 1 > static_cast<std::size_t>(size)) return false;
    out.host.assign(data + at, nameLen); at += nameLen;
    const std::size_t mapLen = p[at++];
    if (at + mapLen != static_cast<std::size_t>(size)) return false;
    out.map.assign(data + at, mapLen);
    return out.port && out.capacity >= 1 && out.capacity <= ui::Capacity && out.players <= out.capacity && ui::name(out.host) && mapText(out.map);
}

struct Interface { std::string address, broadcast; };
// Поднятые IPv4-интерфейсы без loopback: свой адрес (показать хосту) и направленный broadcast.
inline std::vector<Interface> interfaces() {
    std::vector<Interface> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) { buffer.resize(size); list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size); }
    if (rc != NO_ERROR) return out;
    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto ip = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr;
            const unsigned prefix = u->OnLinkPrefixLength;
            if (prefix == 0 || prefix > 30) continue;
            const std::uint32_t mask = htonl(~0u << (32 - prefix));
            in_addr addr{}, bcast{};
            addr.s_addr = ip; bcast.s_addr = ip | ~mask;
            char a1[16]{}, a2[16]{};
            inet_ntop(AF_INET, &addr, a1, sizeof(a1)); inet_ntop(AF_INET, &bcast, a2, sizeof(a2));
            if (std::strncmp(a1, "169.254.", 8) == 0) continue; // link-local без сети
            out.push_back({a1, a2});
        }
    }
    return out;
}

inline bool winsock() {
    static const bool ok = [] { WSADATA d{}; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    return ok;
}

// Хост: поток шлёт маяк, пока объект жив. set() — свежие сведения (карта, игроки, открыто) от Sim-потока.
class Advertiser {
    std::thread thread_;
    std::atomic<bool> stop_{}, running_{};
    std::mutex mutex_;
    Lobby lobby_;
    void run() {
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return;
        BOOL yes = TRUE; setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
        while (!stop_.load()) {
            Lobby copy; { std::lock_guard lock(mutex_); copy = lobby_; }
            const auto packet = encode(copy);
            std::vector<std::string> targets{"255.255.255.255"};
            for (const auto& i : interfaces()) if (std::find(targets.begin(), targets.end(), i.broadcast) == targets.end()) targets.push_back(i.broadcast);
            for (const auto& t : targets) {
                sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(BeaconPort);
                if (inet_pton(AF_INET, t.c_str(), &to.sin_addr) == 1) sendto(s, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
            }
            for (unsigned i = 0; i < PeriodMs / 50 && !stop_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        closesocket(s);
    }
public:
    ~Advertiser() { stop(); }
    bool start(const std::string& host, std::uint16_t port, std::uint8_t capacity = static_cast<std::uint8_t>(ui::Capacity)) {
        if (running_.exchange(true) || !winsock() || !ui::name(host)) { return false; }
        { std::lock_guard lock(mutex_); lobby_ = {}; lobby_.host = host; lobby_.port = port; lobby_.capacity = capacity; lobby_.players = 1; }
        stop_ = false; thread_ = std::thread([this] { run(); }); return true;
    }
    void set(const std::string& map, std::uint8_t players, bool open) {
        std::lock_guard lock(mutex_);
        lobby_.map = mapText(map) ? map : std::string(); lobby_.players = (std::min)(players, lobby_.capacity); lobby_.open = open;
    }
    void stop() { if (!running_.exchange(false)) return; stop_ = true; if (thread_.joinable()) thread_.join(); }
    bool running() const { return running_.load(); }
};

// Клиент: слушает порт маяков, помнит лобби не дольше ExpireMs.
class Browser {
    std::thread thread_;
    std::atomic<bool> stop_{}, running_{};
    std::mutex mutex_;
    std::vector<Lobby> lobbies_;
    void run() {
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return;
        BOOL yes = TRUE; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in any{}; any.sin_family = AF_INET; any.sin_port = htons(BeaconPort); any.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) { closesocket(s); return; }
        DWORD timeout = 300; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        char buf[300];
        while (!stop_.load()) {
            sockaddr_in from{}; int len = sizeof(from);
            const int n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
            const auto now = nowMs();
            std::lock_guard lock(mutex_);
            lobbies_.erase(std::remove_if(lobbies_.begin(), lobbies_.end(), [&](const Lobby& l) { return now - l.seenMs > ExpireMs; }), lobbies_.end());
            Lobby l;
            if (n <= 0 || !decode(buf, n, l)) continue;
            char ip[16]{}; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            l.address = ip; l.seenMs = now;
            auto it = std::find_if(lobbies_.begin(), lobbies_.end(), [&](const Lobby& o) { return o.address == l.address && o.port == l.port; });
            if (it != lobbies_.end()) *it = l; else if (lobbies_.size() < MaxLobbies) lobbies_.push_back(l);
        }
        closesocket(s);
    }
public:
    ~Browser() { stop(); }
    bool start() {
        if (running_.exchange(true) || !winsock()) return false;
        stop_ = false; thread_ = std::thread([this] { run(); }); return true;
    }
    std::vector<Lobby> list() {
        const auto now = nowMs();
        std::vector<Lobby> out;
        { std::lock_guard lock(mutex_); for (const auto& l : lobbies_) if (now - l.seenMs <= ExpireMs) out.push_back(l); }
        std::sort(out.begin(), out.end(), [](const Lobby& a, const Lobby& b) { return a.host != b.host ? a.host < b.host : a.address < b.address; });
        return out;
    }
    void stop() { if (!running_.exchange(false)) return; stop_ = true; if (thread_.joinable()) thread_.join(); }
};
} // namespace pse::discovery
