// Synthetic storage and engine doubles, not KEX objects or runtime evidence.
#include "weapon_pickup_p16.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace wp=pse::weapon_p16;
namespace {
unsigned checks{},failures{},stockCalls{},giveCalls{},grants{},faults{};
bool active=true,badAmmo{};wp::Roster roster;
struct alignas(16) Storage {std::array<unsigned char,0x220> b{};void* data(){return b.data();}};
Storage player[2],puppet[2],node[2],pickup,pickNode,definition;
template<class T>void put(void* p,unsigned offset,T v){std::memcpy(static_cast<unsigned char*>(p)+offset,&v,sizeof(v));}
template<class T>T get(void* p,unsigned offset){T v;std::memcpy(&v,static_cast<unsigned char*>(p)+offset,sizeof(v));return v;}
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void original(void*,void*){++stockCalls;}
std::uint8_t give(void* p,int slot,std::uint8_t select){
    ++giveCalls;put<std::uint8_t>(p,0xcc+slot,1);if(select)put<int>(p,0x8c,slot);
    if(badAmmo)put<std::uint16_t>(p,0xbe,99);return 1;
}
bool eligible(){return active;}
wp::Roster members(){return roster;}
void event(const char* name,unsigned,unsigned){if(std::strcmp(name,"p16_weapon_only_granted")==0)++grants;else if(std::strcmp(name,"p16_weapon_ammo_changed")==0)++faults;}
void reset(){
    stockCalls=giveCalls=grants=faults=0;active=true;badAmmo=false;roster={};
    for(unsigned i=0;i<2;++i){
        player[i]={};puppet[i]={};node[i]={};
        put<void*>(player[i].data(),0xd8,node[i].data());put<void*>(node[i].data(),0x28,puppet[i].data());
        put<void*>(puppet[i].data(),0x10,node[i].data());put<void*>(puppet[i].data(),0x1b8,player[i].data());
        put<std::int16_t>(puppet[i].data(),0x17a,200);put<std::uint16_t>(player[i].data(),0xbe,17);
        roster[i]={player[i].data(),puppet[i].data(),i};
    }
    pickup={};pickNode={};definition={};
    put<void*>(pickup.data(),0x10,pickNode.data());put<void*>(pickNode.data(),0x28,pickup.data());
    put<void*>(pickup.data(),0x170,definition.data());put<int>(definition.data(),0x128,3);
    wp::bindForTest({eligible,members,event},original,give);
}
void touch(unsigned slot){wp::touchForTest(pickup.data(),puppet[slot].data());}
}
int main(){
    reset();const auto beforePickup=pickup.b,beforeNode=pickNode.b;const auto other=player[1].b;
    touch(0);
    check(giveCalls==1&&grants==1&&stockCalls==0,"pickup invokes explicit-player GiveWeapon and never stock consume");
    check(pickup.b==beforePickup&&pickNode.b==beforeNode,"pickup Actor and node byte-identical after grant in engine double");
    check(get<std::uint8_t>(player[0].data(),0xcf)==1&&get<std::uint16_t>(player[0].data(),0xbe)==17,"new weapon added with unchanged ammo");
    check(player[1].b==other,"teammate inventory unaffected");
    for(unsigned i=0;i<20;++i)touch(0);
    check(giveCalls==1&&stockCalls==0,"remaining in pickup cannot farm ammo or repeat grant");
    touch(1);check(giveCalls==2&&grants==2,"second player can collect same persistent Actor");
    put<std::int16_t>(puppet[0].data(),0x17a,0);put<std::uint8_t>(player[0].data(),0xcf,0);touch(0);
    check(giveCalls==2,"dead player cannot collect weapon");
    put<std::int16_t>(puppet[0].data(),0x17a,200);touch(0);
    check(giveCalls==3&&get<std::uint16_t>(player[0].data(),0xbe)==17,"after fixture respawn reset same weapon can be reacquired without ammo");
    check(pickup.b==beforePickup&&pickNode.b==beforeNode,"persistent pickup survives all player lives in fixture");
    reset();active=false;touch(0);check(stockCalls==1&&giveCalls==0,"inactive adapter forwards original rather than freezing host pickup");
    reset();roster={};touch(0);check(stockCalls==1&&giveCalls==0,"unowned actor preserves original path");
    reset();roster[0].canCollect=false;touch(0);touch(1);
    check(stockCalls==0&&giveCalls==1&&get<std::uint8_t>(player[0].data(),0xcf)==0,"respawning recipient cannot consume pickup; teammate remains eligible");
    roster[0].canCollect=true;touch(0);check(giveCalls==2&&stockCalls==0,"committed recipient can use the same persistent pickup");
    reset();put<void*>(puppet[0].data(),0x1b8,player[1].data());touch(0);check(giveCalls==0&&stockCalls==0,"wrong native owner refused without mutation");
    reset();put<int>(definition.data(),0x128,8);touch(0);check(giveCalls==0&&stockCalls==0,"unverified weapon slot refused");
    reset();badAmmo=true;touch(0);touch(1);check(giveCalls==1&&grants==0&&faults==1,"unexpected native ammo change faults once without retry or manual repair");
    reset();check(wp::stop(),"stop accepts current fixture");touch(0);check(stockCalls==1&&giveCalls==0,"stopped adapter forwards original");
    std::printf("P16 WEAPON checks=%u failures=%u; ENGINE DOUBLES, NOT KEX\n",checks,failures);
    return failures?1:0;
}
