// Lobby beacon: codec, rejection of damaged/foreign datagrams, and the real UDP loop (Advertiser -> Browser) on this computer.
#include "ui/discovery.hpp"
#include <windows.h>
#include <cstdio>

using namespace pse::discovery;
int checks{}, failures{};
void check(bool b, const char* why) { ++checks; if (!b) { ++failures; std::printf("FAIL %s\n", why); } }

int main() {
    Lobby l; l.host = "Хост Ra"; l.port = 28745; l.players = 1; l.capacity = 2; l.open = true; l.map = "maps/TOMB.MAP";
    const auto bytes = encode(l);
    Lobby back;
    check(decode(bytes.data(), static_cast<int>(bytes.size()), back), "round trip decodes");
    check(back.host == l.host && back.port == l.port && back.players == 1 && back.capacity == 2 && back.open && back.map == l.map, "round trip keeps every field");
    // damaged or foreign datagrams are refused, never repaired
    check(!decode(bytes.data(), static_cast<int>(bytes.size()) - 1, back), "truncated beacon refused");
    check(!decode((bytes + "x").data(), static_cast<int>(bytes.size()) + 1, back), "trailing bytes refused");
    auto wrongMagic = bytes; wrongMagic[0] = 'X';
    check(!decode(wrongMagic.data(), static_cast<int>(wrongMagic.size()), back), "foreign magic refused");
    auto wrongVersion = bytes; wrongVersion[4] = 9;
    check(!decode(wrongVersion.data(), static_cast<int>(wrongVersion.size()), back), "unknown version refused");
    Lobby bad = l; bad.map = "maps/a\"b";
    check(!decode(encode(bad).data(), static_cast<int>(encode(bad).size()), back), "quote in map name refused");
    bad = l; bad.players = 3;
    check(!decode(encode(bad).data(), static_cast<int>(encode(bad).size()), back), "more players than seats refused");
    bad = l; bad.host = std::string("A\x01", 2);
    check(!decode(encode(bad).data(), static_cast<int>(encode(bad).size()), back), "control character in host name refused");
    check(!decode("PSEL", 4, back), "short datagram refused");

    // the real loop: the host announces, the client lists it with the sender's address and the state the host reported
    Browser browser; Advertiser host;
    check(browser.start(), "browser listens on the beacon port");
    check(host.start("Test Host", 28745), "advertiser starts");
    host.set("maps/TOMB.MAP", 1, false);
    bool seen = false;
    for (int i = 0; i < 40 && !seen; ++i) { Sleep(100); for (const auto& x : browser.list()) if (x.host == "Test Host") { seen = true; check(x.port == 28745 && !x.open && !x.address.empty(), "listed lobby: port, not open yet, sender address"); } }
    check(seen, "browser hears the advertiser (UDP broadcast on this computer)");
    host.set("maps/TOMB.MAP", 2, true);
    bool open = false;
    for (int i = 0; i < 40 && !open; ++i) { Sleep(100); for (const auto& x : browser.list()) if (x.host == "Test Host" && x.open && x.players == 2) open = true; }
    check(open, "state change (open, 2 players) reaches the browser");
    host.stop();
    bool gone = false;
    for (int i = 0; i < 80 && !gone; ++i) { Sleep(100); gone = browser.list().empty(); }
    check(gone, "lobby disappears after the host stops announcing");
    check(!interfaces().empty() || true, "interface enumeration does not crash");
    browser.stop();
    std::printf("discovery_contract: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
