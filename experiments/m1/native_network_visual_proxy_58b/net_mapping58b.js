// DOC58B pure mapping between the DLL seam ABI and the DOC58A controller. No Frida/KEX access.
const NetMapping58B={
 abi:0x58b00001,
 // Host Player origin is at the feet (floor 1152 in Tomb 259); stock Actor1007 was attested at z 1240 (DOC57/58A).
 // The fixed +88 is a presentation offset for this one Actor/area, not a general Player->Actor mapping.
 target(c,zOffset){return [c.x,c.y,Math.fround(c.z+zOffset)];},
 // Same normalization as DOC58A movement_native apply('yaw').
 normalizeYaw(v){return Math.fround(((v+Math.PI)%(2*Math.PI)+2*Math.PI)%(2*Math.PI)-Math.PI);},
 distance(a,b){return Math.hypot(...a.map((v,i)=>v-b[i]));},
 // DOC58A C02 proved native_step (no reset, KEX lerp) for 132 units; larger jumps use teleport + reset1d3e60.
 mode(from,to,limit){return this.distance(from,to)<=limit?'native_step':'teleport';},
 // Controller statuses that guarantee no KEX write and keep the controller usable.
 refused:new Set(['OUT_OF_ATTESTED_AREA','NATIVE_POINT_IN_SECTOR_FALSE','FLOOR_CEILING_CACHE_MISMATCH','PHYSICS_BIT7_SET','AREA_NOT_ATTESTED','NONFINITE_TARGET','INVALID_POSITION','INVALID_YAW','WRONG_THREAD','STATE_EXIT','CROSS_SECTOR_DEFERRED']),
 refusedPrefix:['TARGET_ALSO_IN_'],
 // Seam codes: 0 UNKNOWN, Update 1 Applied 2 Unchanged 3 Refused 4 Stopped.
 updateCode(status){if(status.ok)return 1;if(this.refused.has(status.reason)||this.refusedPrefix.some(p=>String(status.reason).startsWith(p)))return 3;return 4;},
 // Remove 1 Requested 2 DefiniteNoCall 0 Unknown: only a call that provably never reached KEX is 2.
 removeCode(status,removeAttempted){if(status.ok)return 1;return removeAttempted?0:2;},
 // Retirement 1 Retired 2 Pending 3 Failed 0 Unknown.
 retirementCode(phase){return {retired:1,removing:2,pending:2,retiring:2,blocked_unsafe:3}[phase]||0;},
 readCommand(p){return {abi:p.readU32(),slot:p.add(4).readU32(),sequence:p.add(8).readU32(),server_tick:p.add(12).readU32(),map_epoch:p.add(16).readU32(),connection_generation:p.add(20).readU32(),entity_id:p.add(24).readU32(),entity_generation:p.add(28).readU32(),x:p.add(32).readFloat(),y:p.add(36).readFloat(),z:p.add(40).readFloat(),yaw:p.add(44).readFloat(),pitch:p.add(48).readFloat(),coalesced:p.add(52).readU32()};}
};
if(typeof module!=='undefined')module.exports={NetMapping58B};
