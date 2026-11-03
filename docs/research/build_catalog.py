"""Index existing evidence only; no new decompilation and no target modification."""
import datetime, hashlib, json
from pathlib import Path
from inventory import PE

ROOT=Path(__file__).resolve().parents[2]
E=ROOT/'docs/evidence'
PE_MAIN=PE(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe')
ROLES={
'140454a70':('PE entry stub','Toolchain probe; not gameplay loop'),
'1401b01d0':('GameInstance Start, diagnostic-string corroborated','Initialization and embedded Player reset'),
'1401b1000':('GameInstance update candidate','Frame integration; contains UI/state gates'),
'1401b1310':('GameInstance render candidate','Separate presentation from simulation'),
'1401b6480':('State dispatcher','Transition vs unchanged-state tick'),
'1401b6ec0':('State/gameplay tick candidate','Authoritative tick order and world/client suppression'),
'1401b6670':('Leave-state candidate','Handle/script/level teardown'),
'1401b6a00':('Enter-state candidate','Level barrier, player initialization, ending achievements'),
'1401b0e20':('Shutdown candidate','Resource lifetime'),
'1401b0fc0':('Player command reset branch','Avoid state/input bleed'),
'1401b5520':('Get embedded Player, vtable+a8','Returns this+0x40; singleton dependency'),
'1401d41f0':('SpawnActor from map record, diagnostic corroborated','Map player type=0 and class restriction'),
'1401d4780':('SpawnActor by integer type, diagnostic corroborated','Runtime actor allocation/pooling/link/spawn'),
'1401d4b30':('Additional SpawnActor overload candidate','Alternative factory entry; ABI not validated'),
'1401d4e20':('Factory creation linked to global node list','World/mover creation candidate'),
'1401d5000':('Factory pool-return candidate','Object address reuse; network ID generations'),
'1401d50b0':('Additional factory creation candidate','Class/node/spawn lifecycle'),
'1401db790':('Puppet allocator/constructor path','Allocates and zeros 0x1e0 then installs puppet vtable'),
'1401dbc60':('Puppet virtual Spawn','Rebinds singleton Player+d8 and writes Puppet+1b8 owner'),
'1401db8a0':('Puppet virtual update','Owner-dependent movement and collision'),
'1401dbd00':('Puppet damage response candidate','Owner-dependent death/pain/weapon state'),
'1401dbfc0':('Puppet interaction callback candidate','Inspect full payload before using as hook'),
'1401d9370':('Puppet deleting destructor candidate','Distinct from Remove or pool return'),
'1401dfa70':('Player reset candidate','Handle refcounts and field resets'),
'1401dfa20':('Player.GetHealth registered callback','Resolves node actor health, fallback difficulty value'),
'1401dfe30':('Player/controller update candidate','Input, timers, interaction and local/global dependencies'),
'1401e04e0':('Player level-initialization candidate','Weapon animation, position, health restore'),
'1401e34a0':('Input command-building candidate','Uses shared platform input services'),
'1401e2ad0':('Input turning/command candidate','Reentrancy/device separation unknown'),
'1401dc6e0':('Puppet movement branch','Collision and owner-dependent physics'),
'1401dd670':('Puppet movement branch','Mode mapping and commands need runtime validation'),
'1401ddf70':('Puppet movement branch','Owner abilities, collision and relink'),
'1401de8b0':('Puppet movement branch','Special-mode movement candidate'),
'1401a4990':('Collision/move trace candidate','Global scratch, callbacks, not pure/reentrant proven'),
'14019f2d0':('Base actor update candidate','Physics, touch, damage and lifecycle'),
'1401a02f0':('Base actor Spawn candidate','Definitions, health, flags and geometry initialization'),
'1401a02b0':('Actor Remove candidate','Changes flags/links; does not prove immediate free'),
'140172f10':('Script Remove wrapper','Virtual +0x30 dispatch'),
'1401d3d70':('LinkSector candidate','Node membership and actor sector'),
'1401d58e0':('Node-list callback pass','Iteration order and dispatch'),
'1401d3230':('Node/object update callback','Per-tick stamp guard then virtual +0x20'),
'1401b6360':('Deferred node retirement sweep','Refcounts and factory pool-return'),
'1401b61d0':('Node maintenance candidate','Further lifetime details remain unknown'),
'140197ee0':('ObjectNode checked Puppet payload helper','Node+0x28 object, custom RTTI type check'),
'140194ce0':('AI virtual update','State transitions, dead-target clear and chase'),
'1401953d0':('AI Spawn candidate','Base Spawn and difficulty'),
'140195050':('AI damage/death response','Boss achievements, target/instigator/sourceOwner'),
'1401971e0':('AI ChaseTarget, diagnostic corroborated','Target node and singleton-player fallback'),
'1401a1370':('InflictDamage registered callback','Victim health and virtual damage response'),
'14020f760':('Weapon ChangeAnim, diagnostic corroborated','Immediately executes animation-frame actions'),
'1401e0d70':('Player use/interaction candidate','Trace to world face event handler'),
'1401bea70':('Sector event candidate','Actor/sector, scripts and world effects'),
'1401bccb0':('Face/use event candidate','Multiple singleton Player lookups'),
'1401bd1b0':('World event helper candidate','Event rules and geometry; not fully classified'),
'1401be600':('Actor event helper candidate','Artifact/factory branches; not a verified travel API'),
'1401c7b70':('Level LoadMap, diagnostic corroborated','Geometry/actors/script/resource load'),
'1401cc490':('ReadActors candidate','0x14-byte map actor records -> factory'),
'1401c90e0':('Level unload candidate','Frees level arrays and resources'),
'1401c9690':('Level tick candidate','Level-local update; complete roles unresolved'),
'1401e91b0':('Registered RNG Bit','Mutates global RNG state'),
'1401e9260':('Registered RNG UInt8','Eight Bit calls; no determinism assumption'),
'1401f1880':('Global/player script registration','Exact declarations and callback addresses'),
'1401f5040':('Actor script registration','Getter and gameplay bindings'),
'1401f6a70':('Puppet script registration','Typed interface; not instance-construction proof'),
'1401f84a0':('AI script registration','Typed AI interface'),
'1401f1180':('Player.GetButtons registered callback','Reads uint32 at controller+0'),
'140058e50':('CastToActor identity callback','Returns first argument; no ownership conversion'),
'1401af7d0':('Console savegame command','Calls save slot pipeline'),
'1401af8f0':('Console loadgame command','Read requests plus apply slot'),
'1401b3260':('Save slot serialization candidate','Version/state/fields/platform save; not full world snapshot'),
'1401b1de0':('Platform save-slot read request candidate','Asynchronous request and completion lifetime'),
'1401b3f20':('Parse/apply loaded slot candidate','Readiness/slot gates and player restore'),
'1401b5b80':('Player/campaign subset pack candidate','Per-player vs global separation'),
'1401b5ce0':('Player/campaign subset restore candidate','Separate from live world baseline'),
'1401b5df0':('Loaded player/world application candidate','Position/sector/bitset restoration'),
'1401b5850':('Campaign finish achievement conditions','Difficulty and no-ankhs field'),
'1401d8ac0':('Artifact pickup + achievements','Uses collecting Puppet owner; artifact mask'),
'1401d8870':('Ankh pickup + achievement','Uses global Player and count >4'),
'140199710':('Melee action and mantis kill condition','Trace/damage then type=5 and health<=0'),
'140184110':('Steam achievements SetAchievement, diagnostic corroborated','Readiness/cheat guards then Steam virtual calls'),
'140184640':('Achievement SetProgress candidate','Progress vs completion and local Steam persistence'),
'140184a90':('OnUserStatsReceived, diagnostic corroborated','App/result validation and achievement cache'),
'14018ed40':('SocialManager Init, diagnostic corroborated','SteamAPI_Init and callback registration'),
'140182cc0':('SteamUserStats interface initializer','Uses STEAMUSERSTATS_INTERFACE_VERSION012'),
'14018fbf0':('Social update / Steam callback pump','Calls SteamAPI_RunCallbacks'),
'1401e0910':('Ankh acquisition helper candidate','Count/bitmask update used before all-ankhs condition'),
'140107820':('Cheats-enabled notification/flag path','Corroborates achievement gating'),
}

def main():
    records=[]
    for p in sorted((E/'ghidra_2022').glob('G_FUNC_*.json')):
        d=json.loads(p.read_text(encoding='utf8'));f=d['function'];a=f['entry'];key=a.removeprefix('0x')
        role,coop=ROLES.get(key,('UNKNOWN semantic role; existing dossier indexed','Review saved dossier only if a concrete experiment needs it'))
        va=int(a,16);rva=va-PE_MAIN.base
        records.append(dict(id='F2022_'+key,va=a,rva=hex(rva),file_offset=hex(PE_MAIN.off(rva)),
          binary_sha256=d['binary_sha256'],evidence=str(p.relative_to(ROOT/'docs')).replace('\\','/'),
          evidence_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),role_analyst=role,
          role_classification='INFERRED; exact diagnostic/script names independently corroborated where stated',
          observation_classification='VERIFIED static Ghidra output, not executed behavior',
          recovered_prototype_NOT_VALIDATED=f.get('signature_ghidra_inferred'),
          calling_convention_ghidra=f.get('calling_convention_ghidra'),
          body_ranges=d.get('body_ranges'),body_bytes=d.get('body_bytes'),
          decompile_completed=d.get('decompile_completed'),decompile_error=d.get('decompile_error'),
          direct_callees=d.get('direct_callees'),incoming_entry_references=d.get('incoming_entry_references'),
          significance_for_coop=coop,
          limitations=d['limitations']+['No hook/trampoline viability, runtime ABI, reentrancy or gameplay behavior validated'],
          remaining_validation='Use candidate-specific dynamic experiment in docs/07_HOOK_CANDIDATES.md or 10_OPUS_HANDOFF.md; no new broad decompilation required'))
    out={'schema':'pse-function-catalog-v1','captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
         'note':'Derived index of existing direct Ghidra dossiers; no new analysis; file mappings from original PE sections',
         'records':records}
    (E/'FUNCTION_CATALOG.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
    lines=['# Каталог сохранённых функций 2022','',
      'Автоматический индекс существующих dossiers. VERIFIED относится к статическим данным Ghidra; роли и восстановленные прототипы требуют интерпретации. Не все индексированные функции являются кандидатами на перехват. Подробные риски/эксперименты — в [07](../07_HOOK_CANDIDATES.md).',
      '',f'Бинарный SHA-256: `{records[0]["binary_sha256"]}`. Полные поля, call relations, прототипы и Evidence SHA — [JSON](../evidence/FUNCTION_CATALOG.json).',
      '', '| ID / preferred VA | RVA | File offset | Роль аналитика / Evidence |','|---|---|---|---|']
    for r in records:
        lines.append(f'| `{r["va"]}` | `{r["rva"]}` | `{r["file_offset"]}` | [{r["role_analyst"]}](../{r["evidence"]}) |')
    (ROOT/'docs/research/FUNCTION_CATALOG.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print(json.dumps({'functions':len(records),'completed':sum(r['decompile_completed'] is True for r in records)}))

if __name__=='__main__':main()
