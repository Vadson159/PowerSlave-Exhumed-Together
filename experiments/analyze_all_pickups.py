import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

pickup_vas = [
    '1401d8470',
    '1401d8590',
    '1401d8770',
    '1401d8870',
    '1401d8a00',
    '1401d8ac0',
    '1401d8dc0',
    '1401d8f30',
    '1401d9140',
    '1401d91f0'
]

for va in pickup_vas:
    fname = os.path.join(dossier_dir, f'G_FUNC_{va}.json')
    if not os.path.exists(fname):
        print(f"Missing {fname}")
        continue
    with open(fname, 'r', encoding='utf-8') as f:
        d = json.load(f)
    print(f"\n=================== VA: 0x{va} ===================")
    decomp = d.get('pseudocode_non_source', '')
    lines = decomp.splitlines()
    for i, l in enumerate(lines[:45], 1):
        print(f"{i:2d}: {l}")
    if len(lines) > 45:
        print(f"... ({len(lines)} lines total)")
