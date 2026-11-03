import json, os

fname = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022\G_FUNC_1401d8360.json'
with open(fname, 'r', encoding='utf-8') as f:
    data = json.load(f)

decomp = data.get('pseudocode_non_source', '')
for i, line in enumerate(decomp.splitlines()[:60], 1):
    print(f"{i:3d}: {line}")
