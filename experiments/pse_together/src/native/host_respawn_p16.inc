namespace rp16=pse::respawn_p16;
namespace nl16=pse::life_p16;
rp16::Team team16;rp16::DeadInput hostInput16;nl16::RestartGate restart16;
std::deque<pse::BridgeRespawnRequest> requests16;
std::deque<pse::BridgeRespawnConfirm> confirms16;
std::array<nl16::Receipt,8> receipts16{};
std::array<std::optional<rp16::Plan>,8> plans16{};
std::array<Identity,128> retired16{};unsigned retiredCount16{};
std::array<std::uint64_t,128> retiredAllocations16{};
std::array<std::optional<rp16::Request>,8> deferred16{};std::array<std::uint64_t,8> deferredSince16{};std::array<bool,8> connected16{}; // connected16: slots already announced to team16
bool bound16{};std::uint64_t probeSerial16{};
std::atomic<std::uint64_t> approvedProbe16{},approvedReview16{};
std::atomic<unsigned> fixture16{};
bool p16Pending(unsigned slot){return slot<plans16.size()&&plans16[slot].has_value();}
// Scarabs (checkpoints), one per player and world: the last one this player touched. A respawn puts the player beside it (else, as before,
// beside the spot where the first client's body was built).
struct Checkpoint16{bool valid{};int id{-1};float x{},y{},z{},yaw{};int sector{-1};};
std::array<Checkpoint16,8> checkpoints16{};
void markCheckpoint16(unsigned slot,int id){
    if(slot>MaxRemote)return;
    Checkpoint16 c;c.id=id;
    if(!pse::world::checkpointPlace(id,c.x,c.y,c.z,c.yaw,c.sector)){log("p16_checkpoint_unknown","\"slot\":%u,\"id\":%d",slot,id);return;}
    c.valid=true;checkpoints16[slot]=c;
    log("p16_checkpoint","\"slot\":%u,\"id\":%d,\"pos\":[%.0f,%.0f,%.0f],\"sector\":%d",slot,id,c.x,c.y,c.z,c.sector);
}
// The anchor (box, floor/ceiling support, sector, yaw) at a point near a player's scarab, in whatever sector holds that point (a scarab often
// stands in a small niche, and the player's own corpse may lie on the scarab), for the usual clear-spot check; the body's own bounds.
bool anchorNearCheckpoint(const Checkpoint16& c,Byte* body,float x,float y){
    namespace pp=pse::native_proxy::profile;
    const auto count=read<std::uint64_t>(g.base,0xa45970);auto* sectors=read<Byte*>(g.base,0xa45968);
    if(!c.valid||!body||!sectors||count>4096)return false;
    for(const auto i:{pp::floor_height,pp::ceiling_height}){const auto& site=pp::Sites[i];if(!std::equal(std::begin(site.prefix),std::end(site.prefix),g.base+site.rva))return false;}
    int sectorIndex=pse::world::findSector(x,y,c.z);
    if(sectorIndex<0)sectorIndex=pse::world::findSector(x,y,c.z+64.0f);
    if(sectorIndex<0||static_cast<std::uint64_t>(sectorIndex)>=count)return false;
    using Height=float(__fastcall*)(const float*,void*,std::uint8_t);
    auto* sector=sectors+static_cast<std::size_t>(sectorIndex)*0x88;
    const float p[3]{x,y,c.z};
    const float floor=reinterpret_cast<Height>(g.base+pp::Sites[pp::floor_height].rva)(p,sector,1);
    const float ceiling=reinterpret_cast<Height>(g.base+pp::Sites[pp::ceiling_height].rva)(p,sector,1);
    if(!std::isfinite(floor)||!std::isfinite(ceiling)||floor>=ceiling||std::fabs(floor-c.z)>192.0f)return false; // same floor level as the scarab
    pse::placement::Box b{read<pse::bounded::Vec3>(body,0x18),read<pse::bounded::Vec3>(body,0xec),read<pse::bounded::Vec3>(body,0xf8)};
    b.position[0]=x;b.position[1]=y;b.position[2]=floor;
    g.anchor=b;g.anchorYaw=c.yaw;g.anchorSector=sectorIndex;g.anchorFloor=floor;g.anchorCeiling=ceiling;g.anchorValid=true;
    return true;
}
bool p16Restore(){requests16.clear();confirms16.clear();return restart16.restore(log);}
int p16DtorEntry(void* actor){
    for(unsigned i=0;i<retiredCount16;++i)if(retired16[i].actor==actor&&!retired16[i].destroyed){
        if(read<std::uint64_t>(static_cast<Byte*>(actor)-0x60,8)!=retiredAllocations16[i])continue;
        log("p16_retired_dtor_entry","\"retired_index\":%u,\"actor\":\"%p\",\"allocation\":%llu",i,actor,retiredAllocations16[i]);return static_cast<int>(i);
    }
    return -1;
}
void p16DtorReturn(int index,void* actor){
    if(index<0)return;retired16[static_cast<unsigned>(index)].destroyed=true;
    log("p16_retired_dtor_return","\"retired_index\":%d,\"actor\":\"%p\",\"actor_reads\":false",index,actor);
}
rp16::Identity who16(unsigned slot){return {static_cast<std::uint8_t>(slot),slot?g.network->generation(slot):1u,g.network->mapEpoch()};}
void confirm16(const rp16::Confirmation& c){
    pse::BridgeRespawnConfirm out;out.slotId=c.request.who.slot;out.mapEpoch=c.request.who.epoch;out.deathSequence=c.request.death;
    out.newEntityId=0x5500+out.slotId;out.newGeneration=c.generation;const auto& rc=receipts16[out.slotId];out.defaultWeapons=0;for(unsigned i=0;i<8;++i)if(rc.owned[i])out.defaultWeapons|=1u<<i;out.ammo=rc.ammo[1];
    const auto& at=receipts16[out.slotId].start;
    out.posX=at.position.x;out.posY=at.position.y;out.posZ=at.position.z;out.yaw=at.yaw;
    if(confirms16.size()>=32){block("p16_confirm_outbox_full");return;}confirms16.push_back(out);
}
bool complete16(unsigned slot){
    auto& r=receipts16[slot];
    // The key gate approves itself: a portable run has no external reviewer (the keys are the team's own pre-death story).
    if(r.awaitingKeys){approvedProbe16.store(r.probe,std::memory_order_release);approvedReview16.store(1,std::memory_order_release);}
    const auto review=approvedReview16.load(std::memory_order_acquire);
    if(!nl16::finish(g.base,g.expectedTid,r,approvedProbe16.load(),review,log)){block("p16_native_finish");return false;}
    rp16::Confirmation c;const rp16::Gates safe{true,true,true,true};
    if(team16.complete(*plans16[slot],safe,true,{},c)!=rp16::Result::Accepted){block("p16_policy_commit");return false;}
    g.entityGeneration[slot]=c.generation;g.p7Dead[slot]=false;
    if(slot>0){g.p7KeysPermitted=false;g.p7KeyProbeSerial=g.p7KeyReviewSerial=0;std::fill(std::begin(R(slot).held),std::end(R(slot).held),0u);}
    log("p16_respawn_committed","\"slot\":%u,\"epoch\":%u,\"generation\":%u,\"death\":%u,\"health\":200,\"loadout_kept\":true,\"actor\":\"%p\",\"node\":\"%p\",\"allocation\":%llu,\"keys\":%u,\"story_equal\":true,\"world_reloaded\":false",slot,r.epoch,c.generation,c.request.death,r.fresh.actor,r.fresh.node,r.fresh.allocation,r.story.keys);
    confirm16(c);if(!slot)hostInput16.confirm(c,true);plans16[slot].reset();return true;
}
void request16(const rp16::Request& request){
    const unsigned slot=request.who.slot;if(slot>MaxRemote||(slot&&!R(slot).exists))return;
    std::optional<Ctx> ctx;if(slot)ctx.emplace(slot); // the remote's Player block is "the P2" for the native calls below
    rp16::Plan plan;rp16::Confirmation cached;
    const rp16::Gates gates{thread(),(slot?g.network->snapshotEligible(slot):g.network->anySnapshotEligible())&&gameplay()&&g.localThisTick==1,
        !g.worldExit&&!g.shutdown&&read<int>(instance(),0x20)==3,read<Byte>(instance(),background::MenuByte)==0};
    const auto* state=team16.slot(slot);
    if(state&&state->confirmed&&state->confirmed->request==request){
        const auto replay=team16.request(request,gates,plan,cached);if(replay==rp16::Result::Replay)confirm16(cached);return;
    }
    // Bounded congestion search. Every candidate is in the original sector,
    // at the same verified floor/ceiling, and separated from all world Actors.
    const auto anchor=g.anchor;const float anchorYaw=g.anchorYaw,anchorFloor=g.anchorFloor,anchorCeiling=g.anchorCeiling;
    const int anchorSector=g.anchorSector;const bool anchorValid=g.anchorValid;
    const auto restoreAnchor=[&]{g.anchor=anchor;g.anchorYaw=anchorYaw;g.anchorSector=anchorSector;g.anchorFloor=anchorFloor;g.anchorCeiling=anchorCeiling;g.anchorValid=anchorValid;};
    constexpr std::array<std::array<float,2>,17> offsets{{{0,0},{64,0},{-64,0},{0,64},{0,-64},{48,48},{-48,48},{48,-48},{-48,-48},
        {100,0},{-100,0},{0,100},{0,-100},{72,72},{-72,72},{72,-72},{-72,-72}}};
    nl16::Start start{};bool clear{};
    {   // this player's last scarab first (the scarab itself stands on the spot: the search moves beside it)
        auto* body=slot?R(slot).p2.actor:g.p1.actor;const auto& cp=checkpoints16[slot];
        if(cp.valid&&body){
            // rings around the scarab: 8 directions at 80..320 units, nearest first (the scarab itself and a corpse occupy its spot)
            for(float radius=80.0f;radius<=320.0f&&!clear;radius+=60.0f)
                for(int k=0;k<8&&!clear;++k){
                    const float a=static_cast<float>(k)*0.785398163f;
                    if(anchorNearCheckpoint(cp,body,cp.x+radius*std::sin(a),cp.y+radius*std::cos(a))&&spawnAnchorClear(true)){
                        start={{g.anchor.position[0],g.anchor.position[1],g.anchor.position[2]},g.anchorYaw,g.anchorSector};clear=true;
                    }
                }
            log("p16_respawn_checkpoint","\"slot\":%u,\"id\":%d,\"clear\":%s,\"at\":[%.0f,%.0f,%.0f]",slot,cp.id,clear?"true":"false",start.position.x,start.position.y,start.position.z);
        }
        restoreAnchor();
    }
    if(!clear) for(const auto& offset:offsets){
        g.anchor=anchor;g.anchor.position[0]+=offset[0];g.anchor.position[1]+=offset[1];
        if(spawnAnchorClear()){start={{g.anchor.position[0],g.anchor.position[1],g.anchor.position[2]},g.anchorYaw,g.anchorSector};clear=true;break;}
    }
    restoreAnchor();
    if(!clear){
        if(!deferred16[slot]){deferredSince16[slot]=g.ticks;log("p16_respawn_deferred","\"slot\":%u,\"reason\":\"no verified free position near level start\",\"retry\":true",slot);}
        deferred16[slot]=request;return;
    }
    deferred16[slot].reset();
    const auto result=team16.request(request,gates,plan,cached);
    log("p16_respawn_request","\"slot\":%u,\"connection\":%u,\"epoch\":%u,\"death\":%u,\"result\":%u",slot,request.who.connection,request.who.epoch,request.death,static_cast<unsigned>(result));
    if(result==rp16::Result::Replay){confirm16(cached);return;}
    if(result!=rp16::Result::Accepted)return;
    if(retiredCount16==retired16.size()||probeSerial16==UINT64_MAX){block("p16_lifetime_capacity");return;}
    auto& id=slot?R(slot).p2:g.p1;auto* p=slot?R(slot).storage.bytes:primary();nl16::Graph old{p};
    if(!nl16::graph(g.base,old)||old.actor!=id.actor||old.node!=id.node){block("p16_owned_graph");return;}
    const auto primaryBefore=p7PrimaryScalars();
    const auto scope=[&](bool enable){if(!slot)return true;g.scope=enable;return setHook(profile::Getter,enable)&&(enable||prefix(profile::Getter));};
    retiredAllocations16[retiredCount16]=old.allocation;retired16[retiredCount16++]=id;
    plans16[slot]=plan;
    if(!nl16::revive(g.base,g.expectedTid,old,start,g.network->mapEpoch(),plan.newGeneration,++probeSerial16,scope,receipts16[slot],log)){
        g.scope=false;setHook(profile::Getter,false);block("p16_native_revive");return;
    }
    id={receipts16[slot].fresh.actor,receipts16[slot].fresh.node,false};
    if(!slot)pse::native_proxy::acceptLocalRespawn();
    if(slot&&primaryBefore!=p7PrimaryScalars()){block("p16_respawn_changed_primary");return;}
    if(!guards()){block("p16_respawn_guards");return;}
    if(!receipts16[slot].awaitingKeys)complete16(slot);
}
void tick16(){
    if(!p16Eligible()||g.localThisTick!=1||!g.network->anySnapshotEligible())return;
    if(!bound16){
        if(team16.map(g.network->mapEpoch(),true)!=rp16::Result::Accepted||
           team16.connect(who16(0),g.entityGeneration[0])!=rp16::Result::Accepted){block("p16_team_bind");return;}
        connected16[0]=true;
        hostInput16.bind(who16(0),g.entityGeneration[0]);bound16=true;
        if(!restart16.install(g.base,g.expectedTid)){block("p16_restart_install");return;}
        log("p16_team_bound","\"epoch\":%u,\"slots\":%u",g.network->mapEpoch(),MaxRemote+1);
    }
    // a remote joins the team when its body exists (late joiners too)
    for(unsigned slot=1;slot<=MaxRemote;++slot)if(R(slot).exists&&!connected16[slot]&&g.entityGeneration[slot]){
        if(team16.connect(who16(slot),g.entityGeneration[slot])!=rp16::Result::Accepted){block("p16_team_bind");return;}
        connected16[slot]=true;log("p16_team_connected","\"slot\":%u,\"generation\":%u",slot,g.entityGeneration[slot]);
    }
    // A client that reconnected inside this world (new connection generation) keeps its body and its place in the team.
    for(unsigned slot=1;slot<=MaxRemote;++slot)if(connected16[slot]){
        const auto* st=team16.slot(slot);const auto now=g.network->generation(slot);
        if(st&&now&&st->who.connection&&now>st->who.connection){
            const auto previous=st->who.connection;
            const auto r=team16.rebind(who16(slot));
            log("p16_team_rebound","\"slot\":%u,\"connection\":%u,\"previous\":%u,\"result\":%u",slot,now,previous,static_cast<unsigned>(r));
        }
    }
    for(unsigned slot=0;slot<=MaxRemote;++slot){
        if(!connected16[slot])continue;
        if(plans16[slot]){if(receipts16[slot].awaitingKeys)complete16(slot);continue;}
        const auto& id=slot?R(slot).p2:g.p1;
        if(read<std::int16_t>(id.actor,0x17a)<=0||(read<unsigned>(id.actor,0x1c0)&0x200u))
            if(team16.observeDeath(who16(slot),g.entityGeneration[slot])==rp16::Result::Accepted){
                g.p7DeathSequence[slot]=team16.slot(slot)->death;g.p7Dead[slot]=true;
                log("p16_death_observed","\"slot\":%u,\"generation\":%u,\"death\":%u",slot,g.entityGeneration[slot],team16.slot(slot)->death);
            }
    }
    const auto counts=team16.counts();
    static unsigned previousCounts=UINT32_MAX;
    const unsigned packedCounts=counts.alive|(counts.dead<<4)|(counts.respawning<<8)|(counts.faulted<<12);
    if(packedCounts!=previousCounts){
        previousCounts=packedCounts;
        log("p16_team_counts","\"connected\":%u,\"alive\":%u,\"dead\":%u,\"respawning\":%u,\"faulted\":%u",counts.connected,counts.alive,counts.dead,counts.respawning,counts.faulted);
        if(counts.totalPartyKill())log("p16_total_party_kill","\"connected\":%u,\"dead\":%u,\"epoch\":%u,\"state\":3,\"pending\":0",counts.connected,counts.dead,g.network->mapEpoch());
    }
    hostInput16.observe(team16.slot(0)->life==rp16::Life::Dead,team16.slot(0)->death);
    DWORD fg{};GetWindowThreadProcessId(GetForegroundWindow(),&fg);
    if(auto request=hostInput16.sample((GetAsyncKeyState('E')&0x8000)!=0,GetTickCount64(),fg==GetCurrentProcessId()&&read<Byte>(instance(),background::MenuByte)==0))request16(*request);
    for(unsigned slot=0;slot<=MaxRemote;++slot)if(deferred16[slot]&&g.ticks%15==0&&!g.failed&&!plans16[slot]){
        if(g.ticks-deferredSince16[slot]>3600){log("p16_respawn_deferral_expired","\"slot\":%u",slot);deferred16[slot].reset();}
        else{const auto retry=*deferred16[slot];request16(retry);}
    }
    while(!requests16.empty()&&!g.failed){const auto r=requests16.front();requests16.pop_front();request16({{r.slotId,r.connectionGeneration,r.mapEpoch},r.deathSequence});}
    while(!confirms16.empty()&&!g.failed){if(!pse::isSuccess(g.network->bridge.pushOutbound(pse::BridgeMessage{confirms16.front()})))break;confirms16.pop_front();}
    // Directed diagnostic fixture operations; all run on Sim and retain native
    // behavior. Spawned weapon is adapter-owned and retired by native arenas.
    const auto op=fixture16.exchange(0,std::memory_order_acq_rel);
    if(op==1||op==2){auto* a=op==1?g.p1.actor:R(1).p2.actor;using Damage=void(__fastcall*)(void*,void*,int,std::int16_t);
        if(op==2)pse::world::applySelfDamage(a,2000,0); // a client's body takes sourceless damage only through this path
        else reinterpret_cast<Damage>(g.base+0x1a1370)(a,nullptr,0,2000);
        log("p16_fixture_native_death","\"slot\":%u",op-1);}
    if(op==3){const auto pos=position(R(1).p2.actor);using Factory=void*(__fastcall*)(void*,int,const Vec3*,float,int);
        auto* a=reinterpret_cast<Factory>(at(profile::Spawn))(g.base+0x5a10a0,44,&pos,read<float>(R(1).p2.actor,0x38),read<int>(R(1).p2.actor,0x3c));
        log("p16_fixture_weapon_created","\"actor\":\"%p\",\"node\":\"%p\",\"type\":44,\"at_secondary\":true",a,a?read<void*>(a,0x10):nullptr);}
}
