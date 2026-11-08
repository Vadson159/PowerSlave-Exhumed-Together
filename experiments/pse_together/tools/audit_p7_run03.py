"""Read-only execution audit of P7 RUN03; does not accept mode B/V itself."""
import collections
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P7'


def sha(p):
    return hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()


def rows(role):
    with (OUT / f'P7_{role.upper()}_03/native.jsonl').open() as f:
        for line in f:
            yield json.loads(line)


def main():
    checks = []
    def check(name, condition):
        checks.append(dict(name=name, passed=bool(condition)))
    client = [x for x in rows('client') if x['event'].startswith('p7_') or x['event'] in
              ['client_map_status', 'blocked', 'client_quiesced', 'client_native_final_exit']]
    commits = [x for x in client if x['event'] == 'p7_own_committed']
    by_seq = collections.defaultdict(list)
    for x in commits:
        by_seq[x['snapshot_seq']].append(x)
    fields = {'health':'health', 'weapon':'active_weapon', 'owned':'weapon_mask',
              'keys':'keys', 'artifacts':'artifacts', 'abilities':'abilities', 'quest':'quest_items',
              'ankhs':'ankh_count', 'ankh_mask':'ankh_mask', 'team_dolls':'team_dolls',
              'death_sequence':'death_sequence', 'ammo':'ammo'}
    matches = []
    fixtures = []
    primary_count = 0
    primary_bad = []
    for x in rows('host'):
        if x['event'] == 'p7_host_fixture':
            fixtures.append(x)
        elif x['event'] == 'network_native_update':
            primary_count += 1
            if not x['unchanged'] or x['p1_before'] != x['p1_after']:
                primary_bad.append(x['seq'])
        elif x['event'] == 'p7_native_scalars' and x['slot'] == 1 and x['snapshot_seq'] in by_seq:
            for c in by_seq[x['snapshot_seq']]:
                bad = [a for a,b in fields.items() if c[a] != x[b]]
                # Frozen wire enum alive=0/dead=2; scalar DTO alive=0/dead=1.
                if c['life'] != (0 if x['life_state']==0 else 1):
                    bad.append('life')
                if c['pending'] != (255 if x['pending_weapon'] == -1 else x['pending_weapon']):
                    bad.append('pending')
                if c['server_tick'] != x['server_tick']:
                    bad.append('server_tick')
                matches.append(dict(snapshot_seq=x['snapshot_seq'], client_record=c['record'],
                                    host_record=x['seq'], different=bad))
    check('actual commits exist', len(commits) > 100)
    check('every commit has exact source snapshot', len(matches) == len(commits))
    check('all source fields match actual native readback', all(not x['different'] for x in matches))
    check('every commit readback_equal', all(x['readback_equal'] for x in commits))
    check('host primary independent throughout native updates', primary_count > 100 and not primary_bad)
    check('all fixture calls preserve host primary', all(x['primary_unchanged'] and x['guards'] and x['post_world_tick'] for x in fixtures))
    check('health damage and heal sequence', {200,183,83,103,-97} <= {x['health'] for x in commits})
    for n in range(8):
        check(f'weapon {n} selected natively', any(x['weapon'] == n and x['pending'] == 255 for x in commits))
    check('all weapon ownership bits', any(x['owned'] == 255 for x in commits))
    check('all seven ammo pools changed', any(x['ammo'] == [0,53,31,12,60,14,243,4] for x in commits))
    check('key grant and withdrawal', {0,15,5} <= {x['keys'] for x in commits})
    for field, maximum in [('artifacts',63),('abilities',3),('quest',255),('ankhs',5),('ankh_mask',31),('team_dolls',31)]:
        check(f'{field} zero nonzero and maximum', {0,1,maximum} <= {x[field] for x in commits})
    check('invalid key mask16 rejected without effects', any(x['op']==6 and x['a']==16 and not x['accepted'] and x['keys_before']==x['keys_after']==5 for x in fixtures))
    damage = [x for x in client if x['event']=='p7_damage_suppressed']
    check('four local damage kinds suppressed', {0,1,2,3} <= {x['kind'] for x in damage})
    local = [x for x in client if x['event']=='p7_local_damage_fixture']
    check('local damage fixture HP unchanged', len(local)==4 and all(x['suppressed'] and x['before']==x['after']==183 for x in local))
    death = [x for x in client if x['event']=='p7_native_applied' and x['death_sequence']==1]
    check('single native authoritative death', len(death)==1 and death[0]['health']==-97)
    dead = [x for x in commits if x['life']==1]
    check('repeated dead snapshots do not repeat death', len(dead)>=3 and all(x['death_sequence']==1 and x['health']==-97 for x in dead))
    transition = [x for x in client if x['event']=='client_map_status' and x.get('fault')=='transition' and x['record']>death[0]['record']]
    check('death followed by map transition barrier', bool(transition))
    barrier_record = transition[0]['record']
    check('no native apply after barrier', not any(x['event']=='p7_native_applied' and x['record']>barrier_record for x in client))
    restore = [x for x in client if x['event']=='p7_native_hook_restore']
    check('fourteen exact native hook restores', len(restore)==14 and all(x['exact16'] and x['storage_pinned'] for x in restore))
    for role in ['host','client']:
        run=OUT/f'P7_{role.upper()}_03'
        gate=json.loads((run/'ISOLATION_GATE.json').read_text())
        cfg=json.loads((run/'NATIVE_ARTIFACT.json').read_text())['config']
        check(f'{role} exact EXE and isolation', gate['status']=='PASS' and gate['pid']==cfg['pid'] and gate['binary_sha256']=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d')
        check(f'{role} loaded DLL unchanged', sha(Path(cfg['dll']))==cfg['dll_sha256'])
        wait=[json.loads(l) for l in (run/'PROCESS_WAIT.jsonl').read_text().splitlines()][-1]
        check(f'{role} retained and duplicate handles exit0', wait['exit_code']==0 and wait['wait']==wait['duplicate_wait']==0)
    integrity=json.loads((OUT/'INTEGRITY_AFTER_RUN03.json').read_text())
    before=json.loads((OUT/'INTEGRITY_AFTER_RUN01.json').read_text())['game']
    # Historical DOC52 checker covers its own profile path and Steam subset.
    # The real Powerslave EX profile from protected_before is independently
    # checked in P7_INDEPENDENT_RUN03_MACHINE.json; do not imply this does it.
    check('original77 and historical protected subset unchanged', integrity==before and integrity['files']==77 and integrity['original_full_snapshot_equal'] and not integrity['errors'] and not integrity['client_copy_errors'])
    closed=json.loads((OUT/'REA_CLOSED_RUN03.json').read_text())
    check('all fifteen owned REA sessions closed', len(closed)==15 and all(not x['result'].get('isError') for x in closed))
    faults=[x for x in client if x['event']=='p7_native_fault']
    result=dict(status='PASS' if all(x['passed'] for x in checks) else 'FAIL',
                scope='execution audit only; independent package acceptance required',
                checks=checks, count=len(checks), failures=[x['name'] for x in checks if not x['passed']],
                commits=len(commits), exact_matches=matches, fixtures=fixtures, local_damage=local,
                native_faults=faults, death_transition=transition,
                limitation='RUN03 graph rejection at death-to-gameover transition; second flag1000 message also triggered by fault latch and does not attest actual bit. No runtime local pickup/attack witness.',
                logs={role:sha(OUT/f'P7_{role.upper()}_03/native.jsonl') for role in ['host','client']})
    (OUT/'RUN03_EXECUTION_AUDIT.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:result[k] for k in ['status','count','failures','commits','local_damage']},indent=2))


if __name__ == '__main__':
    main()
