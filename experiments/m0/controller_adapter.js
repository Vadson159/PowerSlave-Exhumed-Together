/* Experimental command seam, not recovered engine source. Native calls remain on simulation thread. */
function createControllerAdapter(base,p1,p2,options={}) {
 if(!Number.isInteger(options.simulationTid)||!options.prefixes||!options.gameState)throw new Error('Controller requires observed simulation TID, game state and exact-build prefixes');
 const state=Memory.alloc(240);state.writeByteArray(new Uint8Array(240));state.add(24).writePointer(p1);state.add(32).writePointer(p2);
 state.add(80).writeU32(options.preserveLocalInput?1:0);state.add(84).writeU32(options.simulationTid);
 state.add(112).writeU32(options.skipDeadSecondary?1:0);
 const cm=new CModule(`
 typedef unsigned int U32;
 typedef void (*Update)(void *);
 extern unsigned char state[];
 extern unsigned char game_state[];
 extern U32 current_tid(void);
 #define U(o) (*(volatile U32 *)(state+(o)))
 #define P(o) (*(void **)(state+(o)))
 static int simulation_thread(void) { return current_tid()==U(84); }
 static int gameplay(void) { return simulation_thread() && *(volatile U32 *)game_state==3 && *(volatile U32 *)(game_state+4)==0; }
 static int controlled(void *p) { return gameplay() && U(40) && (p==P(32)||(p==P(24)&&!U(80))); }
 static int secondary_skip(void) {
  unsigned char *player=P(32),*node=*(void **)(player+0xd8),*actor;
  if (!node || node!=P(88)) { U(108)++;return 1; }
  actor=*(void **)(node+0x28);
  if (!actor || actor!=P(96) || *(void **)(actor+0x10)!=node || *(void **)(actor+0x1b8)!=player) { U(108)++;return 1; }
  /* Saved OnDamage1dbd00 sets PF_DEAD; PlayerUpdate1dfe30 otherwise reaches global gameover after 60 dead updates. */
  if (U(112) && (*(U32 *)(actor+0x1c0)&0x200)) { U(104)++;return 1; }
  return 0;
 }
 void commands(void *p) {
  unsigned char *b=p;
  if (!controlled(p)) { ((Update)P(8))(p);return; }
  int second=p==P(32);U32 bits=U(second?52:48);*(U32 *)b=bits;
  /* Native Player Update builds key-duration counters before this seam and may
     overwrite the previous Player values from hardware. Keep synthetic durations
     in adapter-owned memory; a late first press must still be tick1. */
  for (int i=0;i<13;i++) {
   U32 *held=(U32 *)(state+(second?180:128)+i*4);
   if (bits&(1u<<i)) { if (*held<0x7fffffffu) (*held)++; } else *held=0;
   *(U32 *)(b+4+i*4)=*held;
  }
  for (int o=0x38;o<=0x7c;o+=4) *(U32 *)(b+o)=0;
  *(float *)(b+0x38)=*(float *)(state+(second?60:56));
  U(second?76:72)++;
 }
 void turning(void *p) { if (!controlled(p)) ((Update)P(16))(p); }
 void controller(void *p) {
  if (gameplay() && U(40) && p==P(32) && secondary_skip()) return;
  ((Update)P(0))(p);
  if (gameplay() && U(40) && p==P(24)) { U(64)++;if (!secondary_skip()) { ((Update)P(0))(P(32));U(68)++; } }
 }
 `,{state,game_state:options.gameState,current_tid:Module.getGlobalExportByName('GetCurrentThreadId')});
 const addresses=[0x1e34a0,0x1e2ad0,0x1dfe30].map(r=>base.add(r));
 const originals=[],names=['commands','turning','controller_update'];let installed=false,stopped=false;
 function safeThread(){if(Process.getCurrentThreadId()!==options.simulationTid)throw new Error('Controller mutation outside observed simulation thread');}
 // Preparation allocates only adapter-owned state/code. Hooks are installed later at state-tick exit.
 return {state,cm,originals,
  bindIdentity(actor,node){safeThread();if(installed)throw new Error('Bind secondary identity before controller install');
   if(node.isNull()||actor.isNull()||!p2.add(0xd8).readPointer().equals(node)||!node.add(0x28).readPointer().equals(actor)||
    !actor.add(0x10).readPointer().equals(node)||!actor.add(0x1b8).readPointer().equals(p2))throw new Error('Secondary controller identity mismatch');
   state.add(88).writePointer(node);state.add(96).writePointer(actor);},
  install(){safeThread();if(installed)return;if(stopped)throw new Error('Stopped controller adapter cannot be reinstalled');
   addresses.forEach((a,i)=>{const s=options.prefixes[names[i]];
    if(!s||s.rva!==a.sub(base).toUInt32()||typeof s.prefix!=='string'||s.prefix.length!==32)throw new Error('Missing controller prefix '+names[i]);
    const actual=Array.from(new Uint8Array(a.readByteArray(16)),x=>x.toString(16).padStart(2,'0')).join('');
    if(actual!==s.prefix)throw new Error('Controller prefix mismatch '+names[i]);});
   try{
    originals[0]=Interceptor.replaceFast(addresses[0],cm.commands);state.add(8).writePointer(originals[0]);
    originals[1]=Interceptor.replaceFast(addresses[1],cm.turning);state.add(16).writePointer(originals[1]);
    originals[2]=Interceptor.replaceFast(addresses[2],cm.controller);state.writePointer(originals[2]);Interceptor.flush();installed=true;
   }catch(e){state.add(40).writeU32(0);addresses.slice(0,originals.length).reverse().forEach(a=>Interceptor.revert(a));Interceptor.flush();stopped=true;throw e;}
  },
  enable(){safeThread();if(!installed||stopped||state.add(88).readPointer().isNull()||state.add(96).readPointer().isNull())throw new Error('Controller hooks/secondary identity are not ready');state.add(40).writeU32(1);},
  set(frame,b1,b2,y1=0,y2=0){safeThread();state.add(44).writeU32(frame);state.add(48).writeU32(b1);state.add(52).writeU32(b2);state.add(56).writeFloat(y1);state.add(60).writeFloat(y2);},
  counts(){return {p1_controller:state.add(64).readU32(),p2_controller:state.add(68).readU32(),p1_command:state.add(72).readU32(),p2_command:state.add(76).readU32(),
   p2_dead_skips:state.add(104).readU32(),p2_identity_skips:state.add(108).readU32()};},
  stop(){safeThread();state.add(40).writeU32(0);if(stopped)return;
   if(installed){addresses.slice().reverse().forEach(a=>Interceptor.revert(a));Interceptor.flush();installed=false;}stopped=true;}
 };
}
