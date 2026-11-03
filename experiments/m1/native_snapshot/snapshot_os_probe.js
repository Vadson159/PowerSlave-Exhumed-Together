// Passive socket-buffer observations only. Never read an engine pointer or mutate a packet.
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('snapshot OS probe target mismatch');
let ordinal=0;
function emit55(kind,data){send(Object.assign({kind,order:++ordinal,pid:Process.id,tid:Process.getCurrentThreadId(),utc:new Date().toISOString(),kex_reads:0},data));}
function decode55(buffers){
  const total=buffers.reduce((n,b)=>n+b.length,0);if(total>65536)return [];
  const bytes=new Uint8Array(total);let offset=0;for(const b of buffers){bytes.set(b,offset);offset+=b.length;}
  const view=new DataView(bytes.buffer),out=[];
  // Recognize complete v1.10 message spans inside ENet UDP buffers. Only snapshot
  // fields are retained: handshake/reconnect secrets are never dumped.
  for(let i=0;i+24<=total;++i){
    if(view.getUint32(i,true)!==0x50534558 || view.getUint16(i+4,true)!==0x0110 || bytes[i+6]!==0x20)continue;
    const count=bytes[i+23],length=24+65*count;if(!count || count>8 || i+length>total)continue;
    for(let j=0;j<count;++j){const p=i+24+65*j;
      out.push({opcode:32,channel_contract:1,header_sequence:view.getUint32(i+7,true),snapshot_seq:view.getUint32(i+11,true),server_tick:view.getUint32(i+15,true),map_epoch:view.getUint32(i+19,true),count,wire_bytes:length,udp_bytes:total,message_offset:i,slot:bytes[p],entity_id:view.getUint32(p+1,true),entity_generation:view.getUint32(p+5,true),bits:[9,13,17,33,37].map(k=>view.getUint32(p+k,true))});
    }
    i+=length-1;
  }
  return out;
}
function vectors55(pointer,count,limit){
  if(count>64)throw Error('WSABUF count bound');const out=[];let remaining=limit;
  for(let n=0;n<count && remaining>0;++n){const b=pointer.add(16*n),capacity=b.readU32();if(capacity>65536)throw Error('WSABUF byte bound');const length=Math.min(capacity,remaining);out.push(new Uint8Array(b.add(8).readPointer().readByteArray(length)));remaining-=length;}
  return out;
}
Interceptor.attach(Module.getGlobalExportByName('WSASendTo'),{
  onEnter(a){this.socket=a[0].toString();this.rows=[];this.sent=a[3];this.overlapped=!a[7].isNull();try{this.rows=decode55(vectors55(a[1],a[2].toUInt32(),65536));}catch(e){emit55('n55_probe_error',{api:'WSASendTo',error:String(e)});}},
  onLeave(ret){for(const row of this.rows)emit55('n55_udp_snapshot_send',Object.assign({socket:this.socket,result:ret.toInt32(),overlapped:this.overlapped,bytes_sent:!this.sent.isNull()?this.sent.readU32():null},row));}
});
Interceptor.attach(Module.getGlobalExportByName('WSARecvFrom'),{
  onEnter(a){this.socket=a[0].toString();this.buffers=a[1];this.count=a[2].toUInt32();this.received=a[3];this.overlapped=!a[7].isNull();},
  onLeave(ret){if(ret.toInt32()!==0 || this.overlapped || this.received.isNull())return;try{const n=this.received.readU32();for(const row of decode55(vectors55(this.buffers,this.count,n)))emit55('n55_udp_snapshot_receive',Object.assign({socket:this.socket,result:0,overlapped:false,bytes_received:n},row));}catch(e){emit55('n55_probe_error',{api:'WSARecvFrom',error:String(e)});}}
});
emit55('n55_snapshot_os_probe_ready',{target:Process.mainModule.path,scope:'recognized v1.10 snapshot spans in actual synchronous socket buffers; no KEX hooks'});
