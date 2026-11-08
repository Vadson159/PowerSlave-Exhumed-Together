// Isolated P16 derivative; accepted source remains unchanged.
// Included inside the isolated client's private namespace, after its native
// read/log helpers. Not compiled into any accepted P1/P3/P4 DLL target.
namespace sn7=pse::self_native_p7;
namespace ss7=pse::self_p7;
ss7::Authority p7Authority;
sn7::NativeState p7Native;
bool p7Admitted{};
std::uint64_t p7AdmissionSerial{};
std::atomic<unsigned> p7ProbeRequest{},p7LocalDamageRequest{};
std::atomic<int> p7LocalDamageType{},p7LocalDamageAmount{};
std::uintptr_t p7PinnedActor{},p7PinnedNode{};
std::uint64_t p7PinnedAllocation{};
bool p7LifetimeClosed{};
namespace rp16=pse::respawn_p16;
namespace nl16=pse::life_p16;
rp16::DeadInput input16;nl16::RestartGate restart16;nl16::Receipt receipt16;
nl16::Start start16{};bool startValid16{},inputBound16{},requestPending16{},newLocal16{},protect16{};
float startFloor16{},startCeiling16{};
std::uint32_t localGeneration16=1,hostDeath16{};bool hostDead16{};
std::array<std::uint32_t,2> expectedGeneration16{1,1},lastDeath16{};
std::array<bool,2> lastDead16{};
std::atomic<std::uint64_t> approvedProbe16{},approvedReview16{};
std::uint64_t probeSerial16{};rp16::Confirmation ownConfirm16{};

bool p7CurrentGraph(Byte*& node,Byte*& actor) noexcept {
    __try {node=read<Byte*>(primary(),0xd8);actor=node?read<Byte*>(node,0x28):nullptr;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {node=nullptr;actor=nullptr;return false;}
}

bool p7AllocationIdentity(const Byte* node,const Byte* actor,std::uint64_t& sequence) noexcept {
    // DOC-58B.2 tagged Actor block: header=actor-0x60, sequence+8,
    // heap identity+0x18. The current reciprocal native primary graph is
    // checked before reading that header. No native pointer or header writes.
    __try {
        if(!node||!actor||read<const Byte*>(primary(),0xd8)!=node||read<const Byte*>(node,0x28)!=actor||
           read<const Byte*>(actor,0x10)!=node||read<const Byte*>(actor,0x1b8)!=primary()||
           read<const void*>(actor)!=g.base+0x52d198||read<const void*>(actor-0x60,0x18)!=g.base+0xaf4490)return false;
        sequence=read<std::uint64_t>(actor-0x60,8);
        return sequence!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void p7Bind(std::uint32_t epoch,bool sessionReady) {
    if(g.failed||g.stopped||p7LifetimeClosed)return;
    if(requestPending16&&!newLocal16)return;
    Byte* n{};Byte* a{};
    if(!p7CurrentGraph(n,a)) {
        p7LifetimeClosed=true;p7Authority.barrier();p7Admitted=false;sn7::invalidate();block("p7_local_native_graph_unreadable");return;
    }
    if(gameplay()&&a) {
        std::uint64_t sequence{};
        const bool identity=p7AllocationIdentity(n,a,sequence);
        if(!identity||p7LifetimeClosed||(p7PinnedActor&&
           (p7PinnedActor!=reinterpret_cast<std::uintptr_t>(a)||p7PinnedNode!=reinterpret_cast<std::uintptr_t>(n)||p7PinnedAllocation!=sequence))) {
            p7LifetimeClosed=true;p7Authority.barrier();p7Admitted=false;sn7::invalidate();block("p7_local_native_lifetime_changed");return;
        }
        if(!p7PinnedActor) {
            p7PinnedActor=reinterpret_cast<std::uintptr_t>(a);p7PinnedNode=reinterpret_cast<std::uintptr_t>(n);p7PinnedAllocation=sequence;
            log("p7_local_lifetime_pinned","\"actor\":\"%p\",\"node\":\"%p\",\"allocation_sequence\":%llu,\"rearm_supported\":false",a,n,sequence);
        }
    } else if(p7PinnedActor) {
        p7LifetimeClosed=true;p7Authority.barrier();p7Admitted=false;sn7::invalidate();return;
    }
    const bool active=((p7Admitted&&p7Authority.ready())||protect16)&&sessionReady&&!g.failed&&!g.stopped;
    const sn7::Binding binding{reinterpret_cast<std::uintptr_t>(instance()),reinterpret_cast<std::uintptr_t>(primary()),
        reinterpret_cast<std::uintptr_t>(n),reinterpret_cast<std::uintptr_t>(a),epoch?epoch:1u,localGeneration16,g.ticks,
        gameplay(),!g.stopped&&!g.shutdown};
    if(gameplay()&&a&&!sn7::tick(binding,active)) {
        p7Authority.barrier();p7Admitted=false;sn7::invalidate();block("p7_native_tick_binding");
    }
    if(!startValid16&&gameplay()&&a){start16={{read<float>(a,0x18),read<float>(a,0x1c),read<float>(a,0x20)},read<float>(a,0x38),read<int>(a,0x3c)};startFloor16=read<float>(a,0x11c);startCeiling16=read<float>(a,0x120);startValid16=true;}
}
void p7Offer(const pse::BridgePlayerSnapshot& s,std::uint8_t ownSlot,bool sessionReady) {
    if(s.slotId!=ownSlot||!sessionReady||g.failed||g.stopped)return;
    if(requestPending16)return;
    const auto now=steadyNs();
    // Bounded native gate: two processes on the SAME Windows QPC host.
    // Cross-PC clock mapping is not inferred from this subtraction.
    const bool age=s.stateTimeNs&&s.stateTimeNs<=now+1000000ULL&&
        (s.stateTimeNs>=now||now-s.stateTimeNs<500000000ULL);
    ss7::State state;state.identity={ownSlot,s.connectionGeneration,s.mapEpoch,s.entityId,s.entityGeneration,s.resetToken,s.discontinuitySequence};
    state.sequence=s.sequence;state.stateTick=s.serverTick;state.stateTimeNs=s.stateTimeNs;state.clockGeneration=1;
    auto& v=state.values;v.health=static_cast<std::int16_t>(s.health);v.ammo=s.ammoByWeapon;
    v.owned=static_cast<std::uint8_t>(s.weaponMask);v.weapon=s.activeWeapon;
    v.pendingWeapon=s.pendingWeapon<0?ss7::NoPendingWeapon:static_cast<std::uint8_t>(s.pendingWeapon);
    v.keys=s.keys;v.artifacts=s.artifacts;v.abilities=s.abilities;v.questItems=s.questItems;
    v.ankhCount=s.ankhCount;v.ankhMask=s.ankhMask;v.teamDolls=s.teamDolls;
    v.life=s.lifeState==pse::LIFE_ALIVE?ss7::Life::Alive:ss7::Life::Dead;v.deathSequence=s.deathSequence;
    const auto caps=sn7::capabilities();
    if(!caps.complete()) {if(g.ticks%120==0)log("p7_wait_native_gates","\"tick\":%llu",g.ticks);return;}
    if(!p7Admitted) {
        if(!age||!ss7::valid(state))return;
        const auto result=p7Authority.admit({state.identity,++p7AdmissionSerial,now},caps,{500000000ULL,1000000000ULL});
        if(result!=ss7::Result::Accepted) {block("p7_own_admission");return;}
        p7Admitted=true;
        protect16=false;
        log("p7_own_admitted","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"entity\":%u,\"entity_generation\":%u,\"reset\":%u,\"clock_scope\":\"same Windows QPC host only\"",ownSlot,s.connectionGeneration,s.mapEpoch,s.entityId,s.entityGeneration,s.resetToken);
    }
    const auto result=p7Authority.offer(state,now,age);
    if(result!=ss7::Result::Accepted&&result!=ss7::Result::PendingOccupied&&result!=ss7::Result::Stale) {
        log("p7_own_rejected","\"result\":%u,\"snapshot_seq\":%u,\"age_valid\":%s",static_cast<unsigned>(result),s.sequence,age?"true":"false");
        if(result!=ss7::Result::Invalid)block("p7_own_offer");
    }
}
void p7Step(bool sessionReady) {
    if(p7ProbeRequest.exchange(0,std::memory_order_acq_rel)) {sn7::KeysProbe probe;if(!sn7::probeKeys(probe))block("p7_key_probe");}
    if(!p7Admitted||g.failed||g.stopped)return;
    const bool safe=sessionReady&&gameplay()&&!g.failed&&!g.stopped&&g.localThisTick==1;
    const auto result=p7Authority.step(p7Native,{thread(),safe,safe,safe,sessionReady},steadyNs());
    if(result==ss7::Result::Applied||(result==ss7::Result::Unchanged&&g.ticks%30==0)) {
        const auto& state=*p7Authority.applied();const auto& v=state.values;
        log("p7_own_committed","\"tick\":%llu,\"snapshot_seq\":%u,\"server_tick\":%llu,\"health\":%d,\"weapon\":%u,\"pending\":%u,\"owned\":%u,\"keys\":%u,\"artifacts\":%u,\"abilities\":%u,\"quest\":%u,\"ankhs\":%u,\"ankh_mask\":%u,\"team_dolls\":%u,\"life\":%u,\"death_sequence\":%u,\"ammo\":[%u,%u,%u,%u,%u,%u,%u,%u],\"readback_equal\":true",g.ticks,state.sequence,state.stateTick,v.health,v.weapon,v.pendingWeapon,v.owned,v.keys,v.artifacts,v.abilities,v.questItems,v.ankhCount,v.ankhMask,v.teamDolls,static_cast<unsigned>(v.life),v.deathSequence,v.ammo[0],v.ammo[1],v.ammo[2],v.ammo[3],v.ammo[4],v.ammo[5],v.ammo[6],v.ammo[7]);
    } else if(result!=ss7::Result::Unchanged&&result!=ss7::Result::NoPending&&result!=ss7::Result::Unsafe) {
        log("p7_own_failure","\"result\":%u,\"tick\":%llu",static_cast<unsigned>(result),g.ticks);
        p7Authority.barrier();p7Admitted=false;sn7::invalidate();block("p7_own_apply_or_lease");
    }
    if(p7LocalDamageRequest.exchange(0,std::memory_order_acq_rel)&&safe&&p7Authority.ready()) {
        Byte* n{};Byte* a{};std::uint64_t sequence{};
        if(!p7CurrentGraph(n,a)||!p7AllocationIdentity(n,a,sequence)||
           reinterpret_cast<std::uintptr_t>(a)!=p7PinnedActor||reinterpret_cast<std::uintptr_t>(n)!=p7PinnedNode||sequence!=p7PinnedAllocation) {
            p7LifetimeClosed=true;p7Authority.barrier();p7Admitted=false;sn7::invalidate();block("p7_local_damage_binding");return;
        }
        const auto before=read<std::int16_t>(a,0x17a);
        using Damage=void(__fastcall*)(void*,void*,int,short);
        reinterpret_cast<Damage>(g.base+0x1a1370)(a,nullptr,p7LocalDamageType.load(),static_cast<short>(p7LocalDamageAmount.load()));
        log("p7_local_damage_fixture","\"before\":%d,\"after\":%d,\"suppressed\":%s",before,read<std::int16_t>(a,0x17a),before==read<std::int16_t>(a,0x17a)?"true":"false");
    }
}
