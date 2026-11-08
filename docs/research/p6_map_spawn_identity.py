"""P6 / S3.1 (DOC-62): static check of map-object identity and LoadMap spawn order.

Read-only. Starts no game process, no Frida/REA, writes only the result JSON.
Inputs: accepted Ghidra dossiers (docs/evidence/ghidra_2022) plus the narrow
P6 exports (docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/static, produced by
docs/research/invoke_ghidra.py in read-only mode), and the EXE copies (hash only).

Usage: python -X utf8 -I docs/research/p6_map_spawn_identity.py [--out PATH]
Exit code 0 only if every check passes.
"""
import argparse, datetime, glob, hashlib, json, os, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
EXES = {
    'host_test_copy': ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe',
    'client_test_copy': ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe',
    'original_read_only': pathlib.Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe'),
}
G22 = ROOT / 'docs/evidence/ghidra_2022'
P6S = ROOT / 'docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/static'
DEFAULT_OUT = ROOT / 'docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/MAP_SPAWN_IDENTITY_STATIC.json'


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def rel(path):
    try:
        return str(pathlib.Path(path).resolve().relative_to(ROOT)).replace('\\', '/')
    except ValueError:
        return str(path)


class Checker:
    def __init__(self):
        self.checks = []
        self.files = {}

    def dossier(self, path):
        path = pathlib.Path(path)
        d = load(path)
        self.files[rel(path)] = {'sha256': sha256_file(path), 'binary_sha256': d.get('binary_sha256')}
        return d

    def add(self, cid, claim, label, ok, evidence, detail):
        self.checks.append({'id': cid, 'claim': claim, 'label': label,
                            'result': 'PASS' if ok else 'FAIL', 'evidence': evidence, 'detail': detail})

    def regex(self, cid, claim, path, field, patterns, label='VERIFIED static'):
        d = self.dossier(path)
        text = d.get(field) or ''
        if isinstance(text, list):
            text = '\n'.join(map(str, text))
        found, missing = [], []
        for p in patterns:
            m = re.search(p, text)
            (found if m else missing).append({'pattern': p, 'match': m.group(0)[:200] if m else None})
        ok = not missing and d.get('binary_sha256') == TARGET_SHA
        self.add(cid, claim, label, ok, rel(path), {'field': field, 'found': found, 'missing': missing,
                                                     'dossier_binary_sha256': d.get('binary_sha256')})

    def absent(self, cid, claim, path, field, pattern, label='VERIFIED static (absence in exported body only)'):
        d = self.dossier(path)
        text = d.get(field) or ''
        if isinstance(text, list):
            text = '\n'.join(map(str, text))
        hits = [m.group(0)[:160] for m in re.finditer(pattern, text)]
        ok = not hits and d.get('binary_sha256') == TARGET_SHA
        self.add(cid, claim, label, ok, rel(path), {'field': field, 'pattern': pattern, 'hits': hits})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    a = ap.parse_args()
    c = Checker()

    # 0. EXE identity (hash only, read-only).
    exe = {}
    for k, p in EXES.items():
        exe[k] = {'path': str(p), 'exists': p.exists(), 'sha256': sha256_file(p) if p.exists() else None}
    c.add('EXE', 'Analysed and runtime EXE copies equal the target SHA-256', 'VERIFIED (file hash)',
          all(v['sha256'] == TARGET_SHA for v in exe.values()), 'file system', exe)

    # 1. ReadActors 0x1401cc490: sequential records, stride 0x14, spawn iff sector >= 0.
    c.regex('READACTORS_ORDER', 'ReadActors walks actor records 0..count-1 in file order (stride 0x14) and calls the map factory '
            'via factory vtable +0x20 only when record sector (short +2) >= 0; player type with sector>0x7fff is normalised to 0',
            G22 / 'G_FUNC_1401cc490.json', 'pseudocode_non_source',
            [r'lVar7 = lVar7 \+ 0x14;', r'while \(uVar6 < \*\(ulonglong \*\)\(param_1 \+ 0xa8\)\)',
             r'if \(-1 < \*\(short \*\)\(lVar7 \+ 2 \+ \*\(longlong \*\)\(param_1 \+ 0xa0\)\)\)',
             r'\(\*\*\(code \*\*\)\(PTR_vftable_1405a10a0 \+ 0x20\)\)',
             r'if \(\*\(short \*\)\(lVar7 \+ \*\(longlong \*\)\(param_1 \+ 0xa0\)\) == 0\)'])
    d = c.dossier(G22 / 'G_FUNC_1401cc490.json')
    callees = [x.get('entry') for x in d.get('direct_callees', [])]
    c.add('READACTORS_CALLEE', 'Ghidra resolves the factory vcall in ReadActors to 0x1401d41f0', 'VERIFIED static',
          '0x1401d41f0' in callees, rel(G22 / 'G_FUNC_1401cc490.json'), {'direct_callees': callees})

    # 2. Map factory 0x1401d41f0: filters, kexSlaveAI skill filter, record index at Actor+0x180.
    f = G22 / 'G_FUNC_1401d41f0.json'
    c.regex('FACTORY_TYPE_FILTER', 'Map factory returns null for record type < 0 and resolves the class from defs by type '
            '(special cases: 0 kexSlavePuppet, 0x19/0x7e/0x80 kexSlaveFireballSpawner, 0x7f kexSlaveLaserBeam)', f,
            'pseudocode_non_source', [r'if \(\*param_2 < 0\) \{\s*return \(longlong \*\)0x0;', r'FUN_1401bb140\(DAT_140af3350',
                                      r'"kexSlavePuppet"', r'"kexSlaveFireballSpawner"', r'"kexSlaveLaserBeam"'])
    c.regex('FACTORY_SKILL_FILTER', 'Only for the exact class id DAT_140a42c90 the record flags (short +0xc) exclude the spawn '
            'by skill from GameInstance vcall +0x158: skill1 -> bit1, skill2 -> bit2, otherwise bit0', f, 'pseudocode_non_source',
            [r'if \(\*\(int \*\)\(ppuVar14 \+ 1\) == DAT_140a42c90\)', r'uVar6 = param_2\[6\];',
             r'\(\*\*\(code \*\*\)\(DAT_140af33a0 \+ 0x158\)\)\(&DAT_140af33a0\)', r'bVar20 = \(uVar6 & 2\) == 0;',
             r'bVar20 = \(uVar6 & 4\) == 0;', r'bVar20 = \(uVar6 & 1\) == 0;'])
    c.regex('SKILL_QUERY_SAME', 'The same GameInstance vcall +0x158 is the difficulty query of Player.GetHealth 0x1401dfa20 '
            '(no actor: 100 for values 1/2, else 200; DOC-03) and is also called by AI Spawn 0x1401953d0 (DOC-04)',
            G22 / 'G_FUNC_1401dfa20.json', 'pseudocode_non_source',
            [r'iVar2 = \(\*\*\(code \*\*\)\(DAT_140af33a0 \+ 0x158\)\)\(&DAT_140af33a0\);',
             r'if \(\(iVar2 == 1\) \|\| \(uVar1 = 200, iVar2 == 2\)\)', r'uVar1 = 100;'])
    c.regex('SKILL_QUERY_AI_SPAWN', 'AI Spawn 0x1401953d0 calls the same vcall +0x158', G22 / 'G_FUNC_1401953d0.json',
            'pseudocode_non_source', [r'\(\*\*\(code \*\*\)\(DAT_140af33a0 \+ 0x158\)\)\(&DAT_140af33a0\)'])
    c.regex('FACTORY_RECORD_INDEX','Map factory stores (record - level.actorRecords.data)/0x14 into Actor+0x180 '
            '(DAT_140a459e0 = level 0x140a45940 + 0xa0), then origin/yaw, LinkSector(record sector) and virtual Spawn +0x38', f,
            'pseudocode_non_source', [r'lVar17 = \(longlong\)param_2 - DAT_140a459e0;',
                                      r'\*\(int \*\)\(plVar15 \+ 0x30\) = \(int\)\(lVar17 / 0x14\);',
                                      r'FUN_1401d3d70\(plVar15,\(int\)param_2\[1\]\);', r'\(\*\*\(code \*\*\)\(\*plVar15 \+ 0x38\)\)\(plVar15\);'])
    c.regex('FACTORY_RECORD_INDEX_ASM', 'Machine instruction of the +0x180 store', f, 'assembly',
            [r'0x1401d46da: MOV dword ptr \[RDI \+ 0x180\],EDX'])
    c.regex('FACTORY_POOL_REUSE', 'Factory reuses a pooled object of the class when the free list is non-empty (address reuse)', f,
            'pseudocode_non_source', [r'lVar13 = \*\*\(longlong \*\*\)\(lVar18 \+ 8 \+ DAT_140af45f0\);', r'if \(lVar13 == 0\)'])

    # 3. Class id DAT_140a42c90 = kexSlaveAI.
    c.regex('CLASSID_WRITER', 'DAT_140a42c90 is written once by a static class-registration initializer that hashes the name at 0x140527431',
            P6S / 'G_INSPECT_14001ff30.json', 'instructions', [r'LEA RDX,\[0x140527431\]', r'MOV dword ptr \[0x140a42c90\],EAX', r'MOV AL,0x6b'])
    m = c.dossier(P6S / 'G_MEMORY_140527430.json')
    raw = b''.join(int(r['u64_hex'], 16).to_bytes(8, 'little') for r in m['rows'][:2])
    name = raw.split(b'\0')[0].decode('ascii', 'replace')
    c.add('CLASSID_NAME', 'Registration name at 0x140527430 is "kexSlaveAI": the skill filter applies to map enemies of exact class kexSlaveAI',
          'VERIFIED static', name == 'kexSlaveAI' and m.get('binary_sha256') == TARGET_SHA, rel(P6S / 'G_MEMORY_140527430.json'), {'decoded': name})
    r = c.dossier(P6S / 'G_REFS_140a42c90.json')
    writes = [x for x in r['incoming'] if x.get('type') == 'WRITE']
    c.add('CLASSID_SINGLE_WRITE', 'DAT_140a42c90 has exactly one WRITE xref (0x14001ff6e)', 'VERIFIED static',
          [x['from'] for x in writes] == ['0x14001ff6e'], rel(P6S / 'G_REFS_140a42c90.json'),
          {'incoming': [(x['from'], x['type'], (x.get('containing_function') or {}).get('entry')) for x in r['incoming']]})

    # 4. Default and other writers/readers of Actor+0x180.
    c.regex('CTOR_DEFAULT', 'kexSlaveActor constructor 0x14019ef20 sets Actor+0x180 = -1 (non-map default)', G22 / 'G_FUNC_14019ef20.json',
            'assembly', [r'MOV dword ptr \[RBX \+ 0x180\],0xffffffff'])
    c.regex('CTOR_VTABLE', 'The same constructor installs kexSlaveActor::vftable', G22 / 'G_FUNC_14019ef20.json', 'pseudocode_non_source',
            [r'\*param_1 = kexSlaveActor::vftable;', r'\*\(undefined4 \*\)\(param_1 \+ 0x30\) = 0xffffffff;'])
    c.absent('BYTYPE_NO_INDEX', 'By-type factory 0x1401d4780 does not write Actor+0x180', G22 / 'G_FUNC_1401d4780.json', 'assembly',
             r'MOV dword ptr \[[A-Z0-9]+ \+ 0x180\]')
    c.absent('POOLRETURN_NO_INDEX', 'Pool return 0x1401d5000 does not write Actor+0x180 (pooled objects may keep a stale index)',
             G22 / 'G_FUNC_1401d5000.json', 'assembly', r'MOV dword ptr \[[A-Z0-9]+ \+ 0x180\]')
    c.regex('CONSUMED_BITSET', '0x1401d8360 marks bit Actor+0x180 in level bitset 0x140a45a80 (= level+0x140) before removal',
            G22 / 'G_FUNC_1401d8360.json', 'pseudocode_non_source',
            [r'uVar1 = \*\(uint \*\)\(param_1 \+ 0x30\);', r'_DAT_140a45a80', r'\*pbVar5 = \*pbVar5 \| \(byte\)\(1 << \(uVar1 & 7\)\);'])
    c.regex('RESTORE_BY_INDEX', 'Loaded-world restore 0x1401b5df0 restores three level bitsets and, for every kexSlaveActor in sector lists, '
            'calls vcall +0x88 when the saved bit Actor+0x180 is set', G22 / 'G_FUNC_1401b5df0.json', 'pseudocode_non_source',
            [r'FUN_1401bc760\(&DAT_140a45a78,param_1 \+ 0x2a0\);', r'FUN_1401bc760\(&DAT_140a45a98,param_1 \+ 0x2c0\);',
             r'FUN_1401bc760\(&DAT_140a45ab8,param_1 \+ 0x2e0\);', r'uVar1 = \*\(uint \*\)\(plVar8 \+ 0x30\);',
             r'\(\*\*\(code \*\*\)\(\*plVar8 \+ 0x88\)\)\(plVar8\);'])

    # 5. LoadMap order: three zeroed bitsets, ReadActors, then BuildRHIBuffers only.
    c.regex('LOADMAP_BITSETS', 'LoadMap sizes/zeroes bitsets level+0x140 (by actor records +0xa8), +0x160 (by +0x48 array of 0x108-byte items) '
            'and +0x180 (by +0x90 array) and then calls ReadActors and 0x1401cc8d0', G22 / 'G_FUNC_1401c7b70.json', 'pseudocode_non_source',
            [r'lVar13 = \*\(longlong \*\)\(param_1 \+ 0xa8\);[\s\S]{0,400}\*\(ulonglong \*\)\(param_1 \+ 0x138\) = uVar15;[\s\S]{0,60}param_1 \+ 0x140',
             r'lVar13 = \*\(longlong \*\)\(param_1 \+ 0x48\);[\s\S]{0,400}\*\(ulonglong \*\)\(param_1 \+ 0x158\) = uVar15;',
             r'\*\(longlong \*\)\(param_1 \+ 0x90\) != 0\)[\s\S]{0,200}\*\(ulonglong \*\)\(param_1 \+ 0x178\) = uVar15;',
             r'FUN_1401cc490\(param_1,&local_e8\);\s*FUN_1401cc8d0\(param_1\);'])
    c.regex('POSTSTEP_RHI', 'The only step after ReadActors (0x1401cc8d0) is kexSlaveLevelLocal::BuildRHIBuffers', P6S / 'G_FUNC_1401cc8d0.json',
            'pseudocode_non_source', [r'kexSlaveLevelLocal::BuildRHIBuffers: Creating RHI buffers'])
    d = c.dossier(P6S / 'G_FUNC_1401cc8d0.json')
    callees = [x.get('entry') for x in d.get('direct_callees', [])]
    bad = [x for x in callees if x in ('0x1401d41f0', '0x1401d4780', '0x1401d3d70')]
    c.add('POSTSTEP_NO_SPAWN', 'BuildRHIBuffers has no direct call to either factory or LinkSector (indirect calls not excluded)',
          'VERIFIED static (direct callees only)', not bad, rel(P6S / 'G_FUNC_1401cc8d0.json'), {'direct_callees': callees, 'spawn_like': bad})

    # 6. Host-originated displacement example used by ADR-1: sector teleporter.
    c.regex('TELEPORT_ACTOR', 'Sector teleporter 0x1401be2f0 moves the touching actor itself: origin store, LinkSector(dest), yaw, velocity zero',
            G22 / 'G_FUNC_1401be2f0.json', 'pseudocode_non_source',
            [r'\*\(float \*\)\(param_1 \+ 3\) = fVar15;', r'FUN_1401d3d70\(param_1,\(int\)psVar10\[1\]\);',
             r'\*\(float \*\)\(param_1 \+ 7\) = fVar13;', r'\*\(undefined8 \*\)\(\(longlong\)param_1 \+ 0xe4\) = 0;',
             r'\*\(undefined4 \*\)\(param_1 \+ 0x1c\) = 0;'])
    c.regex('TELEPORT_DISPATCH', 'Sector-event dispatcher 0x1401bea70 reaches the teleporter 0x1401be2f0', G22 / 'G_FUNC_1401bea70.json',
            'pseudocode_non_source', [r'FUN_1401be2f0\('])

    # 7. Coverage scan: which exported bodies touch dword Actor+0x180 (informational, dossiers cover only exported functions).
    scan = []
    for p in sorted(glob.glob(str(G22 / 'G_FUNC_*.json'))) + sorted(glob.glob(str(P6S / 'G_FUNC_*.json'))):
        try:
            dd = load(p)
        except Exception:
            continue
        asm = '\n'.join(map(str, dd.get('assembly') or []))
        hits = re.findall(r'0x[0-9a-f]+: MOV (?:dword ptr \[[A-Z0-9]+ \+ 0x180\],[A-Z0-9x]+|E[A-Z0-9]+,dword ptr \[[A-Z0-9]+ \+ 0x180\])', asm)
        if hits:
            scan.append({'dossier': rel(p), 'hits': hits})
    c.add('SCAN_0x180', 'Exported bodies with dword access to [reg+0x180] (informational; class of reg not proven for every hit)',
          'INFO', True, 'docs/evidence/ghidra_2022 + P6 static', {'dossier_count_scanned': len(glob.glob(str(G22 / 'G_FUNC_*.json'))) +
                                                                  len(glob.glob(str(P6S / 'G_FUNC_*.json'))), 'hits': scan})

    failed = [x['id'] for x in c.checks if x['result'] != 'PASS']
    out = {
        'schema': 'pse-p6-map-spawn-identity-v1',
        'task': 'S3.1 / P6 (DOC-62): verify LoadMap spawn order and map-object identity statically',
        'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'target_exe_sha256': TARGET_SHA,
        'runtime_executed': False,
        'limitations': ['Static only: no game process, no Frida/REA session; runtime equality on two processes is INFERRED, '
                        'its gate is the MapReady manifest check of P9 (DOC-62 section ADR-2)',
                        'Decompiler output is not source; absence checks cover exported bodies and direct callees only',
                        'Virtual Spawn (+0x38) side effects per class, level-script start-up spawns and RNG draws are not covered'],
        'summary': {'checks': len(c.checks), 'pass': len(c.checks) - len(failed), 'fail': failed},
        'inputs': c.files,
        'checks': c.checks,
    }
    outp = pathlib.Path(a.out)
    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'out': rel(outp), 'checks': len(c.checks), 'fail': failed}, ensure_ascii=False))
    return 0 if not failed else 1


if __name__ == '__main__':
    sys.exit(main())
