// Included after client_self_p16; current namespace already owns g/log/read.
struct StagedConfirm16 {pse::BridgeRespawnConfirm wire;std::uint8_t slot{};std::uint32_t connection{},epoch{};};
std::optional<StagedConfirm16> stagedConfirm16;
bool stageLocal16(const pse::BridgeRespawnConfirm& c,std::uint8_t slot,std::uint32_t connection,std::uint32_t epoch){
    const rp16::Confirmation confirm{{{slot,connection,epoch},c.deathSequence},c.newGeneration,{}};
    if(!requestPending16||newLocal16||stagedConfirm16||!input16.expects(confirm))return false;
    stagedConfirm16=StagedConfirm16{c,slot,connection,epoch};return true;
}
bool confirmLocal16(const pse::BridgeRespawnConfirm& c,std::uint8_t slot,std::uint32_t connection,std::uint32_t epoch){
    rp16::Confirmation confirm{{{slot,connection,epoch},c.deathSequence},c.newGeneration,{}};
    if(!requestPending16||newLocal16||!input16.expects(confirm)||!startValid16||c.mapEpoch!=epoch||c.newEntityId!=0x5500u+slot||
       c.defaultWeapons!=3||c.ammo!=60)return false;
    const nl16::Start selected{{c.posX,c.posY,c.posZ},c.yaw,start16.sector};
    if(!nl16::startGeometry(g.base,selected,start16,log)){block("p16_confirm_start_geometry");return false;}
    nl16::Graph old{primary()};if(!nl16::graph(g.base,old))return false;
    if(probeSerial16==UINT64_MAX){block("p16_probe_exhausted");return false;}
    if(!nl16::revive(g.base,g.tid,old,selected,epoch,c.newGeneration,++probeSerial16,[](bool){return true;},receipt16,log)){block("p16_client_revive");return false;}
    localGeneration16=c.newGeneration;newLocal16=true;protect16=true;ownConfirm16=confirm;
    p7PinnedActor=reinterpret_cast<std::uintptr_t>(receipt16.fresh.actor);p7PinnedNode=reinterpret_cast<std::uintptr_t>(receipt16.fresh.node);p7PinnedAllocation=receipt16.fresh.allocation;
    sn7::Binding binding{reinterpret_cast<std::uintptr_t>(instance()),reinterpret_cast<std::uintptr_t>(primary()),p7PinnedNode,p7PinnedActor,epoch,localGeneration16,g.ticks,true,true};
    if(!sn7::rearmForRespawn(binding,c.deathSequence)){block("p16_client_backend_rearm");return false;}
    if(!sn7::tick(binding,true)){block("p16_client_fresh_protection");return false;}
    p7ProbeRequest.store(1,std::memory_order_release);
    log("p16_client_confirm_native","\"epoch\":%u,\"generation\":%u,\"death\":%u,\"keys_gate_pending\":true",epoch,localGeneration16,c.deathSequence);
    return true;
}
bool completeLocal16(){
    if(!newLocal16)return false;
    if(receipt16.awaitingKeys&&(approvedProbe16.load(std::memory_order_acquire)!=receipt16.probe||!approvedReview16.load()))return false;
    if(!sn7::observe().keysPermitted)return false;
    if(!nl16::finish(g.base,g.tid,receipt16,approvedProbe16.load(),approvedReview16.load(),log)||!input16.confirm(ownConfirm16,true)){block("p16_client_commit");return false;}
    // The P7 Authority is intentionally terminal after disconnect/death in one world. The respawn handshake just completed a new
    // authenticated native incarnation (new generation + key gate), so its next admission uses a fresh Authority object; P7 code is unchanged.
    p7Authority=ss7::Authority{};p7Admitted=false;
    requestPending16=false;newLocal16=false;
    log("p16_client_respawn_committed","\"epoch\":%u,\"generation\":%u,\"death\":%u,\"health\":200,\"weapons\":3,\"pistol\":60,\"heavy_ammo_zero\":true,\"story_equal\":true,\"actor\":\"%p\",\"node\":\"%p\",\"allocation\":%llu",receipt16.epoch,localGeneration16,ownConfirm16.request.death,receipt16.fresh.actor,receipt16.fresh.node,receipt16.fresh.allocation);
    return true;
}
