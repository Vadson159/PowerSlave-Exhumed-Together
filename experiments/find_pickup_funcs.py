import json

with open(r'PowerSlave_Exhumed_Together_mod\docs\evidence\FUNCTION_CATALOG.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

for r in data.get('records', []):
    va_int = int(r['va'], 16)
    if 0x1401d6000 <= va_int <= 0x1401da000:
        print(f"{r['va']} | {r['rva']} | {r.get('role_analyst')}")
