// Cleanup ONLY of the already returned Actor. No factory, search, relocation or flag writes.
// Main probe stopped on a false constant sector-count assumption after P1 left sector259.
let actor=ptr(cfg.existing_actor),node=ptr(cfg.existing_node),retireNode=null;
let inTick=false,inSweep=false,stopping=false,removed=false,retired=false,failed=false,p1ThisCall=0;
let steps=0,lastStamp=null,postSteps=0,actorCallbacks=0,removeCalls=0,requestStamp=null,afterSubmissions=0;
const nativeRemove=new NativeFunction(base.add(cfg.sites.remove.rva),'void',['pointer'],{abi:'win64',exceptions:'propagate'});
function note(event,fields={}){try{emit(event,Object.assign({engine_stamp:base.add(0xaec390).readU32(),cleanup_only:true},fields));}catch(e){failed=true;}}
function primaryOkay(p){return validPrimary(p)&&equal(graph(p),cfg.original_graph)&&equal(inventory(p),cfg.original_inventory);}
function identity(){
 if(stopping||!thread()||!inTick||!stableWorld(world())||!actor||!node)throw Error('cleanup world/affinity');
 const p=primary();if(!primaryOkay(p))throw Error('cleanup P1 graph/inventory');
 const id=actorFacts(actor),f=id.facts,m=list(node);
 // Membership count follows the actual P1 sector; no assumption that P1 stayed259.
 const expectedCount=p.sector===259?2:1;
 if(!id.node.equals(node)||f.actor!==cfg.existing_actor||f.node!==cfg.existing_node||f.payload!==cfg.existing_actor||f.vtable!==base.add(0x528608).toString()||f.type!==1007||f.sector!==259||!equal(f.position,cfg.position)||f.flags!==0x40||f.removed!==0||f.refs!==0||f.in_use!==1||f.scale!==1.5||f.target!=='0x0'||f.source_owner!=='0x0'||!equal(f.bounds_min,[-12,-12,-24])||!equal(f.bounds_max,[12,12,24])||m.found!==1||m.count!==expectedCount||m.entries.some(e=>e.node!==cfg.existing_node&&e.node!==p.node))throw Error('cleanup direct Actor/node/list validation');
 return {actor:f,sector_list:m,primary:p,p1_inventory_unchanged:true,expected_count:expectedCount};
}
function stop(reason){failed=true;note('cleanup_blocked',{reason,remove_calls:removeCalls,retired});}
Interceptor.attach(base.add(cfg.sites.puppet_tick.rva),{onEnter(args){if(!thread()||!inTick||stopping)return;const p=primary();if(p&&args[0].toString()===p.actor)p1ThisCall++;}});
Interceptor.attach(base.add(cfg.sites.actor_tick.rva),{onEnter(args){if(actor&&args[0].equals(actor))actorCallbacks++;}});
Interceptor.attach(base.add(cfg.sites.on_remove.rva),{onEnter(args){this.ours=actor&&args[0].equals(actor);if(this.ours)note('cleanup_on_remove_entry',{actor:cfg.existing_actor,node:cfg.existing_node});},onLeave(){if(this.ours&&actor)note('cleanup_on_remove_return',{own_node_cleared:actor.add(0x10).readPointer().isNull()});}});
Interceptor.attach(base.add(cfg.sites.pool_return.rva),{onEnter(args){
 this.ours=actor&&args[1].equals(actor);if(!this.ours)return;
 const valid=thread()&&inTick&&inSweep&&removed;
 const facts=valid?{own_node_cleared:actor.add(0x10).readPointer().isNull(),refs:node.add(0x40).readS32(),factory:args[0].toString(),return_address:this.returnAddress.toString()}:{};
 retireNode=valid?node:null;actor=null;node=null;
 note('cleanup_pool_return_entry',Object.assign(facts,{valid_scope:valid,actor_identity_invalidated:true,actor_reads_after_entry:false}));if(!valid)stop('unexpected pool return scope');
 },onLeave(){if(this.ours)note('cleanup_pool_return_complete',{actor_dereferenced:false});}});
Interceptor.attach(base.add(cfg.sites.sweep.rva),{onEnter(){this.ours=thread()&&inTick;if(this.ours)inSweep=true;},onLeave(){if(!this.ours)return;try{
 if(retireNode){const n=retireNode,m=list(n);const facts={payload_null:n.add(0x28).readPointer().isNull(),refs:n.add(0x40).readS32(),in_use:n.add(0x24).readU8(),sector_occurrences:m.found,sector_list:m};retireNode=null;
 if(!facts.payload_null||facts.refs!==0||facts.in_use!==0||facts.sector_occurrences!==0)throw Error('node retirement postcondition');retired=true;note('cleanup_deferred_retired',Object.assign(facts,{all_weak_pointers_null:actor===null&&node===null&&retireNode===null,remove_calls:removeCalls}));}
 }catch(e){retireNode=null;stop(String(e));}finally{inSweep=false;}}});
Interceptor.attach(base.add(cfg.sites.drain.rva),{onEnter(){this.ours=thread()&&inTick&&removed&&node;if(this.ours)note('cleanup_drain_entry',{refs:node.add(0x40).readS32(),sector_list:list(node)});},onLeave(){if(this.ours&&node)note('cleanup_drain_return',{refs:node.add(0x40).readS32(),in_use:node.add(0x24).readU8(),payload:node.add(0x28).readPointer().toString(),sector_list:list(node)});}});
Interceptor.attach(base.add(cfg.sites.sprite.rva),{onEnter(args){if(!removed||stopping)return;const e=args[2];if(e.readPointer().toString()===cfg.existing_frame&&equal(vec(e.add(0x10)),cfg.position)&&e.add(0x50).readS32()===259){afterSubmissions++;note('cleanup_sprite_after_remove',{count:afterSubmissions});}}});
Interceptor.attach(base.add(cfg.sites.tick.rva),{onEnter(args){this.ours=thread()&&args[0].equals(gi);if(this.ours){inTick=true;p1ThisCall=0;}},onLeave(){if(!this.ours)return;try{
 if(stopping||failed||p1ThisCall===0)return;
 const w=world(),p=primary();if(!stableWorld(w)||!primaryOkay(p))throw Error('cleanup stable world/P1 lost');
 const stamp=base.add(0xaec390).readU32();if(stamp===lastStamp)return;lastStamp=stamp;steps++;
 if(!removed){const facts=identity();if(steps===1)note('cleanup_identity_revalidated',facts);
 if(steps>=12){
   // Mark single-shot before native call; NEVER retry a failed Remove.
   removed=true;removeCalls++;requestStamp=stamp;note('cleanup_remove_request',Object.assign(facts,{remove_calls:removeCalls,additional_observed_world_steps:steps,additional_actor_callbacks:actorCallbacks}));
   nativeRemove(actor);
   const after={flags:actor.add(0x16c).readU32(),removed:actor.add(0x40).readU8(),refs:node.add(0x40).readS32(),sector_list:list(node),primary:primary()};
   note('cleanup_remove_return',after);if(after.removed!==1||(after.flags&0x2000)===0||!primaryOkay(after.primary))throw Error('Remove postcondition');
 }
 }else if(retired){postSteps++;if(postSteps<=3||postSteps%30===0)note('cleanup_post_retire',{primary:p,inventory_unchanged:true,post_retire_world_steps:postSteps,all_weak_pointers_null:actor===null&&node===null&&retireNode===null,sector_list:list(),sprite_submissions_after_remove:afterSubmissions,actor_reads:false});}
 else if(stamp-requestStamp>120)throw Error('retirement timeout');
 }catch(e){stop(String(e));}finally{inTick=false;}}});
Interceptor.attach(base.add(cfg.sites.state_exit.rva),{onEnter(){if(gi.add(0x24).readS32()===3&&gi.add(0x20).readS32()!==3){stopping=true;actor=null;node=null;retireNode=null;if(!retired)stop('teardown before retirement');note('cleanup_world_exit',{retired,all_weak_pointers_invalidated:true});}}});
note('cleanup_only_loaded',{pid:Process.id,known_actor:cfg.existing_actor,known_node:cfg.existing_node,source:'actual factory_return + spawn_identity_verified + fresh read-only witness; no memory search',factory_calls:0,game_field_writes:0,simulation_tid:cfg.simulation_tid});
