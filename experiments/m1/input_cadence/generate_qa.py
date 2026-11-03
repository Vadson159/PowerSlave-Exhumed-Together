"""QA-only ENet accounting copies. Never changes the vendored library."""
from pathlib import Path
import difflib,json,hashlib
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
ENET=ROOT/'tools/enet-prototype/third_party/enet';OUT=LAB/'qa_enet';OUT.mkdir(exist_ok=True)
prefix='#include "enet/enet.h"\nextern void qa_packet(const char*,const ENetPacket*,const ENetPeer*,int);\nextern void qa_socket(const ENetBuffer*,size_t,int);\nextern int qa_flush_enabled(void);\n'
records=[]
for name in ['peer.c','protocol.c','win32.c']:
    original=(ENET/name).read_text();s=original
    if name=='peer.c':
        assert s.count('enet_peer_send (')==1;s=s.replace('enet_peer_send (','qa_real_peer_send (')
        s+='\nint enet_peer_send(ENetPeer* p,enet_uint8 c,ENetPacket* b) { int r=qa_real_peer_send(p,c,b); qa_packet("queued",b,p,r); if(r==0 && b->dataLength==45 && b->data[6]==0x10 && qa_flush_enabled()) { qa_packet("flush",b,p,r); enet_host_flush(p->host); } return r; }\n'
    if name=='protocol.c':
        token='if (peer -> packetThrottleCounter > peer -> packetThrottle)\n             {'
        assert s.count(token)==1;s=s.replace(token,token+'\n                qa_packet("throttle_drop",outgoingCommand -> packet,peer,0);')
        assert s.count('enet_host_service (')==1;s=s.replace('enet_host_service (','qa_real_host_service (')
        s+='\nint enet_host_service(ENetHost* h,ENetEvent* e,enet_uint32 t) { int r=qa_real_host_service(h,e,t); if(r>0 && e && e->type==ENET_EVENT_TYPE_RECEIVE) qa_packet("host_event",e->packet,e->peer,r); return r; }\n'
    if name=='win32.c':
        assert s.count('enet_socket_send (')==1;s=s.replace('enet_socket_send (','qa_real_socket_send (')
        s+='\nint enet_socket_send(ENetSocket s,const ENetAddress* a,const ENetBuffer* b,size_t n) { int r=qa_real_socket_send(s,a,b,n);qa_socket(b,n,r);return r;}\n'
    s=prefix+s;(OUT/name).write_text(s,encoding='utf8')
    records.append(dict(original=str((ENET/name).relative_to(ROOT)),original_sha256=hashlib.sha256((ENET/name).read_bytes()).hexdigest(),qa_sha256=hashlib.sha256((OUT/name).read_bytes()).hexdigest()))
    (OUT/(name+'.patch')).write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile=name,tofile='QA/'+name)),encoding='utf8')
(OUT/'MANIFEST.json').write_text(json.dumps(records,indent=2),encoding='utf8')
print('QA-only ENet copies generated')
