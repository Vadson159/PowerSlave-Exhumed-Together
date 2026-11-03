// Validate this bounded room's Actor/node membership as P1 leaves or returns.
function passiveSectorMembership57(m,actorNode,p1){
 const expected=p1.sector===259?[actorNode,p1.node]:[actorNode];
 return m.valid===true&&m.found===1&&m.count===expected.length&&m.entries.length===expected.length&&
        expected.every(id=>m.entries.filter(e=>e.node===id).length===1);
}
if(typeof module!=='undefined')module.exports=passiveSectorMembership57;
