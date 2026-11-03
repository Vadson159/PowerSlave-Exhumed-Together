import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

fname = os.path.join(dossier_dir, 'G_FUNC_140195050.json')
with open(fname, 'r', encoding='utf-8') as f:
    data = json.load(f)
decomp = data.get('pseudocode_non_source', '')
lines = decomp.splitlines()

for i, line in enumerate(lines[120:], 121):
    print(f"{i:3d}: {line}")
