import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

for va in ['1401d8590', '1401d8770', '1401d8a00']:
    fname = os.path.join(dossier_dir, f'G_FUNC_{va}.json')
    with open(fname, 'r', encoding='utf-8') as f:
        d = json.load(f)
    print(f"\n=================== VA: 0x{va} ===================")
    decomp = d.get('pseudocode_non_source', '')
    for i, l in enumerate(decomp.splitlines(), 1):
        print(f"{i:2d}: {l}")
