#include "player_animation.hpp"
#include <cstring>
#include <limits>
using namespace pse;
using namespace pse::player_art;
int main() {
    unsigned checks=0,failed=0;
    auto check=[&](bool v) { ++checks;if(!v) ++failed; };
    BridgePlayerSnapshot s;s.slotId=0;s.entityId=0x5500;s.entityGeneration=1;s.connectionGeneration=3;s.mapEpoch=44;s.sequence=1;s.serverTick=60;
    Animation a;check(a.accept(s));check(a.pose()==Pose::Idle);
    s.sequence++;s.velX=2;check(a.accept(s));check(a.pose()==Pose::Walk);
    s.sequence++;s.buttons=1;check(a.accept(s));check(a.pose()==Pose::Shoot);
    s.sequence++;s.health=0;check(a.accept(s));check(a.pose()==Pose::Death);
    s.sequence++;s.health=200;s.buttons=0;check(a.accept(s));check(a.pose()==Pose::Death);
    check(!a.accept(s)); // duplicate cannot rewind animation
    s.sequence++;s.entityGeneration++;check(!a.accept(s));
    a.reset();check(a.accept(s));check(a.pose()==Pose::Walk);
    s.sequence++;s.velX=1;check(a.accept(s));check(a.pose()==Pose::Idle);
    s.sequence++;s.lifeState=LIFE_DEAD;check(a.accept(s));check(a.pose()==Pose::Death);
    check(a.matches(0,s.sequence,44,3,s.entityId,s.entityGeneration));
    check(!a.matches(0,s.sequence-1,44,3,s.entityId,s.entityGeneration));
    for(unsigned slot=0;slot<8;++slot) for(const auto p:{Pose::Idle,Pose::Walk,Pose::Shoot,Pose::Death}) {
        const auto n=resource(slot,p,false),q=resource(slot,p,true);
        check(std::strstr(n.data(),name(p))!=nullptr && std::strstr(q.data(),"_proxy_")!=nullptr);
    }
    check(resource(8,Pose::Idle,false)[0]==0);
    // One shared John skin: resource names never depend on the slot.
    for(const auto p:{Pose::Idle,Pose::Walk,Pose::Shoot,Pose::Death}) for(const bool proxy:{false,true}) {
        const auto base=resource(0,p,proxy);
        check(std::strstr(base.data(),"player/pse_john_")==base.data());
        for(unsigned slot=1;slot<8;++slot) check(std::strcmp(base.data(),resource(slot,p,proxy).data())==0);
    }
    for(unsigned i=0;i<9;++i) {
        Animation t;auto v=s;v.health=200;v.lifeState=LIFE_ALIVE;
        switch(i) {
            case 0:v.slotId=8;break;case 1:v.entityId=0;break;case 2:v.entityGeneration=0;break;
            case 3:v.connectionGeneration=0;break;case 4:v.mapEpoch=0;break;case 5:v.sequence=0;break;
            case 6:v.velX=std::numeric_limits<float>::quiet_NaN();break;
            case 7:v.velY=std::numeric_limits<float>::infinity();break;
            case 8:v.velZ=-std::numeric_limits<float>::infinity();break;
        }
        check(!t.accept(v));check(!t.valid());
    }
    for(unsigned i=0;i<5;++i) {
        Animation t;check(t.accept(s));auto v=s;v.sequence++;
        switch(i) { case 0:v.slotId=1;break;case 1:v.connectionGeneration++;break;case 2:v.mapEpoch++;break;
                    case 3:v.entityId++;break;case 4:v.serverTick--;break; }
        check(!t.accept(v));check(t.matches(0,s.sequence,44,3,s.entityId,s.entityGeneration));
    }
    std::printf("P8 CONTRACT checks=%u failures=%u; OFFLINE ONLY\n",checks,failed);
    return failed?1:0;
}
