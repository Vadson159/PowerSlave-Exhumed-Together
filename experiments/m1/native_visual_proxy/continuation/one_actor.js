// Shared one-shot state for the native probe and the benign regression fixture.
// API owns all engine-specific validation; this class never searches for a lost pointer.
class OneActor57C {
 constructor(api){this.api=api;this.phase='waiting';this.spawnAttempted=false;this.removeAttempted=false;this.actor=null;this.node=null;this.retireNode=null;this.actorId='';this.nodeId='';this.identitySafe=false;this.spawnCalls=0;this.removeCalls=0;this.simulationTicks=0;this.actorTicks=0;this.lastStamp=null;this.faults=[];this.cleanupReason=null;}
 log(event,data={}){try{this.api.emit(event,data);}catch(e){this.faults.push('journal: '+String(e));if(this.identitySafe&&!this.removeAttempted)this.cleanupReason='journal_failure';}}
 snapshot(){return {phase:this.phase,spawn_calls:this.spawnCalls,remove_calls:this.removeCalls,simulation_ticks:this.simulationTicks,actor_tick_callbacks:this.actorTicks,actor_id:this.actorId,node_id:this.nodeId,identity_safe:this.identitySafe,cleanup_reason:this.cleanupReason,faults:this.faults.slice()};}
 create(){
  if(this.spawnAttempted){this.log('spawn_duplicate_ignored',this.snapshot());return false;}
  this.spawnAttempted=true;this.phase='spawning';this.spawnCalls++;
  try{
   // Capture the real return before ANY diagnostic event/predicate or graph access.
   this.actor=this.api.spawn();this.actorId=this.actor.toString();
  }catch(e){this.phase='blocked_unsafe';this.faults.push('factory_exception: '+String(e));this.log('factory_call_exception',this.snapshot());return false;}
  this.log('factory_return',{returned:this.actorId,pointer_saved:true,spawn_calls:this.spawnCalls});
  if(this.actor.isNull()){this.actor=null;this.phase='blocked_unsafe';this.faults.push('factory_null_return');this.log('factory_null_return',this.snapshot());return false;}
  try{
   const id=this.api.validate(this.actor);this.node=id.node;this.nodeId=this.node.toString();this.identitySafe=true;this.phase='live';
   this.log('spawn_identity_verified',Object.assign(this.snapshot(),id.facts));
  }catch(e){this.phase='blocked_unsafe';this.faults.push('identity_validation: '+String(e));this.log('identity_not_safe_for_remove',this.snapshot());return false;}
  try{
   // Missing optional listener is an observability fact, never Factory==null.
   this.log('factory_diagnostic',this.api.diagnostic?this.api.diagnostic():{listener_required:false});
  }catch(e){this.faults.push('diagnostic: '+String(e));this.cleanupReason='diagnostic_failure';this.log('diagnostic_failure_cleanup_requested',this.snapshot());}
  return true;
 }
 simulationStep(stamp){
  if(this.phase!=='live'||stamp===this.lastStamp)return false;
  this.lastStamp=stamp;this.simulationTicks++;return true;
 }
 actorTick(){if(this.phase==='live')this.actorTicks++;}
 requestCleanup(reason){if(this.identitySafe&&!this.removeAttempted)this.cleanupReason=reason;}
 cleanup(){
  if(this.removeAttempted||!this.cleanupReason)return false;
  if(!this.identitySafe||!this.actor){this.phase='blocked_unsafe';this.log('cleanup_refused_no_identity',this.snapshot());return false;}
  try{this.api.validateRemoval(this.actor,this.node);}catch(e){this.phase='blocked_unsafe';this.faults.push('unsafe_cleanup: '+String(e));this.log('cleanup_refused',this.snapshot());return false;}
  this.removeAttempted=true;this.removeCalls++;this.phase='removing';this.log('remove_request',this.snapshot());
  try{this.api.remove(this.actor);}catch(e){this.phase='blocked_unsafe';this.faults.push('remove_exception_no_retry: '+String(e));this.log('remove_call_exception',this.snapshot());return false;}
  this.phase='pending';this.log('remove_return',this.snapshot());return true;
 }
 poolReturn(ptr){
  if(!this.actor||!ptr.equals(this.actor))return false;
  // Native pool-return is the end of observable Actor identity. Invalidate first.
  this.retireNode=this.node;this.actor=null;this.node=null;this.identitySafe=false;this.phase='retiring';
  this.log('actor_identity_retired',Object.assign(this.snapshot(),{actor_dereferenced:false}));return true;
 }
 finishRetirement(facts){
  if(this.phase!=='retiring'||!this.retireNode)return false;
  this.retireNode=null;
  if(!facts.payload_null||facts.refs!==0||facts.in_use!==0||facts.sector_occurrences!==0){this.phase='blocked_unsafe';this.faults.push('node_retirement_postcondition');this.log('node_retirement_failed',Object.assign(this.snapshot(),facts));return false;}
  this.phase='retired';this.log('deferred_retired',Object.assign(this.snapshot(),facts));return true;
 }
}
