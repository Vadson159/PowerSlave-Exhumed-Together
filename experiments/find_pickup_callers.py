import json

fname = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022\G_FUNC_1401d8360.json'
with open(fname, 'r', encoding='utf-8') as f:
    data = json.load(f)

refs = data.get('incoming_entry_references', [])
print(f"Total incoming references to FUN_1401d8360: {len(refs)}")
for r in refs:
    print(r.get('from'), r.get('containing_function', {}).get('entry'), r.get('containing_function', {}).get('name'))
