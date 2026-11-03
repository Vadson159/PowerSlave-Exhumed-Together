/* Runs the actual observer flow against own-memory fixtures. No native game API. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const messages=[];let count=1;
const actor={add:()=>({readFloat:()=>1152})},local={add:()=>({readU32:()=>0})},p2={add:()=>({readU32:()=>0})};
const ctx={base:{add:()=>0},Interceptor:{attach:()=>{}},prefix:()=>{},local,p2,stage:'alive',
 simulationThread:()=>true,gameplayState:()=>true,live:()=>true,playerActor:()=>null,
 p1Identity:{actor},p2Identity:{actor},controller:{counts:()=>({p2_controller:count})},guards:()=>true,
 graph:p=>({live_identity:true,health:200,origin:p===local?[0,0,1152]:[0,80,1152+(count%20)]}),
 event:(kind,data)=>messages.push({kind,...JSON.parse(JSON.stringify(data))})};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/directed_physics_flow.js','utf8')+'\nglobalThis.flow=createDirectedPhysicsFlow();',ctx);
for(;count<=841;count++)ctx.flow.step();
const cases=messages.filter(m=>m.kind==='C_directed_physics_case');
assert.equal(cases.length,7);
for(const c of cases){assert.equal(c.ticks,120);assert.equal(c.min_distance,80);assert.equal(c.p1.min_floor_gap,0);assert.equal(c.p1.max_floor_gap,0);assert.equal(c.p2.min_floor_gap,0);assert.equal(c.p2.max_floor_gap,19);}
assert.equal(messages.filter(m=>m.kind==='C_directed_physics_observed').length,1);
console.log(JSON.stringify({status:'PASS',cases:cases.length,scope:'Actual flow with own fixtures only; no game runtime acceptance'}));
