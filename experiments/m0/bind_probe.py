"""Bind a diagnostic source to one verified, workspace-owned run. Does not launch/instrument it."""
import argparse,json,hashlib
from pathlib import Path
from prepare import ROOT,OUT,COPY,EXPECTED,digest

def payloads(run):
    for p in sorted(run.glob('*.json')):
        d=json.loads(p.read_text(encoding='utf-8-sig'))
        if not isinstance(d,dict):continue
        n=d.get('structuredContent',{}).get('normalized_result',{})
        for m in n.get('messages',[]):
            if isinstance(m.get('payload'),dict):yield m['payload']

def main():
    a=argparse.ArgumentParser();a.add_argument('run');a.add_argument('mode',choices=['witness','B','lifecycle','controller','weapon','enemy','death','hostdeath','projectile','respawn','inflight','quit','physics','state','isolation','readiness']);a.add_argument('--check-only',action='store_true');a.add_argument('--reuse-constructor-evidence',type=Path);a.add_argument('--source-snapshot',type=Path);a.add_argument('--projectile-weapon',choices=['pistol','cobra','amun'],default='pistol');a=a.parse_args()
    run=(OUT/a.run).resolve()
    if not run.is_relative_to(OUT.resolve()):raise ValueError('Outside evidence root')
    launch=json.loads((run/'launch.json').read_text())
    if Path(launch['target']).resolve()!=(COPY/'anubis_Shipping_Steam_x64.exe').resolve() or digest(Path(launch['target']))!=EXPECTED:raise ValueError('Target/build mismatch')
    cfg={k:launch[k] for k in ['pid','target','profile']};cfg['simulation_tid']=launch['primary_thread_id'];cfg['binary_sha256']=EXPECTED
    cfg.update(workspace=str(ROOT),observe_sleep=False,observe_thread_stacks=False,projectile_weapon='amun' if a.mode=='inflight' else a.projectile_weapon,projectile_target='wall' if a.mode in ['projectile','respawn'] else None,
      reference_policy='DOC34_NATIVE_RELEASE_ONLY',native_classes={'projectile_vtable_rva':0x52dad8,'dynlight_vtable_rva':0x5291b0,'dynlight_ctor_field_rva':0xa43350})
    bound=(run/'observer_bound.js').read_text(encoding='utf8');observer_cfg=json.loads(bound.splitlines()[0][len('const cfg = '):-1]);cfg['sites']=observer_cfg['sites']
    lab=Path(__file__).parent;parts=[]
    probe_lab=lab
    if a.source_snapshot:
        probe_lab=a.source_snapshot.resolve()
        if not probe_lab.is_relative_to(OUT.resolve()) or not probe_lab.is_dir():raise ValueError('Source snapshot must be inside owned M0 evidence')
        cfg['probe_source_snapshot']=str(probe_lab.relative_to(ROOT))
    if a.mode=='witness':parts=[(lab/'constructor_witness.js').read_text(encoding='utf8')]
    elif a.mode=='state':parts=[(lab/'read_state.js').read_text(encoding='utf8')]
    elif a.mode=='readiness':parts=[(lab/'readiness.js').read_text(encoding='utf8')]
    elif a.mode=='isolation':
        source=(lab/'observer.js').read_text(encoding='utf8')
        parts=["const main=Process.mainModule;function emit(kind,data){send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data));}function attempt(fn){try{return fn();}catch(e){return {read_error:String(e)};}}\nif(Process.id!==cfg.pid||main.path.toLowerCase()!==cfg.target.toLowerCase())throw new Error('isolation target mismatch');",
         (lab/'isolation_guard.js').read_text(encoding='utf8'),"const isolationGuard=installM0WriteGuard(cfg,emit);",
         source[source.index('const known='):source.index('function hook(')],source[source.index('const writePaths='):source.index("emit('observer_loaded'")]]
    else:
        if not a.check_only:
            gate_path=run/'ISOLATION_GATE_FOLLOWUP.json'
            if not gate_path.exists():gate_path=run/'ISOLATION_GATE.json'
            gate=json.loads(gate_path.read_text(encoding='utf8'))
            if gate_path.name=='ISOLATION_GATE_FOLLOWUP.json':
                original=run/'ISOLATION_GATE.json'
                if gate.get('follow_up_to',{}).get('sha256')!=digest(original):raise ValueError('Startup follow-up does not match retained initial gate')
            if gate.get('status')!='PASS' or gate.get('pid')!=cfg['pid'] or gate.get('binary_sha256')!=EXPECTED:
                raise ValueError('Current-run isolation gate has not passed; do not load gameplay')
        observations=list(payloads(run))
        identity=[p for p in observations if p.get('kind')=='observer_identity' and p.get('pid')==cfg['pid'] and p.get('binary_sha256_host_verified')==EXPECTED]
        tick=[p for p in observations if p.get('kind')=='tick_sample' and p.get('state')==3 and p.get('tid')==cfg['simulation_tid']]
        if not identity or not tick:raise ValueError('Save actual observer identity and gameplay tick Evidence before binding probe')
        if a.mode=='B':parts=[(lab/'probe_second_spawn.js').read_text(encoding='utf8')]
        else:
            witness=[p for p in observations if p.get('kind')=='C_native_constructor_witness' and p.get('model_matches') and not p.get('differences')]
            if not witness and a.reuse_constructor_evidence:
                saved=a.reuse_constructor_evidence.resolve()
                if not saved.is_relative_to((ROOT/'docs/evidence').resolve()):raise ValueError('Constructor evidence outside workspace evidence')
                prior=json.loads(saved.read_text(encoding='utf-8-sig'))['structuredContent']['normalized_result']['messages']
                payload=[m['payload'] for m in prior if isinstance(m.get('payload'),dict)]
                if not any(p.get('kind')=='observer_identity' and p.get('binary_sha256_host_verified')==EXPECTED for p in payload):raise ValueError('Prior constructor evidence build mismatch')
                witness=[p for p in payload if p.get('kind')=='C_native_constructor_witness' and p.get('size')==0x198 and p.get('model_matches') and not p.get('differences')]
                cfg['constructor_evidence_reused']={'path':str(saved.relative_to(ROOT)),'sha256':hashlib.sha256(saved.read_bytes()).hexdigest(),'binary_sha256':EXPECTED,'note':'Saved C01 evidence reused per user; no constructor investigation repeated'}
            if not witness:raise ValueError('Missing successful native constructor witness')
            # Bind every newly used native site only after exact-file SHA validation above.
            import sys
            sys.path.insert(0,str(ROOT/'docs/research'))
            from inventory import PE
            pe=PE(COPY/'anubis_Shipping_Steam_x64.exe')
            # projectile_factory is retained only for byte checks in frozen historical sources;
            # the initial PE ctor binding proves this RVA creates DynLight, not Projectile.
            sites={'set_target':0x1d35d0,'set_source_owner':0x1a17c0,'projectile_factory':0x1d50b0,'dynamic_light_factory':0x1d50b0,'impact':0x1e6510,
              'remove':0x1a02b0,'reset':0x1dfa70,'weapon_ready':0x20f9e0,
              'controller_update':0x1dfe30,'commands':0x1e34a0,'turning':0x1e2ad0,
              'damage':0x1a1370,'puppet_damage':0x1dbd00,'dead_puppet_movement':0x1de8b0,'weapon_pickup_touch':0x1d8470,
              'collision':0x1a4990,'puppet_touch':0x1dbfc0,'state_exit':0x1b6670,'give_weapon':0x1e06d0,'puppet_deleting_destructor':0x1d9370,'projectile_deleting_destructor':0x1e45e0,'native_shutdown':0xfcc20,'engine_shutdown':0xa4c90,'arena_cleanup':0xc58b0,'final_exit':0xfc580,'target_ai_tick':0x194ce0,'projectile_on_remove':0x1e4a80,'projectile_tick':0x1e4b10,'actor_spawn':0x1a02f0,'actor_destructor':0x19f210,'common_on_remove':0x195000}
            cfg['native_prefixes']={name:{'rva':rva,'prefix':pe.b[pe.off(rva):pe.off(rva)+16].hex()} for name,rva in sites.items()}
            cfg.update(constructor_witness_passed=True,controller_test=a.mode in ['controller','weapon','enemy','death','hostdeath','projectile','respawn','inflight','quit','physics'],weapon_ready=a.mode in ['weapon','enemy','death','hostdeath','projectile','respawn','inflight','quit','physics'],enemy_test=a.mode=='enemy',death_test=a.mode=='death',host_death_test=a.mode=='hostdeath',projectile_test=a.mode in ['projectile','respawn','inflight'],manual_respawn_test=a.mode=='respawn',inflight_respawn_test=a.mode=='inflight',direct_quit_test=a.mode=='quit',context_world_exit_test=True,physics_test=a.mode=='physics')
            if cfg['inflight_respawn_test']:
                windows=[json.loads(p.read_text()) for p in sorted(run.glob('ui_*.json'))]
                windows=[w for w in windows if w.get('pid')==cfg['pid'] and w.get('hwnd') and w.get('width',0)>100]
                if not windows:raise ValueError('Save an actual owned-window UI record before binding queued-E fixture')
                cfg['respawn_fixture_hwnd']=windows[-1]['hwnd']
            probe_source=(probe_lab/'full_player_probe.js').read_text(encoding='utf8')
            if not a.check_only and a.mode in ['enemy','projectile','respawn','inflight','quit','physics'] and 'DOC34_NATIVE_RELEASE_ONLY' not in probe_source:
                raise ValueError('Historical reference-mutating probe is not a DOC-34 passive runtime source')
            parts.append((lab/'context_world_exit.js').read_text(encoding='utf8'))
            if cfg['host_death_test'] or cfg['manual_respawn_test']:parts.append((lab/'host_death_gate.js').read_text(encoding='utf8'))
            if cfg['physics_test']:parts.append((lab/'directed_physics_flow.js').read_text(encoding='utf8'))
            if cfg['manual_respawn_test'] or cfg['inflight_respawn_test']:parts.append((lab/'manual_respawn_input.js').read_text(encoding='utf8'))
            if cfg['manual_respawn_test']:parts.append((lab/'manual_respawn_flow.js').read_text(encoding='utf8'))
            if cfg['inflight_respawn_test']:parts.append((lab/'projectile_respawn_flow.js').read_text(encoding='utf8'))
            if cfg['direct_quit_test']:parts.append((lab/'direct_quit_observer.js').read_text(encoding='utf8'))
            if cfg['controller_test']:parts.append((probe_lab/'controller_adapter.js').read_text(encoding='utf8'))
            parts.append(probe_source)
    output=run/('generated_'+a.mode+'.js')
    if a.check_only:
        print(json.dumps({'status':'PASS','mode':a.mode,'pid':cfg['pid'],'source_bytes':sum(len(s.encode('utf8')) for s in parts),'target_executed':False,'output_written':False,'scope':'Saved prerequisite/binding validation only; does not attach to historical PID'}));return
    if output.exists():raise ValueError('Output exists; retain historical bound source, use a new run')
    output.write_text('const cfg = '+json.dumps(cfg)+';\n'+'\n'.join(parts),encoding='utf8')
    print(json.dumps({'source':str(output),'pid':cfg['pid'],'mode':a.mode,'note':'Bound only; not loaded or executed'}))

if __name__=='__main__':main()
