// Bounded technical controller. Adapter is private; callers receive statuses only.
class NativeVisualProxyMovementController {
 #api; #started=false; #stopped=false; #attempted=false; #removed=false; #identity=null;
 constructor(api){this.#api=api;}
 #status(ok,reason,details={}){return {ok,reason,...details};}
 #scope(){try{if(!this.#api.onSimulationThread())return 'WRONG_THREAD';if(this.#api.stateExitPending())return 'STATE_EXIT';return null;}catch(e){return 'SCOPE_UNKNOWN';}}
 #guard(){const s=this.#scope();if(s)return s;if(!this.#started)return 'NOT_STARTED';if(this.#removed)return 'REMOVED';if(this.#stopped)return 'STOPPED';if(!this.#identity)return 'NOT_SPAWNED';try{if(!this.#api.identityMatches(this.#identity))return 'STALE_IDENTITY';}catch(e){this.#stopped=true;return 'IDENTITY_UNKNOWN';}return null;}
 start(){const s=this.#scope();if(s)return this.#status(false,s);if(this.#stopped||this.#removed)return this.#status(false,'TERMINAL');this.#started=true;return this.#status(true,'STARTED');}
 spawn(){const s=this.#scope();if(s)return this.#status(false,s);if(!this.#started||this.#stopped)return this.#status(false,'NOT_STARTED');if(this.#attempted)return this.#status(false,'SPAWN_ALREADY_ATTEMPTED');this.#attempted=true;try{this.#identity=this.#api.spawnOnce();return this.#status(!!this.#identity,this.#identity?'SPAWNED':'SPAWN_FAILED');}catch(e){this.#stopped=true;return this.#status(false,'SPAWN_FAILED',{error:String(e)});}}
 #operation(name,value,mode){const s=this.#guard();if(s)return this.#status(false,s);let v;
  try{if(!this.#api.interpolationValid())return this.#status(false,'INVALID_INTERPOLATION');v=this.#api.validate(name,value,mode);if(!v.ok)return v;}
  catch(e){this.#stopped=true;return this.#status(false,'VALIDATION_UNKNOWN',{error:String(e)});}
  try{this.#api.apply(name,v.value,mode);if(!this.#api.checkInvariants())throw Error('postcondition');return this.#status(true,'APPLIED',{operation:name,value:v.value,mode});}
  catch(e){this.#stopped=true;return this.#status(false,'MUTATION_FAILED_STOPPED',{error:String(e)});}}
 setPosition(xyz,mode='teleport'){if(!['teleport','native_step'].includes(mode))return this.#status(false,'UNKNOWN_MODE');if(!Array.isArray(xyz)||xyz.length!==3||!xyz.every(Number.isFinite))return this.#status(false,'INVALID_POSITION');return this.#operation('position',xyz.slice(),mode);}
 setYaw(yaw){if(!Number.isFinite(yaw)||Math.abs(yaw)>Math.PI*8)return this.#status(false,'INVALID_YAW');return this.#operation('yaw',yaw,'teleport');}
 moveToSector(sector){if(!Number.isInteger(sector))return this.#status(false,'UNKNOWN_SECTOR');return this.#operation('sector',sector,'teleport');}
 stop(){const s=this.#scope();if(s)return this.#status(false,s);this.#stopped=true;return this.#status(true,'STOPPED');}
 remove(){const s=this.#scope();if(s)return this.#status(false,s);if(this.#removed)return this.#status(false,'DOUBLE_REMOVE');if(!this.#identity)return this.#status(false,'NOT_SPAWNED');try{if(!this.#api.identityMatches(this.#identity))return this.#status(false,'STALE_IDENTITY');}catch(e){return this.#status(false,'IDENTITY_UNKNOWN',{error:String(e)});}
  this.#removed=true;this.#stopped=true;try{this.#api.removeOnce();this.#identity=null;return this.#status(true,'REMOVE_REQUESTED');}catch(e){return this.#status(false,'REMOVE_FAILED_NO_RETRY',{error:String(e)});}}
 retire(){this.#identity=null;this.#removed=true;this.#stopped=true;}
}
if(typeof module!=='undefined')module.exports={NativeVisualProxyMovementController};
