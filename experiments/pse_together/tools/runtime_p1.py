"""P1/S0.4 runtime binding for the unified DLLs; no game is started on import.

Run through the accepted REA MCP Python spawn lane, not a shell launcher:
  runtime_p1.py launch|bind|gate|watch|action <host|client> <P1_HOST_01|P1_CLIENT_01> [...]
  runtime_p1.py input focus <host|client> | key <W|S|A|D> <seconds<=0.4> [handoff] | mouse <dx>
  runtime_p1.py observer client P1_CLIENT_01 snapshot|render <label>

The launcher, isolation guard, DOC51 DLL-loader body, retained-handle watcher,
and ordinary OS-input fixture are reused from accepted laboratories unchanged.
Only output paths, run identities and the unified build artifacts are rebound.
REA loads generated JS; this tool itself never calls Frida or injects a DLL.
"""
from pathlib import Path
import datetime
import hashlib
import importlib
import importlib.util
import json
import os
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
TOOLS = Path(__file__).resolve().parent
LAB = ROOT / 'experiments/pse_together/runtime'
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P1'
EXPECTED = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
RUNS = {'host': 'P1_HOST_03', 'client': 'P1_CLIENT_03'}
GATE_PATH = OUT / 'PRE_NATIVE_GATE_RUN03.json'
DLLS = {'host': ROOT / 'b60/dll/pse_dll_host/pse_m11_bootstrap.dll',
        'client': ROOT / 'b60/dll/pse_dll_client_proxy/pse_native_client58b.dll'}
SITES_PATH = ROOT / 'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json'


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')


def inside(path):
    path = Path(path).resolve()
    if not path.is_relative_to(ROOT.resolve()):
        raise ValueError('Outside mod workspace: ' + str(path))
    return path


def dump(path, obj):
    path = inside(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf8') as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)


def accepted_runner(role):
    if role not in RUNS:
        raise ValueError('Unknown role')
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT / 'experiments/m0'))
    sys.path.insert(0, str(ROOT / 'experiments/m1/native_bootstrap'))
    spec = importlib.util.spec_from_file_location('accepted_m11_runner_p1', ROOT / 'experiments/m1/native_bootstrap/runner.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    m.LAB = LAB
    m.OUT = m.IMPLEMENTATION = OUT
    m.prepare.OUT = OUT
    m.prepare.PROFILE = LAB / 'profile'
    m.prepare.COPY = ROOT / ('experiments/m0/game' if role == 'host' else 'experiments/m1/native_second_instance/game')
    return m


def verify_artifacts():
    gate = load(GATE_PATH)
    if gate.get('status') != 'PASS' or not gate.get('frozen'):
        raise ValueError('P1 pre-native gate has not passed or contains no frozen artifacts')
    frozen = {}
    for row in gate['frozen']:
        path = inside(ROOT / row['path'])
        if sha(path) != row['sha256']:
            raise ValueError('Artifact changed after pre-native gate: ' + row['path'])
        frozen[path] = row['sha256']
    for role, dll in DLLS.items():
        if dll.resolve() not in frozen:
            raise ValueError('Unified DLL missing from gate: ' + role)
    for exe in [ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe',
                ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe']:
        if sha(exe) != EXPECTED:
            raise ValueError('EXE identity mismatch: ' + str(exe))
    return gate


def source_records():
    paths = [ROOT / 'experiments/m1/native_bootstrap/runner.py',
             ROOT / 'experiments/m1/native_second_instance/runner.py',
             ROOT / 'experiments/m0/launch.py', ROOT / 'experiments/m0/prepare.py',
             ROOT / 'experiments/m0/isolation_guard.js', ROOT / 'experiments/m0/observer.js',
             ROOT / 'experiments/m0/generate_observer.py', ROOT / 'experiments/m0/bind_probe.py',
             ROOT / 'experiments/m1/background_host/watch.py', ROOT / 'experiments/m1/background_host/ui53.py',
             ROOT / 'experiments/m1/background_host/foreground_fixture.py',
             ROOT / 'experiments/m1/native_network_visual_proxy_58b/input58b.py',
             Path(__file__), TOOLS / 'observer_p1.js']
    return [dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p)) for p in paths]


def bind(m, role, name):
    verify_artifacts()
    run, launch = m.owned_run(name)
    sys.argv = ['generate_observer', name]
    importlib.import_module('generate_observer').main()
    sys.argv = ['bind_probe', name, 'isolation']
    importlib.import_module('bind_probe').main()
    # observer_bound.js is only an input for the accepted isolation generator.
    # It contains gameplay hooks and MUST NOT be loaded in the P1 process.
    artifact = run / 'native_artifact'
    artifact.mkdir()
    dll = DLLS[role]
    for ext in ['.dll', '.pdb']:
        source = dll.with_suffix(ext)
        shutil.copy2(source, artifact / source.name)
    frozen = artifact / dll.name
    cfg = dict(pid=launch['pid'], target=launch['target'], primary_tid=launch['primary_thread_id'],
               dll=str(frozen), dll_name=dll.name, dll_sha256=sha(frozen),
               native_log=str(run / 'native.jsonl'), role=role)
    dump(run / 'NATIVE_ARTIFACT.json', dict(config=cfg, pre_native_gate_sha256=sha(GATE_PATH),
         sources=source_records(), host_dll_is_unified=role == 'host', client_adapter='internal C++',
         no_frida_kex_control_callbacks=True, new_injector=False))
    # Byte-identical accepted DOC51 loader body, binding only its explicit config.
    loader = (ROOT / 'experiments/m1/native_second_instance/runner.py').read_text(encoding='utf8')
    start = loader.index('if(Process.id!==cfg.pid')
    end = loader.index("'''", start)
    (run / 'load_native.js').write_text('const cfg = ' + json.dumps(cfg) + ';\n' + loader[start:end], encoding='utf8')
    for filename, source in [('os_probe.js', ROOT / 'experiments/m1/native_second_instance/os_probe.js')]:
        (run / filename).write_bytes(source.read_bytes())
    (run / 'snapshot_os_probe.js').write_text('const cfg = ' + json.dumps(cfg) + ';\n' +
        (ROOT / 'experiments/m1/native_snapshot/snapshot_os_probe.js').read_text(encoding='utf8'), encoding='utf8')
    config = Path(launch['profile']) / 'early_native_logs/kexengine.cfg'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n', encoding='ascii')
    dump(run / 'TEST_CONFIG.json', dict(path=str(config), sha256=sha(config), text=config.read_text(), test_profile_only=True))
    print(json.dumps(cfg))


def gate(m, role, name):
    try:
        m.gate(name)
    except SystemExit as e:
        if e.code:
            raise
    run, launch = m.owned_run(name)
    if role != 'client':
        return
    permit = run / 'permit_native.js'
    permit.write_text(permit.read_text(encoding='utf8').replace('pse_m11_bootstrap.dll', DLLS['client'].name), encoding='utf8')
    cfg = load(run / 'NATIVE_ARTIFACT.json')['config']
    head = 'const c=' + json.dumps(cfg) + ';\n' + (
        "if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('target');"
        "const d=Process.getModuleByName(c.dll_name);if(d.path.toLowerCase()!==c.dll.toLowerCase())throw Error('DLL path');\n")
    status = ("const st=new NativeFunction(d.getExportByName('PSE58B_Status'),'uint32',[],'win64')();"
              "send({kind:'p1_status',status:st,seam:st&255,quiescent:(st>>8)&1,proxy_state_plus1:(st>>16)&255,utc:new Date().toISOString()});\n")
    (run / 'status.js').write_text(head + status, encoding='utf8')
    for filename, export, event in [('permit_proxy_spawn.js', 'PSE_P1_PermitProxySpawn', 'p1_spawn_permit'),
                                    ('request_shutdown.js', 'PSE58B_RequestProxyShutdown', 'p1_proxy_shutdown'),
                                    ('request_detach.js', 'PSE58B_RequestDetach', 'p1_detach')]:
        body = ("const r=new NativeFunction(d.getExportByName('" + export + "'),'uint32',[],'win64')();"
                "send({kind:'" + event + "',result:r,kex_calls:0,atomic_control_only:true,utc:new Date().toISOString()});\n")
        (run / filename).write_text(head + body + status, encoding='utf8')


def observer(m, role, name, mode, label):
    if role != 'client' or mode not in ['snapshot', 'render'] or not label.replace('_', '').isalnum():
        raise ValueError('Observer requires client, snapshot|render, and an alphanumeric label')
    run, launch = m.owned_run(name)
    isolation = load(run / 'ISOLATION_GATE.json')
    if isolation.get('status') != 'PASS' or isolation.get('pid') != launch['pid'] or isolation.get('binary_sha256') != EXPECTED:
        raise ValueError('Current run isolation gate missing')
    artifact = load(run / 'NATIVE_ARTIFACT.json')['config']
    sites = load(SITES_PATH)
    cfg = dict(pid=launch['pid'], target=launch['target'], simulation_tid=launch['primary_thread_id'],
               binary_sha256=EXPECTED, dll_name=artifact['dll_name'], dll_path=artifact['dll'],
               native_log=artifact['native_log'], log=str(run / ('observer_' + label + '.jsonl')),
               mode=mode, label=label, observation_export='PSE_P1_ReadObservation', observation_abi=0x50310001,
               sites={k: sites[k] for k in ['scene', 'sprite', 'vertex', 'interpolant_lerp']})
    if mode == 'render':
        # Read only completed native journal lines. This is diagnostic evidence,
        # not a new pointer API or a permit to mutate/dereference the owned Actor.
        spawned=[];terminal_after_spawn=False;prefix_bytes=0;prefix_hash=hashlib.sha256()
        terminal={'native_remove_request','remove_request','actor_identity_retired','deferred_retired','native_proxy_fault','world_exit_observed'}
        with Path(artifact['native_log']).open('rb') as journal:
            for line in journal:
                if not line.endswith(b'\n'): break
                prefix_bytes+=len(line);prefix_hash.update(line)
                if not line.strip(): continue
                row=json.loads(line)
                if row.get('event')=='proxy_spawned' and row.get('pid')==launch['pid'] and row.get('tid')==launch['primary_thread_id']:
                    spawned.append(row)
                elif spawned and row.get('event') in terminal:
                    terminal_after_spawn=True
        if len(spawned) != 1:
            raise ValueError('Render binding requires exactly one actual native proxy_spawned for this PID/Sim TID')
        spawn = spawned[0]
        if terminal_after_spawn:
            raise ValueError('Owned proxy has already removed, retired, faulted or exited')
        actor, node = int(spawn['actor'], 16), int(spawn['node'], 16)
        if not (0 < actor < (1 << 64) and 0 < node < (1 << 64) and actor != node):
            raise ValueError('Invalid diagnostic owned Actor/ObjectNode pair')
        cfg.update(owned_actor=hex(actor), owned_node=hex(node), owned_spawn_record=spawn,
                   owned_pointer_source='native.jsonl/proxy_spawned', native_journal_prefix_bytes=prefix_bytes,
                   native_journal_prefix_sha256=prefix_hash.hexdigest())
    body = (TOOLS / 'observer_p1.js').read_text(encoding='utf8')
    path = run / ('observer_' + label + '.js')
    with path.open('x', encoding='utf8') as f:
        f.write('const cfg=' + json.dumps(cfg) + ';\n' + body)
    dump(path.with_suffix('.manifest.json'), dict(config=cfg, source_sha256=sha(TOOLS / 'observer_p1.js'),
         bound_sha256=sha(path), proxy_pointer_exports=False, proxy_actor_reads=False, local_p1_reads=True,
         own_object_node_reads=mode == 'render', other_actor_reads=mode == 'render',
         pointer_source='diagnostic native proxy_spawned journal' if mode == 'render' else None, game_writes=0,
         control_callbacks=False, dll_owned_hooks_not_attached=['StateTick', 'Update', 'StateExit', 'PuppetTick', 'ActorTick', 'PoolReturn', 'Sweep', 'Drain']))
    print(path)


def watch(role, name):
    source = (ROOT / 'experiments/m1/background_host/watch.py').read_text(encoding='utf8')
    source = source.replace('from audit import ROOT, LAB, OUT', '')
    source = source.replace("LAB.parent/'terminal_shutdown/observer.py'", "ROOT/'experiments/m1/terminal_shutdown/observer.py'")
    source = source.replace("run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')", 'run=OUT/' + repr(name))
    source = source.replace('time.monotonic()-begin<1200', 'time.monotonic()-begin<3600')
    scope = {'__name__': 'accepted53_watch_p1', 'ROOT': ROOT, 'LAB': LAB, 'OUT': OUT}
    exec(compile(source, 'accepted53_watch_p1', 'exec'), scope)
    scope['watch'](role)


def inputs(args):
    physical = bool(args and args[0] == 'physical')
    if physical:
        if len(args)!=3 or args[1] not in ['W','A','S','D'] or not 0<float(args[2])<=.4:
            raise ValueError('Physical input: owned host WASD, 0 < duration <= .4')
        args = ['key', *args[1:]]
    source = (ROOT / 'experiments/m1/native_network_visual_proxy_58b/input58b.py').read_text(encoding='utf8')
    source = source.replace('from workflow import ROOT,OUT,dump,stamp', 'from runtime_p1 import ROOT,OUT,dump,stamp')
    source = source.replace('from workflow import ROOT, OUT, dump, stamp', 'from runtime_p1 import ROOT, OUT, dump, stamp')
    source = source.replace("RUNS={'host':'N58B_HOST_02','client':'N58B_CLIENT_02'}", 'RUNS=' + repr(RUNS))
    source = source.replace("fg=ui.foreground();records.append", "fg=ui.foreground();\n    if fg['pid']!=d['pid']:\n        from focus_p1 import titlebar_focus\n        titlebar_focus(d,h,records);fg=ui.foreground()\n    records.append")
    if physical:
        source = source.replace('def now():', "def physical_key(vk,scan,down):\n    ui.u.keybd_event.argtypes=[ui.W.BYTE,ui.W.BYTE,ui.W.DWORD,ui.C.c_size_t]\n    ui.u.keybd_event(vk,scan,0 if down else 2,0)\n    return True\n\ndef now():")
        source = source.replace('ui.u.PostMessageW(h,0x100,vk,1|(scan<<16))', 'physical_key(vk,scan,True)')
        source = source.replace('ui.u.PostMessageW(h,0x100,vk,1|(scan<<16)|(1<<30))', 'True')
        source = source.replace('ui.u.PostMessageW(h,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31))', 'physical_key(vk,scan,False)')
        source = source.replace("result=dict(status='PASS',action=sys.argv[1:]", "result=dict(status='PASS',physical_os_keyboard=True,action=sys.argv[1:]")
    scope = {'__name__': 'accepted58b_input_p1'}
    exec(compile(source, 'accepted58b_input_p1', 'exec'), scope)
    sys.argv = ['input_p1'] + args
    scope['main']()


def main():
    if sys.argv[1] == 'input':
        inputs(sys.argv[2:])
        return
    command, role, name = sys.argv[1:4]
    if RUNS.get(role) != name:
        raise ValueError('Only fresh P1 run identities are bound')
    m = accepted_runner(role)
    if command == 'launch':
        verify_artifacts()
        if (OUT / name).exists():
            raise ValueError('Run already exists; never reuse a live/stale run')
        m.launch(name)
    elif command == 'bind':
        bind(m, role, name)
    elif command == 'gate':
        gate(m, role, name)
    elif command == 'watch':
        watch(role, name)
    elif command == 'observer':
        observer(m, role, name, *sys.argv[4:6])
    elif command == 'action':
        if 'activate' in sys.argv[4:]:
            raise ValueError('Synthetic activation is not an accepted foreground witness')
        m.action(name, sys.argv[4], sys.argv[5:])
    else:
        raise ValueError(command)


if __name__ == '__main__':
    main()
