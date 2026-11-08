// Exercise the actual passive census function; no Frida, process, or game is loaded.
const fs=require('fs'),vm=require('vm'),path=require('path');
const source=fs.readFileSync(path.join(__dirname,'observer_p1.js'),'utf8');
const body=source.slice(source.indexOf('function spriteCensus('),source.indexOf('\nconst fields='));
let checks=0,failed=0;
function fixture(){
 const memory=new Map();let actorReads=0;
 const owned=0x700000,node=0x201000,other=0x202000,head=0x200000,tail=0x203000,actor=0x400000,anim=0x500000;
 class P {
  constructor(address){this.address=Number(address);}
  add(n){return new P(this.address+n);}equals(p){return this.address===p.address;}isNull(){return this.address===0;}
  toString(){return '0x'+this.address.toString(16);}
  read(){if(this.address>=owned&&this.address<owned+0x1e0){actorReads++;throw Error('OWNED_ACTOR_READ');}
   if(!memory.has(this.address))throw Error('UNREADABLE_'+this.toString());return memory.get(this.address);}
  readPointer(){return new P(this.read());}readU8(){return this.read();}readU32(){return this.read();}
  readS16(){return this.read();}readS32(){return this.read();}readFloat(){return this.read();}
  readU64(){const value=this.read();return {toNumber:()=>value};}
 }
 const base=new P(0x140000000),sectorStart=0x100000,sector=sectorStart+259*0x88;
 memory.set(base.address+0xa45970,270);memory.set(base.address+0xa45968,sectorStart);
 memory.set(sector+0x80,259);memory.set(sector+0x78,head);
 for(const [at,prev,next,payload] of [[head,0,node,0],[node,head,other,owned],[other,node,tail,actor],[tail,other,0,0]]){
  memory.set(at,prev);memory.set(at+8,next);memory.set(at+0x28,payload);memory.set(at+0x24,1);
 }
 memory.set(actor+0x108,anim);memory.set(actor+0x178,0);memory.set(anim+0x24,1);memory.set(anim+0x18,0x600000);
 memory.set(actor+0x6c,10);memory.set(actor+0x70,20);memory.set(actor+0x74,30);memory.set(actor+0x128,0);
 const context={base,ownedActor:new P(owned),ownedNode:new P(node),cfg:{simulation_tid:1},Process:{getCurrentThreadId:()=>1},
  sim:()=>true,stable:()=>true,equal:(a,b)=>JSON.stringify(a)===JSON.stringify(b),
  vec:p=>[p.readFloat(),p.add(4).readFloat(),p.add(8).readFloat()],
  entry:{sector:259,frame:'0x601000',origin_bits:[1065353216,1073741824,1077936128]},
  o:{sector:259,phase:2,identity_valid:true,sim_tid:1,observed_tid:1}};
 return {context,memory,node,other,head,tail,actor,anim,owned,actorReads:()=>actorReads};
}
const cases=[
 ['unique',()=>{},'OURS'],
 ['duplicate',f=>{f.memory.set(f.anim+0x18,0x601000);[1,2,3].forEach((x,i)=>f.memory.set(f.actor+0x6c+4*i,x));},'AMBIGUOUS'],
 ['foreign',f=>{f.context.sim=()=>false;},'UNKNOWN'],
 ['pending',f=>{f.context.o.phase=3;},'UNKNOWN'],
 ['invalid identity',f=>{f.context.o.identity_valid=false;},'UNKNOWN'],
 ['other sector',f=>{f.context.o.sector=123;},'UNKNOWN'],
 ['sector count',f=>{f.memory.set(f.context.base.address+0xa45970,269);},'UNKNOWN'],
 ['owned payload',f=>{f.memory.set(f.node+0x28,f.actor);},'UNKNOWN'],
 ['owned node retired',f=>{f.memory.set(f.node+0x24,0);},'UNKNOWN'],
 ['reciprocity',f=>{f.memory.set(f.other,0);},'UNKNOWN'],
 ['cycle',f=>{f.memory.set(f.other+8,f.node);},'UNKNOWN'],
 ['frame bounds',f=>{f.memory.set(f.actor+0x178,1);},'UNKNOWN'],
 ['unreadable other render',f=>{f.memory.delete(f.actor+0x70);},'UNKNOWN'],
 ['nonfinite z',f=>{f.memory.set(f.actor+0x128,NaN);},'UNKNOWN'],
 ['owned Actor alias',f=>{f.memory.set(f.other+0x28,f.owned);},'UNKNOWN'],
 ['missing own node',f=>{f.memory.set(f.head+8,f.other);f.memory.set(f.other,f.head);},'UNKNOWN']
];
for(const [name,mutate,expected] of cases){
 const f=fixture();mutate(f);const actual=vm.runInNewContext(body+'\nspriteCensus(entry,o);',f.context);
 checks++;if(actual.status!==expected){failed++;process.stderr.write(name+': '+actual.status+' expected '+expected+'\n');}
 checks++;if(f.actorReads()!==0){failed++;process.stderr.write(name+': owned Actor was read\n');}
}
process.stdout.write(`observer census: ${checks} checks, ${failed} failed\n`);process.exitCode=failed?1:0;
