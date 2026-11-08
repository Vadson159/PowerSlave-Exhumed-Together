"""P7 evidence reuse and narrow static audit; NEVER launches KEX or writes game state.

The machine receipt is separate from package/native readiness. A PASS here does
not approve a key-reference write, install a damage hook, or activate a protocol.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P7'
SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
OLD = ROOT / 'docs/evidence/ghidra_2022'
NEW = OUT / 'static'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def load(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def pe_offset(data, rva):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    for n in range(count):
        off = pe + 24 + optional + 40 * n
        virtual_size, address, size, raw = struct.unpack_from('<IIII', data, off + 8)
        if address <= rva < address + min(virtual_size, size):
            return raw + rva - address
    raise ValueError('RVA lacks unambiguous file-backed bytes')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    checks, inputs, sites = [], [], []

    def check(name, ok, **detail):
        checks.append(dict(name=name, passed=bool(ok), **detail))

    def dossier(folder, address):
        p = folder / ('G_FUNC_' + address + '.json')
        d = load(p)
        inputs.append(dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p), reused=folder == OLD,
                           analysis_profile=dict(provider=d['provider'], version=d['ghidra_version'],
                                                 language=d['language'], compiler=d['compiler_spec'])))
        check(address + ' evidence exact-build/profile', d['binary_sha256'] == SHA and
              d['language'] == 'x86:LE:64:default' and d['compiler_spec'] == 'windows' and
              d['decompile_completed'] and d['function']['entry'] == '0x' + address)
        return d

    host = ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
    client = ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe'
    original = Path('S:/Games/SteamLibrary/steamapps/common/PowerSlave Exhumed/anubis_Shipping_Steam_x64.exe')
    for role, p in [('host copy', host), ('client copy', client), ('original read-only', original)]:
        check(role + ' exact EXE', sha(p) == SHA, sha256=sha(p))
    data = client.read_bytes()
    static = {}
    for folder, addresses in [(OLD, ['1401b5ce0', '1401b5b80', '1401b5df0', '1401dfe30',
                                     '1401e04e0', '1401a1370', '1401dbd00', '14019f2d0',
                                     '1401d8a00', '1401d8ac0']),
                              (NEW, ['1401e0770', '1401e0850', '1401dc080', '1401debc0',
                                     '1401dc420', '1401deef0', '1401e09d0', '1401a16d0', '1401aee80'])]:
        for address in addresses:
            d = dossier(folder, address)
            static[address] = d['pseudocode_non_source']
            rva = int(d['function']['rva'], 16)
            offset = pe_offset(data, rva)
            sites.append(dict(analyst_name='candidate_' + address, recovered_name=d['function']['name'],
                              binary_sha256=SHA, preferred_va=d['function']['entry'], rva=hex(rva),
                              file_offset=hex(offset), prefix16=data[offset:offset + 16].hex(),
                              evidence=inputs[-1]['path'], runtime_observed=False,
                              abi='Ghidra/assembly candidate; NOT blanket authorization to call'))
    restore = static['1401b5ce0']
    check('scalar restore has no calls', 'FUN_' not in restore.split('{', 1)[1])
    check('scalar restore excludes keys field', '0xd8)' not in restore and '0x98)' not in restore)
    check('scalar restore puts health into pending slot', 'param_1 + 0x1d0' in restore)
    controller_asm = dossier(OLD, '1401dfe30')['assembly']
    check('controller applies pending health', any('MOVZX EAX,word ptr [RBX + 0x190]' in s for s in controller_asm) and
          any('MOV word ptr [RDI + 0x17a],AX' in s for s in controller_asm))
    check('level initializer resets keys', 'param_1 + 0x26' in static['1401e04e0'])
    check('level initializer invokes weapon actions', 'FUN_14020f760' in static['1401e04e0'])
    check('key pickup is OR and consume', 'uVar2 | (ushort)uVar5' in static['1401d8a00'] and 'FUN_1401d8360' in static['1401d8a00'])
    check('checkpoint key restore is coupled to world', 'FUN_1401e04e0' in static['1401b5df0'] and '0x29c' in static['1401b5df0'])
    check('base actor bypasses common damage with direct decrement', '+ -1' in static['14019f2d0'] and '0x17a' in static['14019f2d0'])
    check('ammo native add uses signed short and cap', 'short param_3' in static['1401e0770'] and '0xbc' in static['1401e0770'])
    check('health helper halves amount by difficulty', 'param_2 >> 1' in static['1401e0850'])
    check('fall and drowning use common damage', all('FUN_1401a1370' in static[n] for n in ['1401debc0', '1401e09d0']))
    check('forcefield damage uses common damage', 'FUN_1401a1370' in static['1401dc080'])
    check('give command only grants all four keys', 'Got all keys!' in static['1401aee80'] and 'uVar4 | 8' in static['1401aee80'])

    # Native registered mutable reference. Byte proof is a leaf ABI observation,
    # not a runtime proof of the receiver/lifetime or permission to dereference.
    rva = 0x1ae0c0
    offset = pe_offset(data, rva)
    prefix = data[offset:offset + 8].hex()
    check('GetKeys exact leaf LEA RAX,[RCX+98];RET', prefix == '488d8198000000c3', prefix=prefix)
    sites.append(dict(analyst_name='GetKeys_native_reference', recovered_name='LAB_1401ae0c0', binary_sha256=SHA,
                      preferred_va='0x1401ae0c0', rva=hex(rva), file_offset=hex(offset), prefix8=prefix,
                      evidence='docs/evidence/ghidra_2022/G_INSPECT_1401ae0c0.json', runtime_observed=False,
                      abi='Win64 RCX=Player; RAX=int16 reference candidate. Runtime receiver/lifetime gate OPEN.'))

    baseline = load(ROOT / 'experiments/pse_together/src/wire111/BASELINE.json')
    check('all frozen v1.10 source files unchanged', all(sha(ROOT / row['source']) == row['baseline_sha256'] for row in baseline), files=len(baseline))
    frozen111 = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72/PRE_NATIVE_RUN04_snapshot/sources/experiments/pse_together/src/wire111'
    wire111 = ROOT / 'experiments/pse_together/src/wire111'
    pinned111 = sorted(p for p in frozen111.rglob('*') if p.is_file())
    check('identity-only v1.11 unchanged from accepted P4 candidate', bool(pinned111) and
          all((wire111 / p.relative_to(frozen111)).is_file() and sha(p) == sha(wire111 / p.relative_to(frozen111)) for p in pinned111), files=len(pinned111))
    module = ROOT / 'experiments/pse_together/src/session/self_state_p7.hpp'
    text = module.read_text(encoding='utf-8')
    check('draft native capabilities closed by default', 'bool health{},allAmmo{},ownedAndSelectedWeapons{},keys{},inventory{},death{};' in text)
    check('draft has no KEX offsets or native memcpy', all(s not in text for s in ['0xaf33', '0x17a', 'memcpy(', 'reinterpret_cast']))
    for p in [module, ROOT / 'experiments/pse_together/tests/session/self_state_p7.cpp', Path(__file__)]:
        inputs.append(dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p), reused=False))

    spec = importlib.util.spec_from_file_location('p7_integrity', Path(__file__).with_name('integrity.py'))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    game = audit.games()
    accepted_game = load(ROOT / 'docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72/INTEGRITY_CONFIRM_FINAL.json')['game']
    check('original 77/protected saves match accepted integrity', game == accepted_game, game=game)
    result = dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',
                  meaning='Static assertions and integrity ONLY; NOT P7/native/runtime acceptance',
                  package_status='REVIEW / PARTIAL', mode='В', checks=checks, inputs=inputs,
                  game_started=False, native_writes=False, steam_calls=False,
                  old_runs_repeated=False, accepted_research_repeated=False)
    (OUT / 'MACHINE_AUDIT.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    (OUT / 'NATIVE_SITES.json').write_text(json.dumps(sites, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(result['status'], sum(c['passed'] for c in checks), '/', len(checks), 'static/integrity checks; P7 REVIEW/PARTIAL')
    for c in checks:
        if not c['passed']:
            print('FAIL:', c['name'])
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
