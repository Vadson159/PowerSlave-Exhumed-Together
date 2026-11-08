// P1 read-only observer. KEX control, Actor identity and lifetime stay inside C++.
// No Actor/ObjectNode pointer API is added. The journal-bound owned Actor is never
// dereferenced; its ObjectNode and other live sector Actors are read for a census.
// PSE_P1_ReadObservation copies a 128-byte DTO only on the attested Sim/render TID.
// Only read-only DTO and candidate-comparison exports are called. No NativeCallback,
// game stores or control hooks are present.
'use strict';
if (Process.id !== cfg.pid || Process.mainModule.path.toLowerCase() !== cfg.target.toLowerCase())
 throw Error('P1 observer PID/target mismatch');
const base = Process.mainModule.base, gi = base.add(0xaf33a0), local = base.add(0xaf33e0);
const dll = Process.getModuleByName(cfg.dll_name);
if (dll.path.toLowerCase() !== cfg.dll_path.toLowerCase()) throw Error('P1 observer DLL mismatch');
function bytes(p,n) { return Array.from(new Uint8Array(p.readByteArray(n))).map(v=>v.toString(16).padStart(2,'0')).join(''); }
function vec(p) { return [p.readFloat(),p.add(4).readFloat(),p.add(8).readFloat()]; }
function bits(p) { return [p.readU32(),p.add(4).readU32(),p.add(8).readU32()]; }
function equal(a,b) { return JSON.stringify(a) === JSON.stringify(b); }
function sim() { return Process.getCurrentThreadId() === cfg.simulation_tid; }
function stable() { return gi.add(0x20).readS32()===3 && gi.add(0x24).readS32()===3 && gi.add(0x28).readS32()===0; }
for (const name of (cfg.mode==='render'?['scene','sprite','interpolant_lerp']:['scene'])) {
 const s=cfg.sites[name]; if (bytes(base.add(s.rva),16)!==s.prefix) throw Error('P1 observer prefix mismatch: '+name);
}
const journal=new File(cfg.log,'w');
let serial=0,frames=0,sprites=0,matching=0,intermediate=0,lerps=0,foreign=0,errors=0,invalidated=0;
let observationQueries=0,lerpFilterCalls=0,lerpFilterRejected=0,spriteFilterRejected=0;
let oursSprites=0,ambiguousSprites=0,unknownIdentitySprites=0;
let lastPhase=null,lastLerp=null,lastSpriteRevision=null,baseline=null,lastSnapshotFrame=-1,cachedFrame=null;
const output=Memory.alloc(128);
const observation=new NativeFunction(dll.getExportByName(cfg.observation_export),'uint32',['pointer','uint32'],'win64');
const filterNative=cfg.mode==='render'?new NativeFunction(dll.getExportByName('PSE_P1_IsPositionInterpolant'),'uint32',['pointer'],'win64'):null;
const ownedActor=cfg.mode==='render'?ptr(cfg.owned_actor):null,ownedNode=cfg.mode==='render'?ptr(cfg.owned_node):null;
if(cfg.mode==='render'&&(ownedActor.isNull()||ownedNode.isNull()||ownedActor.equals(ownedNode)))throw Error('Missing diagnostic owned pair');
function emit(event,data={},quiet=false) {
 const d=Object.assign({event,record:++serial,pid:Process.id,tid:Process.getCurrentThreadId(),utc:new Date().toISOString()},data);
 journal.write(JSON.stringify(d)+'\n'); journal.flush(); if (!quiet) send(d);
}
function readObservation() {
 if (!sim()) { foreign++; return null; }
 observationQueries++;const code=observation(output,128);
 // ERROR_INVALID_STATE is a normal filled, invalid DTO after retirement/unload.
 // Parse its header/phase and count invalidation, without treating it as a probe error.
 if (code!==0 && code!==5023) { if (errors++<8) emit('observation_refused',{code}); return null; }
 if (output.readU32()!==128 || output.add(4).readU32()!==cfg.observation_abi) throw Error('observation ABI mismatch');
 const phase=output.add(8).readU32(), valid=output.add(12).readU32()===1;
 if (phase!==lastPhase) { lastPhase=phase;emit('native_observation_phase',{phase,identity_valid:valid,proxy_actor_reads_in_js:0}); }
 if (code===5023 || !valid || phase!==2 || output.add(120).readU32()!==0 || output.add(124).readU32()!==0) { invalidated++;return null; }
 if (output.add(112).readU32()!==cfg.simulation_tid || output.add(116).readU32()!==Process.getCurrentThreadId())
  throw Error('observation thread mismatch');
 return {phase,identity_valid:valid,handle:output.add(16).readU64().toString(),sequence:output.add(24).readU32(),
  server_tick:output.add(28).readU32(),revision:output.add(32).readU32(),engine_stamp:output.add(36).readU32(),
  frame_token:output.add(40).readPointer().toString(),origin:vec(output.add(48)),origin_bits:bits(output.add(48)),
  render:vec(output.add(60)),render_bits:bits(output.add(60)),angles:vec(output.add(72)),angles_bits:bits(output.add(72)),
  previous:vec(output.add(84)),previous_bits:bits(output.add(84)),yaw:output.add(96).readFloat(),scale:output.add(100).readFloat(),
  sector:output.add(104).readS32(),flags:output.add(108).readU32(),sim_tid:output.add(112).readU32(),observed_tid:output.add(116).readU32()};
}
// All work below is synchronous JS reads following a fresh, valid Live DTO on
// the same simulation thread. It calls no DLL export/native engine routine.
// The owned Actor pointer is compared only; only its ObjectNode is dereferenced.
function spriteCensus(entry,o) {
 const result={status:'UNKNOWN',sector:o?o.sector:-1,complete:false,walked:0,other_actors:0,own_occurrences:0,
  other_matching_candidates:0,duplicates:[],unknown:[],proxy_actor_reads_in_js:0,own_object_node_reads:true,other_actor_reads:true};
 function require(ok,reason){if(!ok)throw Error(reason);}
 const floatCarrier=new DataView(new ArrayBuffer(4));
 function floatBits(value){floatCarrier.setFloat32(0,value,true);return floatCarrier.getUint32(0,true);}
 try {
  require(sim()&&stable()&&o&&o.phase===2&&o.identity_valid&&o.sim_tid===cfg.simulation_tid&&o.observed_tid===Process.getCurrentThreadId(),'LIVE_SIM_SCOPE');
  require(o.sector>=0&&entry.sector===o.sector,'SECTOR_MISMATCH');
  const count=base.add(0xa45970).readU64().toNumber(),sectors=base.add(0xa45968).readPointer();
  require(count===270&&!sectors.isNull(),'SECTOR_GEOMETRY_UNKNOWN');
  const sector=sectors.add(o.sector*0x88);require(sector.add(0x80).readS32()===o.sector,'SECTOR_SELF_INDEX');
  const head=sector.add(0x78).readPointer();require(!head.isNull()&&!head.equals(ownedActor),'SECTOR_HEAD_UNKNOWN');
  require(head.readPointer().isNull(),'LEADING_SENTINEL');
  let previous=head,node=head.add(8).readPointer();const seen=new Set();
  for(;;) {
   require(!node.isNull()&&!node.equals(ownedActor),'NODE_NULL_OR_ACTOR_ALIAS');
   require(!seen.has(node.toString()),'NODE_CYCLE');seen.add(node.toString());
   const next=node.add(8).readPointer();
   require(node.readPointer().equals(previous)&&previous.add(8).readPointer().equals(node),'RECIPROCAL_LINKS');
   if(next.isNull())break;
   require(result.walked<4096,'NODE_LIMIT_4096');result.walked++;
   const payload=node.add(0x28).readPointer(),inUse=node.add(0x24).readU8();
   require(inUse===1&&!payload.isNull(),'NODE_NOT_LIVE');
   if(node.equals(ownedNode)) {
    require(payload.equals(ownedActor),'OWNED_NODE_PAYLOAD');result.own_occurrences++;
   } else if(payload.equals(ownedActor)) {
    // An alias is never classified as an ordinary other Actor, hence never read.
    throw Error('OWNED_ACTOR_PAYLOAD_ON_OTHER_NODE');
   } else {
    result.other_actors++;
    const animation=payload.add(0x108).readPointer(),frameId=payload.add(0x178).readS16();
    require(!animation.isNull()&&frameId>=0&&frameId<animation.add(0x24).readU32(),'OTHER_FRAME_BOUNDS_UNKNOWN');
    const frames=animation.add(0x18).readPointer();require(!frames.isNull(),'OTHER_FRAME_ARRAY_UNKNOWN');
    const frame=frames.add(frameId*0x110).toString(),render=vec(payload.add(0x6c)),zOffset=payload.add(0x128).readFloat();
    require(render.every(Number.isFinite)&&Number.isFinite(zOffset),'OTHER_RENDER_NONFINITE');
    const adjusted=[render[0],render[1],Math.fround(render[2]+zOffset)],renderBits=adjusted.map(floatBits);
    if(frame===entry.frame&&equal(renderBits,entry.origin_bits)) {
     result.other_matching_candidates++;result.duplicates.push({node:node.toString(),actor:payload.toString(),frame,render_output:render,z_offset:zOffset,origin:adjusted,origin_bits:renderBits});
    }
   }
   previous=node;node=next;
  }
  require(result.own_occurrences===1,'OWNED_NODE_OCCURRENCES');result.complete=true;
  result.status=result.other_matching_candidates?'AMBIGUOUS':'OURS';
 }catch(e){result.unknown.push(String(e));result.status='UNKNOWN';}
 return result;
}
const fields=[['ankh_count',0x84,2,'s16'],['ankh_mask',0x86,2,'u16'],['artifacts',0x90,4,'u32'],
 ['abilities',0x94,4,'u32'],['keys',0x98,2,'u16'],['quest_items',0x9a,2,'u16'],['team_dolls',0xa4,4,'u32'],
 ['current_weapon',0x88,4,'s32'],['pending_weapon',0x8c,4,'s32'],['owned_weapons',0xcc,8,'u8x8'],
 ['ammo',0xbc,16,'u16x8'],['health',0x17a,2,'s16','Puppet']];
function snapshotP1(label) {
 if (!sim() || !stable()) return;
 const node=local.add(0xd8).readPointer();if(node.isNull())throw Error('local P1 node missing');
 const actor=node.add(0x28).readPointer();if(actor.isNull())throw Error('local P1 Puppet missing');
 if(!actor.add(0x10).readPointer().equals(node) || !actor.add(0x1b8).readPointer().equals(local) ||
    !actor.readPointer().equals(base.add(0x52d198)) || node.add(0x24).readU8()!==1)throw Error('local P1 reciprocal graph mismatch');
 const identity={player:local.toString(),node:node.toString(),actor:actor.toString(),own_node:actor.add(0x10).readPointer().toString(),
  owner:actor.add(0x1b8).readPointer().toString(),vtable:actor.readPointer().toString()};
 const values={};for (const [name,offset,size,reader,owner] of fields) {
  const p=(owner==='Puppet'?actor:local).add(offset);let decoded;
  if(reader==='s16')decoded=p.readS16();else if(reader==='u16')decoded=p.readU16();else if(reader==='s32')decoded=p.readS32();
  else if(reader==='u32')decoded=p.readU32();else if(reader==='u8x8')decoded=Array.from({length:8},(_,i)=>p.add(i).readU8());
  else decoded=Array.from({length:8},(_,i)=>p.add(i*2).readU16());
  values[name]={raw:bytes(p,size),decoded,status:'OK',size,offset:(owner==='Puppet'?'Puppet':'Player')+'+0x'+offset.toString(16)};
 }
 const snapshot={schema:'P1StateSnapshot-v1',label,status:'OK',binary_sha256:cfg.binary_sha256,pid:Process.id,
  tid:Process.getCurrentThreadId(),runtime_base:base.toString(),native_tick:base.add(0xaec390).readU32(),identity,fields:values,errors:[],
  position:vec(actor.add(0x18)),yaw:actor.add(0x38).readFloat(),sector:actor.add(0x3c).readS32(),game_writes:0};
 emit('p1_state_snapshot',{snapshot});
 if(!baseline)baseline=snapshot;
 else {const differences=[];if(!equal(baseline.identity,identity))differences.push('identity');
  for(const [name] of fields)if(baseline.fields[name].raw!==values[name].raw)differences.push(name);
  emit('p1_state_comparison',{before:baseline.label,after:label,status:differences.length?'DIFFERENT':'EQUAL',differences,compared_fields:fields.map(f=>f[0])});}
}
function error(where,e) { if(errors++<12)emit('observer_error',{where,error:String(e)}); }
Interceptor.attach(base.add(cfg.sites.scene.rva),{onEnter() {
 try {if(!sim()){foreign++;return;}if(!stable())return;frames++;
  if(cfg.mode==='render'){const o=readObservation();cachedFrame=o?o.frame_token:null;}
  if(frames===1||frames-lastSnapshotFrame>=600){snapshotP1(cfg.label+(frames===1?'':'_'+frames));lastSnapshotFrame=frames;}
 }catch(e){error('scene',e);}
}});
if (cfg.mode==='render') {
 Interceptor.attach(base.add(cfg.sites.sprite.rva),{onEnter(args) {
  if(!sim())return;
  try {const p=args[2],frame=p.readPointer().toString();if(!cachedFrame||frame!==cachedFrame){spriteFilterRejected++;return;}
   const o=readObservation();if(!o||frame!==o.frame_token)return;
   const entry={frame:p.readPointer().toString(),origin:vec(p.add(0x10)),origin_bits:bits(p.add(0x10)),angles:vec(p.add(0x28)),
    angles_bits:bits(p.add(0x28)),sector:p.add(0x50).readS32(),flags:p.add(0x4c).readU32(),scale:p.add(8).readFloat()};
   sprites++;const identity=equal(entry.origin_bits,o.render_bits)&&equal(entry.angles_bits,o.angles_bits)&&entry.sector===o.sector&&entry.flags===o.flags&&entry.scale===o.scale;
   if(!identity){if(sprites<=12)emit('sprite_candidate_mismatch',{entry,observation:o});return;}
   matching++;
   const census=spriteCensus(entry,o),spriteIdentity=census.status;
   if(spriteIdentity==='OURS')oursSprites++;else if(spriteIdentity==='AMBIGUOUS')ambiguousSprites++;else unknownIdentitySprites++;
   if(spriteIdentity!=='OURS'&&(ambiguousSprites+unknownIdentitySprites)<=16)emit('sprite_identity_unresolved',{identity:spriteIdentity,census,entry,observation:o});
   const isIntermediate=!equal(o.render_bits,o.origin_bits)&&!equal(o.render_bits,o.previous_bits);
   if(isIntermediate){intermediate++;if(intermediate<=4000)emit('sprite_intermediate_submission',{render_frame:frames,entry,observation:o,
    identity:spriteIdentity,census,last_lerp:lastLerp,correlated_with_last_lerp:!!lastLerp&&equal(lastLerp.output_bits,entry.origin_bits),
    proxy_actor_reads_in_js:0,game_writes:0},true);}
   if(lastSpriteRevision!==o.revision){lastSpriteRevision=o.revision;emit('sprite_submission',{render_frame:frames,entry,observation:o,
    identity:spriteIdentity,census,intermediate:isIntermediate,proxy_actor_reads_in_js:0,game_writes:0},matching>3);}
  }catch(e){error('sprite',e);}
 }});
 Interceptor.attach(base.add(cfg.sites.interpolant_lerp.rva),{onEnter(args) {
  this.before=null;if(!sim())return;try{lerpFilterCalls++;if(filterNative(args[0])!==1){lerpFilterRejected++;return;}this.before=readObservation();}catch(e){error('lerp entry',e);}
 },onLeave(){try{
  const a=this.before;if(!a)return;const b=readObservation();if(!b||a.handle!==b.handle||a.revision!==b.revision||a.sequence!==b.sequence||
    !equal(a.origin_bits,b.origin_bits)||!equal(a.previous_bits,b.previous_bits)||equal(a.render_bits,b.render_bits))return;
  // No interpolant pointer is used. The DLL's own output changed during this one synchronous native call.
  const delta=b.origin.map((x,i)=>x-b.previous[i]),axis=delta.findIndex(x=>Math.abs(x)>.001);if(axis<0)return;
  const t=(b.render[axis]-b.previous[axis])/delta[axis],formulaError=Math.max(...b.render.map((x,i)=>Math.abs(x-(b.previous[i]+delta[i]*t))));
  lerps++;lastLerp={sample:lerps,output:b.render,output_bits:b.render_bits,render_frame:frames,revision:b.revision,sequence:b.sequence,
   previous:b.previous,current:b.origin,t,formula_error:formulaError};
  if(lerps<=6000)emit('native_interpolation_sample',Object.assign({identity:'OWN_DTO_OUTPUT_CHANGED_DURING_NATIVE_CALL',proxy_actor_reads_in_js:0},lastLerp),true);
 }catch(e){error('lerp leave',e);}}});
}
setInterval(()=>emit('observer_summary',{frames,sprite_candidates:sprites,matching_sprites:matching,intermediate_sprites:intermediate,
 ours_sprites:oursSprites,ambiguous_sprites:ambiguousSprites,unknown_identity_sprites:unknownIdentitySprites,
 lerp_samples:lerps,observation_queries:observationQueries,lerp_filter_calls:lerpFilterCalls,lerp_filter_rejected:lerpFilterRejected,
 sprite_filter_rejected:spriteFilterRejected,foreign_callbacks:foreign,errors,invalid_phase_or_identity:invalidated,proxy_actor_reads_in_js:0,game_writes:0,
 native_control_callbacks:0,mode:cfg.mode}),2000);
emit('observer_loaded',{pid:Process.id,image_base:base.toString(),binary_sha256_host_verified:cfg.binary_sha256,expected_tid:cfg.simulation_tid,
 mode:cfg.mode,label:cfg.label,read_only_export:cfg.observation_export,observation_abi:cfg.observation_abi,
 hooks:cfg.mode==='render'?['scene','sprite','interpolant_lerp']:['scene'],interpolant_filter:'PSE_P1_IsPositionInterpolant',proxy_actor_reads_in_js:0,game_writes:0,
 native_control_callbacks:0,own_object_node_reads:cfg.mode==='render',other_actor_reads:cfg.mode==='render',
 uniqueness_scope:'fresh valid Live DTO + one owned current-sector node + complete bounded other-Actor frame/render census'});
