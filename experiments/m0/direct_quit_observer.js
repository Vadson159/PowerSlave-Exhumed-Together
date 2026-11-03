/* Passive Direct-Quit trace. No teardown calls: context_world_exit performs the
   already validated once-only unbind. Pin all code/data through process exit. */
function createM0DirectQuitObserver() {
 for(const [n,r] of [['engine_shutdown',0xa4c90],['arena_cleanup',0xc58b0],['final_exit',0xfc580]])prefix(n,r);
 const trace={ready:false,shutdown:false,engine_enter:0,engine_return:0,state_exit:0,
  arenas:[],destructors:[],other_arenas_after_quiesce:0,diagnostic_overflow:false,puppet_ticks_after_quiesce:0,state_ticks_after_quiesce:0,final_exit:0,os_exit:[],hooks:null,owned:null};
 function ownSnapshot(){return {player:p2.toString(),external_handle:p2.add(0xd8).readPointer().toString(),guards:guards(),
  trace:JSON.parse(JSON.stringify(trace)),storage_and_script_pinned:true,actor_or_node_reads:false,full_m0:false};}
 function controlBytes(){const result={};for(const name of ['commands','turning','controller_update']){
  const s=cfg.native_prefixes[name],actual=Array.from(new Uint8Array(base.add(s.rva).readByteArray(16)),v=>v.toString(16).padStart(2,'0')).join('');
  result[name]={actual,expected:s.prefix,passed:actual===s.prefix};}
  const getter=Array.from(new Uint8Array(base.add(0x1b5520).readByteArray(5)),v=>v.toString(16).padStart(2,'0')).join('');
  return {sites:result,getter,passed:getter==='488d4140c3'&&Object.values(result).every(v=>v.passed)};
 }
 // Registered after the validated context unbind listener. No Actor access here.
 Interceptor.attach(base.add(0xfcc20),{onEnter(args){if(!simulationThread())return;
  trace.shutdown=true;trace.hooks=controlBytes();
  const platform=args[0].readPointer(),engine=base.add(0xaec370).readPointer();
  event('C_direct_quit_shutdown_entry',{platform:args[0].toString(),platform_vtable:platform.toString(),final_target:platform.add(0x80).readPointer().toString(),
   engine_vtable:engine.toString(),engine_shutdown_target:engine.add(0x10).readPointer().toString(),snapshot:ownSnapshot()});
 }});
 Interceptor.attach(base.add(0xa4c90),{onEnter(args){this.quitEngine=false;if(!trace.ready)return;trace.engine_enter++;this.quitEngine=true;
  event('C_direct_quit_engine_shutdown_entry',{this_pointer:args[0].toString(),snapshot:ownSnapshot()});
 },onLeave(){if(this.quitEngine){trace.engine_return++;event('C_direct_quit_engine_shutdown_return',{snapshot:ownSnapshot()});}}});
 Interceptor.attach(base.add(0xc58b0),{onEnter(args){this.quitArena=null;if(!trace.ready)return;
  const rva=args[0].sub(base).toString(),kind=['0xaf4490','0xaf4600'].includes(rva)?'known_actor_arena':['0xaecc20','0xaecca0'].includes(rva)?'known_engine_arena':'other';
  if(kind==='other'){if(trace.shutdown)trace.other_arenas_after_quiesce++;return;}
  if(trace.arenas.length>=16){trace.diagnostic_overflow=true;return;}
  this.quitArena={arena:args[0].toString(),rva,arena_kind:kind,after_shutdown_entry:trace.shutdown,returned:false};trace.arenas.push(this.quitArena);event('C_direct_quit_arena_entry',this.quitArena);
 },onLeave(){if(this.quitArena){this.quitArena.returned=true;event('C_direct_quit_arena_return',this.quitArena);}}});
 Interceptor.attach(base.add(0x1b6670),{onEnter(){if(trace.ready){trace.state_exit++;event('C_direct_quit_state_exit',{count:trace.state_exit,after_shutdown_entry:trace.shutdown});}}});
 // Only compare pointers with cached identities. No post-destructor Actor reads.
 Interceptor.attach(base.add(0x19f210),{onEnter(args){this.quitDestructor=null;if(!trace.ready)return;
  const found=[p1Identity,p2Identity,...retiredPuppets,...referenceCandidates].find(r=>r&&r.actor.equals(args[0]));if(!found)return;
  if(trace.destructors.length>=16){trace.diagnostic_overflow=true;return;}
  this.quitDestructor={actor:args[0].toString(),actor_kind:found.kind,generation:found.generation??null,returned:false};trace.destructors.push(this.quitDestructor);
  event('C_direct_quit_owned_actor_destructor_entry',this.quitDestructor);
 },onLeave(){if(this.quitDestructor){this.quitDestructor.returned=true;event('C_direct_quit_owned_actor_destructor_return',this.quitDestructor);}}});
 Interceptor.attach(base.add(0x1db8a0),{onEnter(args){if(trace.shutdown&&p2Identity&&args[0].equals(p2Identity.actor))trace.puppet_ticks_after_quiesce++;}});
 Interceptor.attach(base.add(0x1b6ec0),{onEnter(){if(trace.shutdown)trace.state_ticks_after_quiesce++;}});
 Interceptor.attach(base.add(0xfc580),{onEnter(){trace.final_exit++;event('C_direct_quit_final_exit',{native_rva:'0xfc580',noreturn:true,snapshot:ownSnapshot()});}});
 for(const [mod,name] of [['kernel32.dll','ExitProcess'],['ntdll.dll','RtlExitUserProcess']]){
  const address=Process.getModuleByName(mod).findExportByName(name);if(address)Interceptor.attach(address,{onEnter(args){if(!trace.shutdown)return;
   trace.os_exit.push({api:mod+'!'+name,code:args[0].toUInt32()});event('C_direct_quit_os_exit',{api:mod+'!'+name,code:args[0].toUInt32(),snapshot:ownSnapshot()});
  }});
 }
 return {tick(){if(trace.ready||frames<120)return;
  const primary=graph(local),secondary=graph(p2);if(!primary.live_identity||!secondary.live_identity||primary.health!==200||secondary.health!==200||!guards())throw Error('Direct Quit fixture not independently live');
  trace.ready=true;trace.owned={primary:{actor:primary.actor,node:primary.node},secondary:{actor:secondary.actor,node:secondary.node}};
  event('C_direct_quit_ready',{primary,secondary,controller:controller.counts(),guards:guards(),policy:'Native UI Quit while P2 alive; never force StateExit/arena calls or hot unload during shutdown'});
 }};
}
