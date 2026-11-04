"""P20 / S6.1 (DOC-63): machine reconciliation of the 20 achievement keys.

Read-only. Starts no game process, no Frida/REA, calls no Steam API, writes only the result JSON.
Inputs: the spec docs/research/p20_achievement_spec.json, accepted static evidence
(E017, E018, E014, STRINGS_main2022, ghidra_2022 dossiers), the KPF of the test copy
(defs/, scripts/, localization/; read with zipfile) and DOC-63 itself (its key table).
EXE/KPF of the original install are only hashed.

Checks: spec <-> embedded definitions (E018) <-> key xrefs (E017) <-> emitter pseudocode
(static patterns) <-> defs (types, bits, counts) <-> scripts (no script-side awards, ending
branch) <-> localization <-> DOC-63 table. Then mutation tests: each corrupted copy of the
spec must fail at least one check.

Usage: python -X utf8 -I docs/research/p20_achievement_spec_check.py [--out PATH]
Exit code 0 only if every check passes and every mutation is caught.
"""
import argparse, copy, datetime, hashlib, json, pathlib, re, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
KPF_SHA = 'd34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd'
ORIG = pathlib.Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed')
EXES = {
    'host_test_copy': ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe',
    'client_test_copy': ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe',
    'original_read_only': ORIG / 'anubis_Shipping_Steam_x64.exe',
}
KPFS = {
    'host_test_copy': ROOT / 'experiments/m0/game/PowerslaveEX.kpf',
    'client_test_copy': ROOT / 'experiments/m1/native_second_instance/game/PowerslaveEX.kpf',
    'original_read_only': ORIG / 'PowerslaveEX.kpf',
}
SPEC = ROOT / 'docs/research/p20_achievement_spec.json'
DOC = ROOT / 'docs/63_S6_1_ACHIEVEMENT_SPEC.md'
EV = ROOT / 'docs/evidence'
G22 = EV / 'ghidra_2022'
DEFAULT_OUT = EV / 'm4/P20_ACHIEVEMENT_SPEC/SPEC_CHECK.json'
CALL = r'\(\*\*\(code \*\*\)\(\*DAT_140aeaf80 \+ 0x10\)\)\(DAT_140aeaf80,"{key}",0\);'


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


class Checks:
    def __init__(self):
        self.items = []

    def add(self, cid, desc, ok, evidence, detail=None):
        self.items.append({'id': cid, 'description': desc, 'pass': bool(ok),
                           'evidence': evidence, 'detail': detail})

    @property
    def failed(self):
        return [c for c in self.items if not c['pass']]


def parse_actors(text):
    out = {}
    for m in re.finditer(r'^Actor\s+(\w+)\s*\{(.*?)^\}', text, re.S | re.M):
        body = m.group(2)
        cls = re.search(r'classname\s+"(\w+)"', body)
        bits = re.search(r'^\s*bits\s+(-?\d+)', body, re.M)
        out[m.group(1)] = {'classname': cls and cls.group(1), 'bits': bits and int(bits.group(1))}
    return out


def parse_defines(text):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'^#define\s+(\w+)\s+(-?\d+)', text, re.M)}


def parse_loc(text):
    return {m.group(1): m.group(2) for m in re.finditer(r'^\s*(ach_(?:name|desc)_ach\d\d)\s*=\s*"(.*)"\s*$', text, re.M)}


def load_context():
    ctx = {}
    ctx['e018'] = load(EV / 'E018_ACHIEVEMENT_DEFINITIONS.json')
    ctx['e017'] = load(EV / 'E017_ACHIEVEMENT_ANCHORS.json')
    ctx['e014_text'] = (EV / 'E014_SCRIPT_REGISTRATION_MAP.json').read_text(encoding='utf-8')
    strings = load(EV / 'STRINGS_main2022.json')
    ctx['exe_strings'] = {s['value'] for s in strings['strings']}
    ctx['strings_sha'] = strings.get('binary_sha256')
    z = zipfile.ZipFile(KPFS['host_test_copy'])
    names = z.namelist()
    rd = lambda n: z.read(n).decode('utf-8', 'replace')
    ctx['scripts'] = {n: rd(n) for n in names if n.startswith('scripts/') and n.endswith('.txt')}
    ctx['defines'] = parse_defines(rd('defs/defines.txt'))
    actors = {}
    for n in names:
        if n.startswith('defs/actors/') and n.endswith('.txt'):
            for k, v in parse_actors(rd(n)).items():
                v['file'] = n
                actors[k] = v
    ctx['actors'] = actors
    ctx['loc'] = {n.split('_')[-1][:-4]: parse_loc(rd(n)) for n in names if n.startswith('localization/loc_')}
    ctx['dossiers'] = {}
    ctx['doc_text'] = DOC.read_text(encoding='utf-8') if DOC.exists() else ''
    return ctx


def dossier(ctx, va):
    key = va.lower().replace('0x', '')
    if key not in ctx['dossiers']:
        p = G22 / f'G_FUNC_{key}.json'
        ctx['dossiers'][key] = (p, load(p) if p.exists() else None)
    return ctx['dossiers'][key]


def run_checks(spec, ctx):
    c = Checks()
    achs = spec['achievements']
    keys = [a['key'] for a in achs]

    # B. structure
    c.add('SPEC_20', 'spec lists exactly 20 achievements, ordinals 0..19, unique keys',
          len(achs) == 20 and sorted(a['ordinal'] for a in achs) == list(range(20)) and len(set(keys)) == 20,
          rel(SPEC), {'count': len(achs)})

    # C. E018 embedded definitions
    e018 = {d['string_id']: d for d in ctx['e018']['definition']['achievements']}
    mism = []
    for a in achs:
        d = e018.get(a['key'])
        if not d or d['ordinal'] != a['ordinal'] or d['title'] != f"$ach_name_{a['loc']}" \
                or d['description'] != f"$ach_desc_{a['loc']}" or d['xbliveID'] != str(a['ordinal'] + 1):
            mism.append(a['key'])
    c.add('E018_MATCH', 'every key/ordinal/loc id/xbliveID equals the embedded JSON at VA 0x140529230',
          not mism and set(e018) == set(keys) and ctx['e018']['binary_sha256'] == TARGET_SHA,
          rel(EV / 'E018_ACHIEVEMENT_DEFINITIONS.json'), {'mismatch': mism, 'extra': sorted(set(e018) - set(keys))})

    # D. E017 key xrefs -> single emitter
    e017 = {s['value']: s for s in ctx['e017']['strings'] if s['value'].startswith('ACH_')}
    bad = {}
    for a in achs:
        s = e017.get(a['key'])
        fns = sorted({(x.get('containing_function') or {}).get('entry') for x in s['xrefs']}) if s else None
        if fns != [a['emitter']['va']]:
            bad[a['key']] = fns
    c.add('E017_EMITTER', 'each key string has xrefs only from the spec emitter; no other ACH_ strings in the EXE',
          not bad and set(e017) == set(keys) and ctx['e017']['binary_sha256'] == TARGET_SHA,
          rel(EV / 'E017_ACHIEVEMENT_ANCHORS.json'), {'bad': bad, 'extra': sorted(set(e017) - set(keys))})

    # E. emitter pseudocode
    for a in achs:
        p, d = dossier(ctx, a['emitter']['va'])
        code = (d or {}).get('pseudocode_non_source') or ''
        pats = list(a.get('static_patterns', []))
        if 'artifact' in a:
            pats.append(r'& ' + re.escape(a['artifact']['mask']) + r'\) != 0\) \{\s*' + CALL.format(key=a['key']))
        else:
            pats.append(r'"' + re.escape(a['key']) + r'"')
        missing = [x for x in pats if not re.search(x, code)]
        ok = d is not None and d.get('binary_sha256') == TARGET_SHA and d.get('decompile_completed') is not False and not missing
        c.add(f"PSEUDO_{a['key']}", f"{a['key']}: condition patterns present in emitter {a['emitter']['va']}",
              ok, rel(p), {'missing': missing})
    for va, pats in spec.get('helper_patterns', {}).items():
        p, d = dossier(ctx, va)
        code = (d or {}).get('pseudocode_non_source') or ''
        missing = [x for x in pats if not re.search(x, code)]
        c.add(f'HELPER_{va}', f'helper/guard patterns present in {va}', d is not None and not missing, rel(p), {'missing': missing})
    # all emissions use slot +0x10, none use +0x28 (SetProgress)
    plus28 = []
    for va in sorted({a['emitter']['va'] for a in achs}):
        p, d = dossier(ctx, va)
        code = (d or {}).get('pseudocode_non_source') or ''
        if re.search(r'\*DAT_140aeaf80 \+ 0x28', code):
            plus28.append(va)
    c.add('SERVICE_SLOT', 'all 20 keys are emitted through DAT_140aeaf80 vtable+0x10; no emitter calls +0x28 (progress)',
          not plus28, 'docs/evidence/ghidra_2022/G_FUNC_<emitter>.json', {'plus28': plus28})

    # F. localization
    en = ctx['loc'].get('english', {})
    lbad = [a['key'] for a in achs if en.get(f"ach_name_{a['loc']}") != a['title'] or en.get(f"ach_desc_{a['loc']}") != a['desc']]
    langs_missing = {lang: [k for a in achs for k in (f"ach_name_{a['loc']}", f"ach_desc_{a['loc']}") if k not in t]
                     for lang, t in ctx['loc'].items()}
    langs_missing = {k: v for k, v in langs_missing.items() if v}
    c.add('LOC_ENGLISH', 'title/desc of every key equal localization/loc_english.txt', not lbad and len(ctx['loc']) == 5,
          'PowerslaveEX.kpf:localization/loc_english.txt', {'mismatch': lbad, 'languages': sorted(ctx['loc'])})
    c.add('LOC_ALL_LANGS', 'all 5 localizations define name+desc for all 20 loc ids', not langs_missing,
          'PowerslaveEX.kpf:localization/loc_*.txt', langs_missing)

    # G. defs
    defs, actors = ctx['defines'], ctx['actors']
    for a in achs:
        if 'ai_type' in a:
            t = a['ai_type']
            p, d = dossier(ctx, a['emitter']['va'])
            code = (d or {}).get('pseudocode_non_source') or ''
            const = re.search(r'[!=]= (?:%d|%s)\)' % (t['value'], re.escape(hex(t['value']))), code)
            c.add(f"DEFS_TYPE_{a['key']}", f"{a['key']}: defs {t['define']} == {t['value']} and the emitter compares the type with this constant",
                  defs.get(t['define']) == t['value'] and bool(const), 'PowerslaveEX.kpf:defs/defines.txt; ' + rel(p),
                  {'defs_value': defs.get(t['define']), 'emitter_compare': const.group(0) if const else None})
        if 'artifact' in a:
            art = a['artifact']
            act = actors.get(art['actor'], {})
            c.add(f"DEFS_ART_{a['key']}", f"{a['key']}: {art['actor']} is kexSlavePickupArtifact with bits {art['bit']} and mask {art['mask']} == 1<<bit",
                  act.get('classname') == 'kexSlavePickupArtifact' and act.get('bits') == art['bit'] and int(art['mask'], 0) == 1 << art['bit'],
                  'PowerslaveEX.kpf:' + act.get('file', 'defs/actors/artifacts.txt'), act)
    arts = {k: v for k, v in actors.items() if v['classname'] == 'kexSlavePickupArtifact'}
    spec_arts = {a['artifact']['actor'] for a in achs if 'artifact' in a}
    c.add('DEFS_ART_SET', 'exactly the 6 spec artifacts use kexSlavePickupArtifact, bits 0..5',
          set(arts) == spec_arts and sorted(v['bits'] for v in arts.values()) == list(range(6)),
          'PowerslaveEX.kpf:defs/actors/artifacts.txt', sorted(arts))
    dolls = {k: v for k, v in actors.items() if v['classname'] == 'kexSlavePickupTeamDoll'}
    alld = next(a for a in achs if a['key'] == 'ACH_ALL_TEAM_DOLLS') if 'ACH_ALL_TEAM_DOLLS' in keys else {}
    c.add('DEFS_DOLLS', 'team dolls: N kexSlavePickupTeamDoll actors with bits 0..N-1; N == spec threshold == emitter 0x1f == loc "31"',
          len(dolls) == alld.get('threshold') == 0x1f and sorted(v['bits'] for v in dolls.values()) == list(range(len(dolls)))
          and str(alld.get('threshold')) in en.get('ach_desc_ach04', ''),
          'PowerslaveEX.kpf:defs/actors/teamdolls.txt', {'count': len(dolls), 'threshold': alld.get('threshold')})
    ankhs = {k: v for k, v in actors.items() if v['classname'] == 'kexSlavePickupMaxHealth'}
    alla = next(a for a in achs if a['key'] == 'ACH_ALL_ANKAHS') if 'ACH_ALL_ANKAHS' in keys else {}
    c.add('DEFS_ANKHS', 'health ankhs: N kexSlavePickupMaxHealth actors Item_Health_Ankah1..N with bits 0..N-1; N == spec threshold == (4 < count) == loc "5"',
          sorted(ankhs) == [f'Item_Health_Ankah{i}' for i in range(1, 6)] and sorted(v['bits'] for v in ankhs.values()) == list(range(5))
          and alla.get('threshold') == 5 and str(alla.get('threshold')) in en.get('ach_desc_ach20', ''),
          'PowerslaveEX.kpf:defs/actors/items.txt', {'actors': sorted(ankhs), 'threshold': alla.get('threshold')})
    power = sorted(k for k, v in actors.items() if v['classname'] == 'kexSlavePickupPower')
    c.add('DEFS_POWER', 'weapon boost: Item_Power is the only kexSlavePickupPower actor', power == ['Item_Power'],
          'PowerslaveEX.kpf:defs/actors/items.txt', power)
    c.add('DEFS_MACHETE', 'Wep_Machete defined (weapon slot 0)', defs.get('Wep_Machete') == 0, 'PowerslaveEX.kpf:defs/defines.txt', defs.get('Wep_Machete'))
    classes = ['kexSlavePickupArtifact', 'kexSlavePickupTeamDoll', 'kexSlavePickupMaxHealth', 'kexSlavePickupPower', 'kexSlaveAI', 'kexSlavePuppet']
    c.add('EXE_CLASSES', 'pickup/AI class names used by defs exist as EXE strings', all(x in ctx['exe_strings'] for x in classes)
          and ctx['strings_sha'] == TARGET_SHA, rel(EV / 'STRINGS_main2022.json'), classes)

    # H. scripts
    award_refs = sorted(n for n, t in ctx['scripts'].items() if re.search(r'ACH_|achiev', t, re.I))
    c.add('SCRIPTS_NO_AWARD', 'no script under scripts/ references an ACH_ key or achievements (all 20 are native-only)',
          not award_refs and len(ctx['scripts']) == 70, 'PowerslaveEX.kpf:scripts/', {'refs': award_refs, 'scripts': len(ctx['scripts'])})
    tomb = ctx['scripts'].get('scripts/map/TOMBEND_script.txt', '')
    c.add('SCRIPT_TOMBEND_BAD_BRANCH', 'TOMBEND: Player().GetQuestItems() != 0xFF -> kexSlaveEvents::EndGame(false)',
          bool(re.search(r'if\(Player\(\)\.GetQuestItems\(\) != 0xFF\)\s*\{\s*kexSlaveEvents::EndGame\(false\);', tomb))
          and 'void EndGame(const bool)' in ctx['e014_text'] and 'FUN_1401f1570' in ctx['e014_text'],
          'PowerslaveEX.kpf:scripts/map/TOMBEND_script.txt; ' + rel(EV / 'E014_SCRIPT_REGISTRATION_MAP.json'))
    good_direct = sorted(n for n, t in ctx['scripts'].items() if re.search(r'EndGame\(\s*true', t))
    c.add('SCRIPT_TOMBEND_GOOD_BRANCH', 'no script calls EndGame(true): good ending (state 7) is entered by a native path after the 0xFF branch',
          not good_direct and bool(re.search(r'GetQuestItems\(\) == 0xFF', tomb)), 'PowerslaveEX.kpf:scripts/', good_direct)

    # H2. user decisions PR-1..PR-3 (2026-11-04) recorded and reflected
    ud = spec.get('user_decisions', {})
    taints = {t['id'] for t in spec.get('run_taints', [])}
    c.add('USER_DECISIONS', 'PR-1..PR-3 recorded in spec and DOC-63; taints IMPORTED_STATE/DIFFICULTY_CHANGED removed',
          set(ud) == {'PR-1', 'PR-2', 'PR-3'} and taints == {'CHEAT'}
          and all(re.search(rf'\*\*{k}\*\*[^\n]*(принято|решение пользователя)', ctx['doc_text']) for k in ud),
          rel(SPEC) + '; ' + rel(DOC), {'decisions': sorted(ud), 'taints': sorted(taints)})

    # I. DOC-63 table
    rows = {}
    for line in ctx['doc_text'].splitlines():
        m = re.match(r'^\|\s*(\d+)\s*\|\s*`(ACH_\w+)`\s*\|(.*)$', line)
        if m:
            rows.setdefault(m.group(2), []).append((int(m.group(1)), m.group(3)))
    dbad = [a['key'] for a in achs if len(rows.get(a['key'], [])) != 1 or rows[a['key']][0][0] != a['ordinal']
            or a['emitter']['rva'] not in rows[a['key']][0][1]]
    c.add('DOC63_TABLE', 'DOC-63 key table: 20 rows, each key once with its ordinal and emitter RVA',
          ctx['doc_text'] != '' and not dbad and set(rows) == set(keys), rel(DOC), {'mismatch': dbad, 'rows': len(rows)})
    return c


MUTATIONS = [
    ('drop_key', lambda s: s['achievements'].pop(7)),
    ('swap_ordinals', lambda s: (s['achievements'][0].__setitem__('ordinal', 1), s['achievements'][1].__setitem__('ordinal', 0))),
    ('dolls_threshold_23', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_ALL_TEAM_DOLLS').__setitem__('threshold', 23)),
    ('artifact_bit_swap', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_SANDALS')['artifact'].update(bit=1, mask='2')),
    ('wrong_emitter', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_KILL_MANTIS_MACHETE')['emitter'].update(va='0x140195050', rva='0x195050')),
    ('ankhs_threshold_4', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_ALL_ANKAHS').__setitem__('threshold', 4)),
    ('queen_is_ai_queen', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_DEFEAT_QUEEN')['ai_type'].update(define='AI_Queen', value=4)),
    ('hard_includes_pharaoh', lambda s: next(a for a in s['achievements'] if a['key'] == 'ACH_FINISH_HARD').__setitem__(
        'static_patterns', [r'if \(\*\(int \*\)\(param_1 \+ 0x1d8\) >= 1\)'])),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    args = ap.parse_args()

    hashes = {}
    for group, paths, want in (('exe', EXES, TARGET_SHA), ('kpf', KPFS, KPF_SHA)):
        for name, p in paths.items():
            got = sha256_file(p) if p.exists() else None
            hashes[f'{group}:{name}'] = {'path': rel(p), 'sha256': got, 'match': got == want}
    spec = load(SPEC)
    ctx = load_context()
    checks = run_checks(spec, ctx)
    checks.add('INPUT_HASHES', 'EXE copies == target SHA, KPF copies == accepted KPF SHA (originals only hashed)',
               all(h['match'] for h in hashes.values()) and spec['binary_sha256'] == TARGET_SHA and spec['kpf_sha256'] == KPF_SHA,
               'filesystem', hashes)

    mut = []
    for name, fn in MUTATIONS:
        s = copy.deepcopy(spec)
        fn(s)
        failed = [x['id'] for x in run_checks(s, ctx).failed]
        mut.append({'mutation': name, 'caught': bool(failed), 'failed_checks': failed})

    ok = not checks.failed and all(m['caught'] for m in mut)
    result = {
        'schema': 'pse.p20.achievement_spec_check.v1',
        'task': 'S6.1 / P20 (mode A)',
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
        'runtime': 'none: static evidence + KPF text only; no game, Frida/REA or Steam',
        'binary_sha256': TARGET_SHA,
        'inputs': {'spec': rel(SPEC), 'spec_sha256': sha256_file(SPEC), 'doc': rel(DOC),
                   'doc_sha256': sha256_file(DOC) if DOC.exists() else None,
                   'checker_sha256': sha256_file(pathlib.Path(__file__))},
        'summary': {'checks': len(checks.items), 'passed': len(checks.items) - len(checks.failed),
                    'mutations': len(mut), 'mutations_caught': sum(m['caught'] for m in mut), 'pass': ok},
        'checks': checks.items,
        'mutations': mut,
    }
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print(f"checks {result['summary']['passed']}/{result['summary']['checks']}, "
          f"mutations caught {result['summary']['mutations_caught']}/{len(mut)} -> {'PASS' if ok else 'FAIL'}")
    for x in checks.failed:
        print('FAIL', x['id'], json.dumps(x['detail'], ensure_ascii=False)[:300])
    for m in mut:
        if not m['caught']:
            print('MUTATION NOT CAUGHT', m['mutation'])
    print('->', rel(out))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
