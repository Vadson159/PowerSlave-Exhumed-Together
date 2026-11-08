// P8 exact-build candidate, mode V. Win64 ABI inferred from DOC-68 static
// instruction evidence and subsequently gated by the P8 runtime probe.
// Never write animation pointers/refs/flags; use native resource lookup + bind.
#pragma once
#include "player_animation.hpp"
#include "john_profile.hpp"
#include <algorithm>
#include <cstring>

namespace pse::player_art {
struct Binding {
    static constexpr unsigned LookupRva=0x19ed10, BindRva=0x1a1170;
    static bool prefixes(const unsigned char* image) {
        constexpr unsigned char lookup[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40};
        constexpr unsigned char bind[]={0x48,0x85,0xd2,0x0f,0x84,0xc6,0x00,0x00,0x00,0x56,0x48,0x83,0xec,0x20};
        return std::equal(std::begin(lookup),std::end(lookup),image+LookupRva) &&
               std::equal(std::begin(bind),std::end(bind),image+BindRva);
    }
    template<class T> static T read(const void* p,unsigned offset) {
        return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(p)+offset);
    }
    static void* lookup(unsigned char* image,unsigned slot,Pose pose,bool proxy) {
        if(slot>=8 || !prefixes(image)) return nullptr;
        const auto text=resource(slot,pose,proxy);
        using Lookup=void*(__fastcall*)(void*,const char*);
        auto* anim=reinterpret_cast<Lookup>(image+LookupRva)(nullptr,text.data());
        if(!anim) return nullptr;
        const unsigned expected=pose==Pose::Idle?1u:pose==Pose::Walk?4u:pose==Pose::Shoot?2u:7u;
        if(read<unsigned>(anim,0x24)!=expected) return nullptr;
        auto* frames=read<unsigned char*>(anim,0x18);
        if(!frames) return nullptr;
        for(unsigned i=0;i<expected;++i) {
            auto* frame=frames+0x110*i;
            const auto delay=read<unsigned short>(frame,0);
            const unsigned wanted=pose==Pose::Idle?30u:pose==Pose::Shoot?5u:pose==Pose::Death && i==6?65535u:7u;
            if(delay!=wanted || read<unsigned>(frame,0x14)!=0) return nullptr;
            // Native AdvanceAnim follows non-'-' nextAnim strings. Never allow
            // a John animation to enter stock gameplay actions or Remove.
            auto* next=read<const char*>(frame,0x20);
            if(!next || std::strcmp(next,"-")!=0) return nullptr;
        }
        return anim;
    }
    // Caller attests simulation thread, live owned actor, stable world and no
    // teardown barrier. Resources are stack-local weak views, never cached.
    static bool apply(unsigned char* image,void* actor,unsigned slot,Pose pose,bool proxy,bool& changed) {
        changed=false;
        auto* anim=lookup(image,slot,pose,proxy);
        if(!actor || !anim) return false;
        if(read<void*>(actor,0x108)!=anim) {
            using Bind=void(__fastcall*)(void*,void*);
            reinterpret_cast<Bind>(image+BindRva)(actor,anim);changed=true;
            if(read<void*>(actor,0x108)!=anim || read<short>(actor,0x178)!=0 || read<float>(actor,0x114)!=0) return false;
        }
        // Numeric visual scale only, on the caller's confirmed live owned Actor.
        // +124 is the accepted DOC-57 scale field. No pointer/ref/flag stores.
        if(proxy) *reinterpret_cast<float*>(static_cast<unsigned char*>(actor)+0x124)=JohnScale;
        return true;
    }
};
}
