"""One-time isolated P16 host derivation. Never overwrites an existing candidate."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'src/native/host_p7.cpp'
dest=ROOT/'src/native/host_p16.cpp'
s=source.read_text(encoding='utf8')
def replace(old,new):
    global s
    assert s.count(old)==1,old
    s=s.replace(old,new)
replace('#include <pse_together/config.hpp>','#include <pse_together/config.hpp>\n#include "weapon_pickup_p16.hpp"')
replace('void construct() {',r'''
bool p16Eligible(){
    return thread()&&g.inTick&&g.schedule&&g.phase==Phase::Live&&!g.failed&&!g.worldExit&&!g.shutdown&&
        gameplay()&&g.network&&g.network->wantsPlayer();
}
pse::weapon_p16::Roster p16Roster(){
    pse::weapon_p16::Roster r{};
    if(p16Eligible()){
        if(live(g.p1,primary()))r[0]={primary(),g.p1.actor,0};
        if(live(g.p2,player()))r[1]={player(),g.p2.actor,1};
    }
    return r;
}
void p16Event(const char* name,unsigned slot,unsigned value){
    log(name,"\"slot\":%u,\"value\":%u",slot,value);
}
void construct() {''')
replace('snapshot("independent_spawn");','snapshot("independent_spawn");\n    if(!pse::weapon_p16::install(g.base,g.expectedTid,{p16Eligible,p16Roster,p16Event}))block("p16_weapon_hook_install");')
replace('void unbind(const char* reason) {','void unbind(const char* reason) {\n    if(!pse::weapon_p16::stop())block("p16_weapon_restore");')
with dest.open('x',encoding='utf8') as f:
    f.write('// P16 isolated host candidate; native respawn is not implemented here.\n')
    f.write('// Derived from host_p7.cpp SHA256 '+hashlib.sha256(source.read_bytes()).hexdigest()+'\n'+s)
print(dest)
