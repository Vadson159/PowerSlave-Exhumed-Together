namespace rp16=pse::respawn_p16;
namespace nl16=pse::life_p16;
rp16::Team team16;rp16::DeadInput hostInput16;nl16::RestartGate restart16;
std::deque<pse::BridgeRespawnRequest> requests16;
std::deque<pse::BridgeRespawnConfirm> confirms16;
std::array<nl16::Receipt,2> receipts16{};
std::array<std::optional<rp16::Plan>,2> plans16{};
std::array<Identity,128> retired16{};unsigned retiredCount16{};
std::array<std::uint64_t,128> retiredAllocations16{};
std::array<std::optional<rp16::Request>,2> deferred16{};std::array<std::uint64_t,2> deferredSince16{};
bool bound16{};std::uint64_t probeSerial16{};
std::atomic<std::uint64_t> approvedProbe16{},approvedReview16{};
std::atomic<unsigned> fixture16{};
bool p16Pending(unsigned slot){return slot<plans16.size()&&plans16[slot].has_value();}
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
rp16::Identity who16(unsigned slot){return {static_cast<std::uint8_t>(slot),slot?g.network->generation():1u,g.network->mapEpoch()};}
void confirm16(const rp16::Confirmation& c){
    pse::BridgeRespawnConfirm out;out.slotId=c.request.who.slot;out.mapEpoch=c.request.who.epoch;out.deathSequence=c.request.death;
    out.newEntityId=0x5500+out.slotId;out.newGeneration=c.generation;out.defaultWeapons=3;out.ammo=60;
    const auto& at=receipts16[out.slotId].start;
    out.posX=at.position.x;out.posY=at.position.y;out.posZ=at.position.z;out.yaw=at.yaw;
    if(confirms16.size()>=32){block("p16_confirm_outbox_full");return;}confirms16.push_back(out);
}
bool complete16(unsigned slot){
    auto& r=receipts16[slot];
    if(r.awaitingKeys&&approvedProbe16.load(std::memory_order_acquire)!=r.probe)return false;
    const auto review=approvedReview16.load(std::memory_order_acquire);
    if(!nl16::finish(g.base,g.expectedTid,r,approvedProbe16.load(),review,log)){block("p16_native_finish");return false;}
    rp16::Confirmation c;const rp16::Gates safe{true,true,true,true};
    if(team16.complete(*plans16[slot],safe,true,{},c)!=rp16::Result::Accepted){block("p16_policy_commit");return false;}
    g.entityGeneration[slot]=c.generation;g.p7Dead[slot]=false;
    if(slot==1){g.p7KeysPermitted=false;g.p7KeyProbeSerial=g.p7KeyReviewSerial=0;std::fill(std::begin(g.held),std::end(g.held),0u);}
    log("p16_respawn_committed","\"slot\":%u,\"epoch\":%u,\"generation\":%u,\"death\":%u,\"health\":200,\"weapons\":3,\"pistol\":60,\"heavy_ammo_zero\":true,\"actor\":\"%p\",\"node\":\"%p\",\"allocation\":%llu,\"keys\":%u,\"story_equal\":true,\"world_reloaded\":false",slot,r.epoch,c.generation,c.request.death,r.fresh.actor,r.fresh.node,r.fresh.allocation,r.story.keys);
    confirm16(c);if(!slot)hostInput16.confirm(c,true);plans16[slot].reset();return true;
}
void request16(const rp16::Request& request){
    const unsigned slot=request.who.slot;if(slot>=2)return;
    rp16::Plan plan;rp16::Confirmation cached;
    const rp16::Gates gates{thread(),g.network->snapshotEligible()&&gameplay()&&g.localThisTick==1,
        !g.worldExit&&!g.shutdown&&read<int>(instance(),0x20)==3,read<Byte>(instance(),background::MenuByte)==0};
    const auto* state=team16.slot(slot);
    if(state&&state->confirmed&&state->confirmed->request==request){
        const auto replay=team16.request(request,gates,plan,cached);if(replay==rp16::Result::Replay)confirm16(cached);return;
    }
    // Bounded congestion search. Every candidate is in the original sector,
    // at the same verified floor/ceiling, and separated from all world Actors.
    const auto anchor=g.anchor;
    constexpr std::array<std::array<float,2>,17> offsets{{{0,0},{64,0},{-64,0},{0,64},{0,-64},{48,48},{-48,48},{48,-48},{-48,-48},
        {100,0},{-100,0},{0,100},{0,-100},{72,72},{-72,72},{72,-72},{-72,-72}}};
    nl16::Start start{};bool clear{};
    for(const auto& offset:offsets){
        g.anchor=anchor;g.anchor.position[0]+=offset[0];g.anchor.position[1]+=offset[1];
        if(spawnAnchorClear()){start={{g.anchor.position[0],g.anchor.position[1],g.anchor.position[2]},g.anchorYaw,g.anchorSector};clear=true;break;}
    }
    g.anchor=anchor;
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
    auto& id=slot?g.p2:g.p1;auto* p=slot?player():primary();nl16::Graph old{p};
    if(!nl16::graph(g.base,old)||old.actor!=id.actor||old.node!=id.node){block("p16_owned_graph");return;}
    const auto primaryBefore=p7PrimaryScalars();
    const auto scope=[&](bool enable){if(!slot)return true;g.scope=enable;return setHook(profile::Getter,enable)&&(enable||prefix(profile::Getter));};
    retiredAllocations16[retiredCount16]=old.allocation;retired16[retiredCount16++]=id;
    plans16[slot]=plan;
    if(!nl16::revive(g.base,g.expectedTid,old,start,g.network->mapEpoch(),plan.newGeneration,++probeSerial16,scope,receipts16[slot],log)){
        g.scope=false;setHook(profile::Getter,false);block("p16_native_revive");return;
    }
    id={receipts16[slot].fresh.actor,receipts16[slot].fresh.node,false};
    if(slot&&primaryBefore!=p7PrimaryScalars()){block("p16_respawn_changed_primary");return;}
    if(!guards()){block("p16_respawn_guards");return;}
    if(!receipts16[slot].awaitingKeys)complete16(slot);
}
void tick16(){
    if(!p16Eligible()||g.localThisTick!=1||!g.network->snapshotEligible())return;
    if(!bound16){
        if(team16.map(g.network->mapEpoch(),true)!=rp16::Result::Accepted||
           team16.connect(who16(0),g.entityGeneration[0])!=rp16::Result::Accepted||team16.connect(who16(1),g.entityGeneration[1])!=rp16::Result::Accepted){block("p16_team_bind");return;}
        hostInput16.bind(who16(0),g.entityGeneration[0]);bound16=true;
        if(!restart16.install(g.base,g.expectedTid)){block("p16_restart_install");return;}
        log("p16_team_bound","\"epoch\":%u,\"slots\":2",g.network->mapEpoch());
    }
    for(unsigned slot=0;slot<2;++slot){
        if(plans16[slot]){if(receipts16[slot].awaitingKeys)complete16(slot);continue;}
        const auto& id=slot?g.p2:g.p1;
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
    for(unsigned slot=0;slot<2;++slot)if(deferred16[slot]&&g.ticks%15==0&&!g.failed&&!plans16[slot]){
        if(g.ticks-deferredSince16[slot]>3600){log("p16_respawn_deferral_expired","\"slot\":%u",slot);deferred16[slot].reset();}
        else{const auto retry=*deferred16[slot];request16(retry);}
    }
    while(!requests16.empty()&&!g.failed){const auto r=requests16.front();requests16.pop_front();request16({{r.slotId,r.connectionGeneration,r.mapEpoch},r.deathSequence});}
    while(!confirms16.empty()&&!g.failed){if(!pse::isSuccess(g.network->bridge.pushOutbound(pse::BridgeMessage{confirms16.front()})))break;confirms16.pop_front();}
    // Directed diagnostic fixture operations; all run on Sim and retain native
    // behavior. Spawned weapon is adapter-owned and retired by native arenas.
    const auto op=fixture16.exchange(0,std::memory_order_acq_rel);
    if(op==1||op==2){auto* a=op==1?g.p1.actor:g.p2.actor;using Damage=void(__fastcall*)(void*,void*,int,std::int16_t);
        reinterpret_cast<Damage>(g.base+0x1a1370)(a,nullptr,0,2000);log("p16_fixture_native_death","\"slot\":%u",op-1);}
    if(op==3){const auto pos=position(g.p2.actor);using Factory=void*(__fastcall*)(void*,int,const Vec3*,float,int);
        auto* a=reinterpret_cast<Factory>(at(profile::Spawn))(g.base+0x5a10a0,44,&pos,read<float>(g.p2.actor,0x38),read<int>(g.p2.actor,0x3c));
        log("p16_fixture_weapon_created","\"actor\":\"%p\",\"node\":\"%p\",\"type\":44,\"at_secondary\":true",a,a?read<void*>(a,0x10):nullptr);}
}
