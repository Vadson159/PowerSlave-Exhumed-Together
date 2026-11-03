import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

for va in ['1401be2f0', '1401be600']:
    fname = os.path.join(dossier_dir, f'G_FUNC_{va}.json')
    with open(fname, 'r', encoding='utf-8') as f:
        d = json.load(f)
    print(f"\n=================== VA: 0x{va} ===================")
    decomp = d.get('pseudocode_non_source', '')
    for i, l in enumerate(decomp.splitlines()[:50], 1):
        print(f"{i:2d}: {l}")
