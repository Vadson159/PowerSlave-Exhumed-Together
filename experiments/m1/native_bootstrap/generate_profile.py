"""Bind the small native adapter to accepted M0 evidence; no analysis or execution."""
from pathlib import Path
import hashlib
import json
import sys

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
EVIDENCE = ROOT / 'docs/evidence/m1/M11_IMPLEMENTATION_20261101'
EXPECTED = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
sys.path.insert(0, str(ROOT / 'docs/research'))
from inventory import PE


def main():
    exe = ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
    if hashlib.sha256(exe.read_bytes()).hexdigest() != EXPECTED:
        raise ValueError('Exact-build gate failed')
    witness = ROOT / 'docs/evidence/m0/C01/status_01_constructor.json'
    messages = json.loads(witness.read_text(encoding='utf-8-sig'))['structuredContent']['normalized_result']['messages']
    payloads = [m.get('payload', {}) for m in messages]
    if not any(p.get('kind') == 'C_native_constructor_witness' and p.get('model_matches') and p.get('size') == 408 and not p.get('differences') for p in payloads if isinstance(p, dict)):
        raise ValueError('Missing accepted constructor witness')
    pe = PE(exe)
    sites = [
        ('Tick', 0x1b6ec0, 'docs/evidence/ghidra_2022/G_FUNC_1401b6ec0.json'),
        ('Update', 0x1dfe30, 'docs/evidence/ghidra_2022/G_FUNC_1401dfe30.json'),
        ('Commands', 0x1e34a0, 'docs/evidence/ghidra_2022/G_FUNC_1401e34a0.json'),
        ('Turning', 0x1e2ad0, 'docs/evidence/ghidra_2022/G_FUNC_1401e2ad0.json'),
        ('Getter', 0x1b5520, 'docs/evidence/m0/C01/RESULT.md'),
        ('StateExit', 0x1b6670, 'docs/evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT.md'),
        ('Shutdown', 0xfcc20, 'docs/evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md'),
        ('ActorDtor', 0x19f210, 'docs/evidence/ghidra_2022/G_FUNC_14019f210.json'),
        ('PuppetTick', 0x1db8a0, 'docs/evidence/ghidra_2022/G_FUNC_1401db8a0.json'),
        ('EngineShutdown', 0xa4c90, 'docs/evidence/m0_static_final_acceptance/G_FUNC_1400a4c90.json'),
        ('FinalExit', 0xfc580, 'docs/evidence/m0_static_final_acceptance/G_FUNC_1400fc580.json'),
        ('Reset', 0x1dfa70, 'docs/evidence/m0/C01/RESULT.md'),
        ('Spawn', 0x1d4780, 'docs/evidence/m0/A03/RESULT.md'),
        ('WeaponReady', 0x20f9e0, 'docs/evidence/m0/C01/RESULT.md'),
    ]
    records = []
    text = ['// Generated from exact-file bytes and existing accepted M0 evidence.',
            '#pragma once', '#include <cstdint>', '#include <cstddef>',
            'namespace profile {',
            f'inline constexpr char Sha256[] = "{EXPECTED}";',
            'inline constexpr wchar_t Workspace[] = LR"(' + str(ROOT) + ')";',
            'inline constexpr wchar_t Executable[] = LR"(' + str(exe) + ')";',
            'struct Site { const char* name; std::uint32_t rva; unsigned char prefix[16]; };',
            'enum Index : std::size_t { ' + ', '.join(s[0] for s in sites) + ', Count };',
            'inline constexpr Site Sites[] = {']
    for name, rva, evidence in sites:
        if not (ROOT / evidence).is_file():
            raise ValueError('Missing evidence ' + evidence)
        offset = pe.off(rva)
        prefix = pe.b[offset:offset + 16]
        records.append(dict(analyst_role=name, binary_sha256=EXPECTED, preferred_va=hex(0x140000000 + rva),
                            rva=hex(rva), file_offset=hex(offset), prefix16=prefix.hex(), evidence=evidence))
        text.append('  {"%s", 0x%x, {%s}},' % (name, rva, ','.join('0x%02x' % b for b in prefix)))
    text += ['};', '} // namespace profile', '']
    (LAB / 'src').mkdir(exist_ok=True)
    (LAB / 'src/build_profile.hpp').write_text('\n'.join(text), encoding='utf8')
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (EVIDENCE / 'NATIVE_COORDINATES.json').write_text(json.dumps(dict(
        binary_sha256=EXPECTED, preferred_image_base='0x140000000', sites=records,
        reused_constructor=dict(path=str(witness.relative_to(ROOT)), sha256=hashlib.sha256(witness.read_bytes()).hexdigest()),
        limitations=['Analyst roles, not recovered KEX source names.', 'Coordinates and Win64 contracts apply only to the accepted EXE.',
                     'Source generation is not runtime acceptance.']), indent=2), encoding='utf8')
    print(json.dumps(dict(status='PASS', sites=len(records), exe_sha256=EXPECTED, execution=False)))


if __name__ == '__main__':
    main()
