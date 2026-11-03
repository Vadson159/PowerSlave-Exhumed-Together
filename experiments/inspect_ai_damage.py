import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

fname = os.path.join(dossier_dir, 'G_FUNC_140195050.json')
with open(fname, 'r', encoding='utf-8') as f:
    data = json.load(f)
decomp = data.get('pseudocode_non_source', '')
lines = decomp.splitlines()

print(f"Total lines in 140195050: {len(lines)}")
for i, line in enumerate(lines[:120], 1):
    print(f"{i:3d}: {line}")
