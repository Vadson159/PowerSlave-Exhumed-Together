/* M0-C prerequisite. Observe native initialization; never construct/copy/rebind objects. */
'use strict';
const m=Process.mainModule;
if(Process.id!==cfg.pid || m.path.toLowerCase()!==cfg.target.toLowerCase())throw new Error('C witness target mismatch');
const player=m.base.add(0xaf33e0),vt=m.base.add(0x5137c8);
const prefix=Array.from(new Uint8Array(m.base.add(0x1ae110).readByteArray(16)),b=>b.toString(16).padStart(2,'0')).join('');
if(prefix!==cfg.sites.constructor_witness.prefix)throw new Error('Native initializer prefix mismatch');
function emit(kind,data){send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data));}
function expectedBytes(){
  // Independently encode only stores established in G_FUNC_1401ae110, plus BSS zero initialization.
  // This is a diagnostic model in JS, never written into any target allocation.
  const a=new Uint8Array(0x198);for(let i=0;i<4;i++)a[0x80+i]=255;
  for(const [offset,p] of [[0xe0,player],[0x160,vt],[0x178,vt]]){
    let s=p.toString().slice(2).padStart(16,'0');for(let i=0;i<8;i++)a[offset+i]=parseInt(s.slice(14-i*2,16-i*2),16);
  }return a;
}
let calls=0;
Interceptor.attach(m.base.add(0x1ae110),{onEnter(){this.before=Array.from(new Uint8Array(player.readByteArray(0x198)));},onLeave(ret){
 const a=new Uint8Array(player.readByteArray(0x198)),e=expectedBytes(),differences=[];
 for(let i=0;i<a.length;i++)if(a[i]!==e[i])differences.push({offset:'0x'+i.toString(16),actual:a[i],expected:e[i]});
 emit('C_native_constructor_witness',{ordinal:++calls,returned:ret.toString(),player:player.toString(),bytes_before_hex:this.before.map(x=>x.toString(16).padStart(2,'0')).join(''),
   bytes_after_hex:Array.from(a,x=>x.toString(16).padStart(2,'0')).join(''),size:0x198,model_matches:!differences.length,differences,
   self:player.add(0xe0).readPointer().toString(),interpolant_vtables:[0x160,0x178].map(o=>player.add(o).readPointer().toString()),
   interpolant_slots:[0,8,16,24].map(o=>({offset:o,target:vt.add(o).readPointer().toString()})),
   note:'Native static initializer hardcodes singleton addresses. This observer does not invoke it or create another Player.'});
}});
