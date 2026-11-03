"""Bind observer to a launched workspace process and exact original function prefixes."""
import json,sys
from pathlib import Path
from prepare import ROOT,OUT,COPY
sys.path.insert(0,str(ROOT/'docs/research'))
from inventory import PE

def main():
    run=sys.argv[1];launch=json.loads((OUT/run/'launch.json').read_text())
    pe=PE(COPY/'anubis_Shipping_Steam_x64.exe')
    sites={'state_tick':0x1b6ec0,'player_update':0x1dfe30,'get_player':0x1b5520,
      'factory':0x1d4780,'map_factory':0x1d41f0,'puppet_spawn':0x1dbc60,'puppet_tick':0x1db8a0,
      'player_reset':0x1dfa70,'remove':0x1a02b0,'pool_return':0x1d5000,'userpath':0xed600,
      'constructor_witness':0x1ae110}
    cfg={'pid':launch['pid'],'target':launch['target'],'profile':launch['profile'],'binary_sha256':launch['binary_sha256'],
        'sites':{n:{'rva':r,'prefix':pe.b[pe.off(r):pe.off(r)+16].hex()} for n,r in sites.items()}}
    source='const cfg = '+json.dumps(cfg)+';\n'+(Path(__file__).parent/'observer.js').read_text(encoding='utf8')
    (OUT/run/'observer_bound.js').write_text(source,encoding='utf8')
    print(json.dumps({'source':str(OUT/run/'observer_bound.js'),'pid':cfg['pid']}))

if __name__=='__main__':main()
