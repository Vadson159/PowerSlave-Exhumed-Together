import json

with open(r'PowerSlave_Exhumed_Together_mod\docs\evidence\FUNCTION_CATALOG.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

records = data.get('records', [])
for i, r in enumerate(records):
    print(f"{r.get('id')} | {r.get('va')} | {r.get('rva')} | {r.get('role_analyst')} | {r.get('role_classification')}")
