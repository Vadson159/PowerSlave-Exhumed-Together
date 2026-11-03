import json

fname = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022\G_INSPECT_1401f1510.json'
with open(fname, 'r', encoding='utf-8') as f:
    d = json.load(f)

for ins in d.get('instructions', [])[:15]:
    print(ins.get('va'), ins.get('instruction'))
