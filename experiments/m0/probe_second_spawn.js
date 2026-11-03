/* M0-B destructive-in-disposable-process probe. One factory call on the observed simulation thread.
   No Player/actor pointer assignments, no restoration writes, no achievements/network calls.
   This probe must be loaded only after M0-A first-player runtime identity was captured. */
'use strict';
const game=Process.mainModule;
if(Process.id!==cfg.pid || game.path.toLowerCase()!==cfg.target.toLowerCase())
  throw new Error('M0-B target PID/path mismatch');
function capture(p){const node=p.add(0x10).readPointer();return {actor:p.toString(),node:node.toString(),payload:node.add(0x28).readPointer().toString(),
  refs:node.add(0x40).readS32(),owner:p.add(0x1b8).readPointer().toString(),origin:[0x18,0x1c,0x20].map(o=>p.add(o).readFloat()),yaw:p.add(0x38).readFloat(),sector:p.add(0x3c).readS32(),health:p.add(0x17a).readS16()};}
function event(kind,data){send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data));}
const factory=game.base.add(0x5a10a0),slot=factory.readPointer().add(0x18).readPointer(),entry=game.base.add(0x1d4780);
if(!slot.equals(entry))throw new Error('Factory vtable slot does not match verified SpawnActor candidate');
const spawn=new NativeFunction(entry,'pointer',['pointer','int','pointer','float','int'],'win64');
let fired=false,frames=0,listener;
event('B_armed',{pid:Process.id,factory:factory.toString(),slot:slot.toString(),abi:'Windows x64; Factory*,int,Vec3*,float,int -> Actor*',expected_simulation_tid:cfg.simulation_tid});
listener=Interceptor.attach(game.base.add(0x1b6ec0),{onEnter(args){this.gi=args[0];},onLeave(){
  if(fired||Process.getCurrentThreadId()!==cfg.simulation_tid)return;
  const gi=this.gi,player=gi.add(0x40);
  if(gi.add(0x24).readS32()!==3 || gi.add(0x28).readS32()!==0)return;
  const node=player.add(0xd8).readPointer();if(node.isNull())return;
  const original=node.add(0x28).readPointer();if(original.isNull()||!original.add(0x1b8).readPointer().equals(player))return;
  if(++frames<30)return;
  fired=true;
  const before=capture(original),position=Memory.alloc(12);
  // The very same validated sector/origin is deliberate: this tests construction/binding,
  // not remote collision or placement. No guessed map coordinate is supplied.
  position.writeFloat(before.origin[0]);position.add(4).writeFloat(before.origin[1]);position.add(8).writeFloat(before.origin[2]);
  event('B_before',{original:before,player:player.toString(),player_node:node.toString()});
  try {
    const created=spawn(factory,0,position,before.yaw,before.sector);
    const currentNode=player.add(0xd8).readPointer();
    event('B_after',{returned:created.toString(),original:capture(original),created:created.isNull()?null:capture(created),
      player:player.toString(),player_node:currentNode.toString(),player_actor:currentNode.isNull()?null:currentNode.add(0x28).readPointer().toString(),
      independent_controller:false,note:'No restoration attempted. End this disposable process after evidence capture; not M0 success.'});
  } catch(e){event('B_native_call_error',{error:String(e),stack:e.stack});}
  listener.detach();
}});
