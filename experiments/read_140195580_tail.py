import json, os

fname = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022\G_FUNC_140195580.json'
with open(fname, 'r', encoding='utf-8') as f:
    d = json.load(f)

decomp = d.get('pseudocode_non_source', '')
lines = decomp.splitlines()
for i, l in enumerate(lines[79:], 80):
    print(f"{i:2d}: {l}")
