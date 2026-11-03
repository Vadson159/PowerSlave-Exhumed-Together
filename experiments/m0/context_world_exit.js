/* Extend the native GI state-exit contract to our unregistered Player allocation.
   Called only before native arena teardown; never Reset a post-unload handle. */
function prepareM0ContextWorldExit() {
 prefix('state_exit',0x1b6670);
 let completed=false,exitInProgress=false,exitStarted=false;
 // The entry was checked by full_player_probe before any passive listeners.
 Interceptor.attach(base.add(0x19f210),{onEnter(args){
  if(!simulationThread()||!args[0].readPointer().equals(base.add(0x52d198))||!args[0].add(0x1b8).readPointer().equals(p2))return;
  const rec=[p2Identity,...retiredPuppets].find(r=>r&&r.actor.equals(args[0])&&(live(r)||exitInProgress&&r.pool_return_seen));
  if(rec)this.saved={rec,actor:args[0].toString(),generation:rec.generation};
 },onLeave(){if(!this.saved)return;const q=this.saved;q.rec.native_actor_destructor_returned=true;q.rec.invalid=true;
  event('C_owned_puppet_actor_destructor_return',{actor:q.actor,generation:q.generation,reads_after_return:false,arena_completion_recorded_separately:true});
 }});
 Interceptor.attach(base.add(0x1b6670),{onEnter(args){
  if(!simulationThread()||!args[0].equals(gi)||gi.add(0x24).readS32()!==3)return;
  const next=gi.add(0x20).readS32();if(next===3)return;
  if(exitStarted)return;exitStarted=true;
  this.observed=true;exitInProgress=true;const handle=p2.add(0xd8).readPointer();
  try{
   stopHooks();
   const before=graph(p2),primaryBefore=handle.isNull()?null:graph(local),primaryInventory=handle.isNull()?null:inventory(local);
   if(!handle.isNull()){
    if(!p2Identity||!handle.equals(p2Identity.node)||!live(p2Identity))throw Error('Owned context handle invalid before native state-exit; no Reset attempted');
    callOnce('context_unbind_before_world_exit',()=>reset(p2));
   }
   if(!p2.add(0xd8).readPointer().isNull())throw Error('Native Reset did not unbind external context');
   if(primaryBefore&&(!samePrimary(primaryBefore,graph(local))||!sameInventory(primaryInventory,inventory(local))))throw Error('External context unbind changed primary graph/health/inventory');
   event('C_context_unbound_before_world_exit',{native_rva:'0x1b6670',next_state:next,before,primary_before:primaryBefore,primary_after:primaryBefore?graph(local):null,
    primary_inventory_before:primaryInventory,primary_inventory_after:primaryInventory?inventory(local):null,external_handle_after:p2.add(0xd8).readPointer().toString(),owned_guards:guards(),policy:'One native Reset before original world cleanup. No holder releases or arena calls by adapter.'});
  }catch(e){stage='context_exit_unknown';event('C_context_world_exit_error',{error:String(e),native_calls:nativeCalls,note:'Context/code remain pinned; original native state-exit proceeds, never retry uncertain native calls.'});}
 },onLeave(){if(!this.observed)return;completed=p2.add(0xd8).readPointer().isNull();
  event('C_native_world_exit_returned',{native_rva:'0x1b6670',external_handle:p2.add(0xd8).readPointer().toString(),context_unbound:completed,
   guards:guards(),actor_reads:false,owned_puppet_destructors:[p2Identity,...retiredPuppets].filter(Boolean).map(r=>({generation:r.generation,pool_return_seen:r.pool_return_seen,native_actor_destructor_returned:!!r.native_actor_destructor_returned})),full_m0:false});exitInProgress=false;
 }});
 // native_shutdown was checked by the full probe before any hook on this site.
 // Direct Quit may bypass StateExit. Quiesce while native definitions are alive.
 Interceptor.attach(base.add(0xfcc20),{onEnter(){
  if(!simulationThread())return;this.observed=true;exitInProgress=true;
  try{
   stopHooks();const handle=p2.add(0xd8).readPointer();let before=null;
   if(!handle.isNull()){
    if(exitStarted||!gameplayState()||!p2Identity||!handle.equals(p2Identity.node)||!live(p2Identity)||!p2Identity.actor.readPointer().equals(base.add(0x52d198)))throw Error('No native Reset of an uncertain or post-world-exit handle during shutdown');
    before=graph(p2);callOnce('context_unbind_before_native_shutdown',()=>reset(p2));
   }
   if(!p2.add(0xd8).readPointer().isNull())throw Error('External context still bound at native shutdown');
   event('C_context_native_shutdown_quiesced',{native_rva:'0xfcc20',before,external_handle:'0x0',guards:guards(),native_calls:nativeCalls,storage_pinned_until_process_exit:true});
  }catch(e){event('C_context_native_shutdown_error',{error:String(e),native_calls:nativeCalls,note:'Keep context/code pinned; no uncertain native retry or post-teardown Reset.'});}
 },onLeave(){if(this.observed)event('C_native_shutdown_returned',{external_handle:p2.add(0xd8).readPointer().toString(),actor_reads:false});}});
 return {completed(){return completed;}};
}
