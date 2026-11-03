import json, os

fname = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022\G_FUNC_1401a1370.json'
with open(fname, 'r', encoding='utf-8') as f:
    d = json.load(f)

decomp = d.get('pseudocode_non_source', '')
lines = decomp.splitlines()
print(f"Total lines in FUN_1401a1370: {len(lines)}")
for i, l in enumerate(lines[:60], 1):
    print(f"{i:2d}: {l}")
