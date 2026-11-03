// Offline regression using genuine captured list/identity rows; never attaches KEX.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const membership=require('./sector_membership.js');
const run=process.argv[2],rows=fs.readFileSync(path.join(run,'native.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
const spawn=rows.find(r=>r.event==='spawn_identity_verified');
const clean=fs.readFileSync(path.join(run,'cleanup.jsonl'),'utf8').trim().split('\n').map(JSON.parse).find(r=>r.event==='cleanup_identity_revalidated');
const node=spawn.node_id,p1=spawn.p1;
assert(membership(spawn.sector_list,node,p1),'P1 and Actor in259');
assert(membership(clean.sector_list,node,clean.primary),'Actor remains259, P1 moved123');
assert.equal(clean.sector_list.count===spawn.sector_list.count,false,'reproduce old erroneous constant-count condition');
assert(!membership({...clean.sector_list,valid:false},node,clean.primary),'invalid native list');
assert(!membership({...clean.sector_list,found:0},node,clean.primary),'missing Actor occurrence');
assert(!membership({...clean.sector_list,count:2},node,clean.primary),'count/entries disagreement');
assert(!membership(clean.sector_list,node,p1),'missing P1 while P1 declares sector259');
assert(!membership({...spawn.sector_list,entries:[{node},{node}]},node,p1),'duplicate Actor and missing P1');
assert(!membership({...clean.sector_list,entries:[{node:'0xdead'}]},node,clean.primary),'unrelated node');
console.log(JSON.stringify({status:'PASS',checks:9,scope:'offline native-evidence sector-membership guard regression; no new KEX run',current_full_probe_native_replay:false}));
