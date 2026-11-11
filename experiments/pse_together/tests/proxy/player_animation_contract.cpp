// Offline contract of the proxy pose policy (player_animation.hpp): which animation a player's sprite shows from its snapshots.
#include "player_animation.hpp"
#include <cstdio>
namespace pa=pse::player_art;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
pse::BridgePlayerSnapshot snap(std::uint32_t tick,float x,float y,std::uint32_t buttons=0,float vx=0,float vy=0,std::int32_t health=200){
    pse::BridgePlayerSnapshot s;
    s.slotId=1;s.entityId=0x5501;s.entityGeneration=1;s.connectionGeneration=1;s.mapEpoch=3;s.sequence=tick;s.serverTick=tick;
    s.posX=x;s.posY=y;s.posZ=0;s.velX=vx;s.velY=vy;s.health=health;s.lifeState=health>0?pse::LIFE_ALIVE:pse::LIFE_DEAD;s.buttons=buttons;
    return s;
}
}
int main(){
    {   // A remote body on the host: zero velocity in every other snapshot (re-placed to its client's pose), but it keeps moving: Walk, steady.
        pa::Animation a;bool steady=true;
        a.accept(snap(2,0,0,0,3,0));
        for(std::uint32_t t=4;t<=40;t+=2) { const float vx=(t%4)?0.f:3.f;a.accept(snap(t,static_cast<float>(t)*3.f,0,0,vx,0));steady=steady&&a.pose()==pa::Pose::Walk; }
        check(steady,"moving body with flickering velocity stays Walk");
    }
    {   // Forward held against a wall: the walk animation (as in the stock game, legs move), no position change needed.
        pa::Animation a;a.accept(snap(2,0,0,1u<<2));
        check(a.pose()==pa::Pose::Walk,"forward button alone is Walk");
    }
    {   // Stopped: back to Idle after the hold, not at once.
        pa::Animation a;a.accept(snap(2,0,0,1u<<2));a.accept(snap(4,0,0));
        check(a.pose()==pa::Pose::Walk,"Walk holds right after stopping");
        a.accept(snap(20,0,0));
        check(a.pose()==pa::Pose::Idle,"Idle after the hold");
    }
    {   // A single click shows the whole attack, not one frame.
        pa::Animation a;a.accept(snap(2,0,0,1));a.accept(snap(4,0,0));a.accept(snap(8,0,0));
        check(a.pose()==pa::Pose::Shoot,"click keeps Shoot for the hold");
        a.accept(snap(30,0,0));
        check(a.pose()==pa::Pose::Idle,"Idle after the attack");
    }
    {   // Held attack while walking: Shoot outranks Walk; death outranks everything and sticks.
        pa::Animation a;
        for(std::uint32_t t=2;t<=20;t+=2) a.accept(snap(t,static_cast<float>(t)*3.f,0,1u|(1u<<2)));
        check(a.pose()==pa::Pose::Shoot,"held attack while walking is Shoot");
        a.accept(snap(22,60,0,1,0,0,0));
        check(a.pose()==pa::Pose::Death,"death");
        a.accept(snap(24,60,0,1u<<2));
        check(a.pose()==pa::Pose::Death,"death does not restart into motion");
    }
    {   // A big jump of position (teleport, long gap) is not read as walking speed.
        pa::Animation a;a.accept(snap(2,0,0));a.accept(snap(200,5000,0));
        check(a.pose()==pa::Pose::Idle,"teleport after a long gap is not Walk");
    }
    {   // The proxy presents snapshots ~10 behind the newest (presentation buffer): the pose must follow the presented one, while the stream flows.
        pa::Animation a;
        for(std::uint32_t t=2;t<=20;t+=2) a.accept(snap(t,0,0));                                  // standing
        for(std::uint32_t t=22;t<=60;t+=2) a.accept(snap(t,static_cast<float>(t)*3.f,0,1u<<2));  // then walking
        check(a.sameEntity(1,3,1,0x5501,1),"same entity while the stream flows");
        check(a.poseAt(10)==pa::Pose::Idle,"presented standing snapshot shows Idle");
        check(a.poseAt(40)==pa::Pose::Walk,"presented walking snapshot shows Walk although newer ones exist");
        check(a.poseAt(41)==pa::Pose::Walk,"between two snapshots: the older one's pose");
        check(a.poseAt(1)==pa::Pose::Idle,"older than the history: oldest pose");
        check(!a.sameEntity(1,3,1,0x5501,2),"another body generation is not the same entity");
        for(std::uint32_t t=62;t<=80;t+=2) a.accept(snap(t,180,0,1));                             // attack
        check(a.poseAt(70)==pa::Pose::Shoot,"presented attack shows Shoot");
        a.accept(snap(82,180,0,0,0,0,0));                                                        // death
        check(a.poseAt(80)==pa::Pose::Shoot && a.poseAt(82)==pa::Pose::Death,"death shows when its snapshot is presented");
    }
    {   // A long session: the ring wraps and still answers for recent snapshots.
        pa::Animation a;
        for(std::uint32_t t=1;t<=1000;++t) a.accept(snap(t,0,0,(t>=990)?(1u<<2):0u));
        check(a.poseAt(985)==pa::Pose::Idle && a.poseAt(995)==pa::Pose::Walk,"ring buffer wraps");
    }
    std::printf("player_animation_contract: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
