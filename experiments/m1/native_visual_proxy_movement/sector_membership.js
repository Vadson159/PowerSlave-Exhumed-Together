function passiveSectorMembership57(m,actorNode,p1){
 const expected=p1.sector===259?[actorNode,p1.node]:[actorNode];
 return Number.isInteger(p1.sector)&&p1.sector>=0&&actorNode!==p1.node&&m.valid===true&&m.found===1&&m.count===expected.length&&m.entries.length===expected.length&&
 expected.every(id=>m.entries.filter(e=>e.node===id).length===1)&&m.entries.every(e=>e.payload && e.payload!=='0x0')&&
 m.entries.every(e=>e.node!==p1.node||e.payload===p1.actor);
}
if(typeof module!=='undefined')module.exports=passiveSectorMembership57;