"""Derive isolated P16 session/client sources once; never changes accepted P7."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def derived(source,dest,changes):
    src=ROOT/source;out=ROOT/dest
    if out.exists():raise RuntimeError('Refusing overwrite: '+str(out))
    text=src.read_text(encoding='utf8')
    for before,after in changes:
        if text.count(before)!=1:raise ValueError((source,before,text.count(before)))
        text=text.replace(before,after)
    out.write_text('// Isolated P16 derivative; accepted source remains unchanged.\n'+text,encoding='utf8')
derived('src/session/network_input.cpp','src/session/network_input_p16.cpp',[
    ('namespace m12a {','namespace m12a {\nvoid p16Receive(const pse::BridgeRespawnRequest&);'),
    ('if (active && req->slotId == 1 && req->connectionGeneration == gen) recover(pse::RESYNC_EXTERNAL);',
     'if(active&&ready&&req->slotId==1&&req->connectionGeneration==gen&&req->mapEpoch==epoch&&snapshotEligible())p16Receive(*req);')])
derived('src/native/client_p7.cpp','src/native/client_p16.cpp',[
    ('#include "self_native_p7.hpp"','#include "self_native_p7.hpp"\n#include "lifecycle_p16.hpp"\n#include "respawn_p16.hpp"'),
    ('#include "client_self_p7.inc"','#include "client_self_p16.inc"')])
derived('src/native/client_self_p7.inc','src/native/client_self_p16.inc',[])
derived('src/native/self_native_p7.cpp','src/native/self_native_p16.cpp',[])
print('P16 session/client/self derivatives created')
