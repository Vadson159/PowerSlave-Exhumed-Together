// DOC57 exact-build initialized field content reads. No game writes or refcounts.
const P1Fields57 = [
  ['ankh_count',0x84,2,'int16','s16'],
  ['ankh_mask',0x86,2,'uint16 bitmask','u16'],
  ['artifacts',0x90,4,'EnumPlayerArtifacts uint32 mask','u32'],
  ['abilities',0x94,4,'EnumPlayerAbilities uint32 mask','u32'],
  ['keys',0x98,2,'int16 API / raw16 mask','u16'],
  ['quest_items',0x9a,2,'int16 API / raw16 mask','u16'],
  ['team_dolls',0xa4,4,'uint32 mask','u32'],
  ['current_weapon',0x88,4,'int32 enum','s32'],
  ['pending_weapon',0x8c,4,'int32 enum','s32'],
  ['owned_weapons',0xcc,8,'uint8[8]','u8x8'],
  ['ammo',0xbc,16,'raw16[8]; C++ signedness unknown','u16x8'],
  ['health',0x17a,2,'int16','s16','Puppet']
];
const P1LeafGetters57={artifacts:0x1ae080,abilities:0x1ae090,keys:0x1ae0c0,quest_items:0x1ae0a0,team_dolls:0x1ae0b0};
const P1GetterCache57={};
function snapshotP1State57(label,p) {
  const result={schema:'P1StateSnapshot-v1',label,status:'UNKNOWN',
    binary_sha256:cfg.binary_sha256,pid:Process.id,tid:Process.getCurrentThreadId(),
    runtime_base:base.toString(),native_tick:null,identity:null,fields:{},errors:[]};
  // Always return explicit UNKNOWN rather than fabricate a complete snapshot.
  try {
    if(!thread()||!inTick)throw Error('snapshot outside attested simulation scope');
    result.native_tick=base.add(0xaec390).readU32();
    if(!validPrimary(p))throw Error('invalid P1 graph');
    const fresh=primary();
    if(!validPrimary(fresh)||!equal(graph(fresh),graph(p)))throw Error('stale P1 identity');
    result.identity=graph(fresh);
    const player=ptr(fresh.player),puppet=ptr(fresh.actor);
    for(const [name,offset,size,type,reader,owner] of P1Fields57) {
      const field={field:name,offset:(owner==='Puppet'?'Puppet':'Player')+'+0x'+offset.toString(16),
        getter:null,type,size,raw:null,decoded:null,status:'UNKNOWN'};
      result.fields[name]=field;
      try {
        let at=(owner==='Puppet'?puppet:player).add(offset);
        if(P1LeafGetters57[name]) {
          const rva=P1LeafGetters57[name],entry=base.add(rva);
          field.getter={rva:'0x'+rva.toString(16),abi:'win64 pointer(pointer)',return:'borrowed inline reference'};
          const expected='488d81'+offset.toString(16).padStart(2,'0')+'000000c3';
          if(bytes(entry,8)!==expected)throw Error('leaf getter bytes changed');
          if(!P1GetterCache57[name])P1GetterCache57[name]=new NativeFunction(entry,'pointer',['pointer'],{abi:'win64',exceptions:'propagate'});
          const returned=P1GetterCache57[name](player);
          if(!returned.equals(at))throw Error('leaf getter returned unexpected inline reference');
          at=returned;
        }
        field.raw=bytes(at,size);
        if(reader==='s16')field.decoded=at.readS16();
        else if(reader==='u16')field.decoded=at.readU16();
        else if(reader==='s32')field.decoded=at.readS32();
        else if(reader==='u32')field.decoded=at.readU32();
        else if(reader==='u8x8')field.decoded=Array.from({length:8},(_,i)=>at.add(i).readU8());
        else if(reader==='u16x8')field.decoded=Array.from({length:8},(_,i)=>at.add(2*i).readU16());
        else throw Error('unsupported mandatory reader');
        field.status='OK';
      } catch(error) { field.error=String(error);result.errors.push(name+': '+String(error)); }
    }
    result.status=result.errors.length?'UNKNOWN':'OK';
  } catch(error) { result.errors.push(String(error)); }
  return result;
}
function compareP1State57(before,after) {
  const report={before:before&&before.label,after:after&&after.label,status:'UNKNOWN',
    differences:[],unknown:[],compared_fields:[]};
  if(!before||!after){report.unknown.push('missing snapshot');return report;}
  if(before.status!=='OK'||after.status!=='OK')report.unknown.push('incomplete snapshot');
  for(const key of ['binary_sha256','pid','runtime_base','identity']) {
    if(before[key]===null||before[key]===undefined||after[key]===null||after[key]===undefined)
      report.unknown.push('missing '+key);
    else if(JSON.stringify(before[key])!==JSON.stringify(after[key]))
      report.differences.push({field:key,before:before[key],after:after[key]});
  }
  for(const [name,offset,size] of P1Fields57) {
    const a=before.fields&&before.fields[name],b=after.fields&&after.fields[name];
    if(!a||!b||a.status!=='OK'||b.status!=='OK'||typeof a.raw!=='string'||typeof b.raw!=='string'||
       a.raw.length!==size*2||b.raw.length!==size*2||!/^[0-9a-f]+$/i.test(a.raw)||!/^[0-9a-f]+$/i.test(b.raw)) {
      report.unknown.push(name);continue;
    }
    report.compared_fields.push(name);
    if(a.raw.toLowerCase()!==b.raw.toLowerCase())report.differences.push({field:name,offset:a.offset,
      before_raw:a.raw,after_raw:b.raw,before:a.decoded,after:b.decoded});
  }
  report.status=report.unknown.length?'UNKNOWN':report.differences.length?'DIFFERENT':'EQUAL';
  return report;
}
if(typeof module!=='undefined'&&module.exports)module.exports={P1Fields57,snapshotP1State57,compareP1State57};
