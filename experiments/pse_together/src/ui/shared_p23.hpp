#pragma once
#include "model.hpp"
#include <atomic>
#include <mutex>
#include <deque>

namespace pse::ui {
constexpr bool nativeCapture(bool nativePermit,bool gameplay,bool capture) {return nativePermit && gameplay && capture;}
// Process-lifetime retained object: SIM, NETWORK and render never share engine
// pointers. No callbacks while holding this mutex. All queues have hard bounds.
struct Shared {
    using Logger=void(*)(const char*,const char*,...);
    std::atomic<Logger> eventLogger{};
    std::mutex mutex;
    Model view;
    Menu menu;
    std::deque<std::string> chatOut;
    std::atomic<bool> interfaceEnabled{},permitted{}, requested{}, capture{}, ready{}, stopping{};
    std::atomic<bool> paused{}; // the game's Esc menu is open: gameplay state, but the local Player is not updated (set by the Sim tick)
    bool host{};
    void publish(const Model& m) {
        std::optional<Chat> message;
        {std::lock_guard lock(mutex);
            const auto before=view.messages().empty()?0:view.messages().back().sequence;
            view=m;if(!m.messages().empty() && m.messages().back().sequence>before)message=m.messages().back();
        }
        if(message) if(auto log=eventLogger.load(std::memory_order_acquire)) {
            auto escape=[](const std::string& s) {std::string out;for(char ch:s) {if(ch=='"'||ch=='\\')out+='\\';out+=ch;}return out;};
            const auto who=escape(message->displayName),text=escape(message->message);
            log("p23_chat_received","\"host_sequence\":%llu,\"slot\":%u,\"generation\":%u,\"name\":\"%s\",\"text\":\"%s\"",message->sequence,message->slot,message->generation,who.c_str(),text.c_str());
        }
    }
    Model read() { std::lock_guard lock(mutex);return view; }
    Menu settings() { std::lock_guard lock(mutex);return menu; }
    bool submit(Menu m) {
        if (!m.valid() || m.host!=host || !interfaceEnabled.load() || stopping.load() || requested.load()) return false;
        {std::lock_guard lock(mutex);menu=std::move(m);chatOut.clear();}
        requested.store(true,std::memory_order_release);return true;
    }
    bool say(std::string s) {
        if (!chat(s) || stopping.load()) return false;
        std::lock_guard lock(mutex);if(chatOut.size()==16)return false;chatOut.push_back(std::move(s));return true;
    }
    std::deque<std::string> takeChat() { std::lock_guard lock(mutex);std::deque<std::string> out;out.swap(chatOut);return out; }
    void clear() { ready.store(false);std::lock_guard lock(mutex);view.reset();chatOut.clear(); }
};
inline Shared& shared() { static auto* s=new Shared;return *s; }
} // namespace pse::ui
