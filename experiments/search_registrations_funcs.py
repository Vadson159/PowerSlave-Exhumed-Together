import json

with open(r'PowerSlave_Exhumed_Together_mod\docs\evidence\E014_SCRIPT_REGISTRATION_MAP.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

funcs = data.get('functions', [])
print(f"Functions count: {len(funcs)}")
for f in funcs:
    decl = f.get('declaration', '')
    name = f.get('name', '')
    text = f"{name} {decl}".lower()
    if any(k in text for k in ['pickup', 'give', 'weapon', 'item', 'inventory', 'ammo', 'health', 'armor', 'doll', 'key', 'artifact', 'touch', 'use', 'power', 'transmitter']):
        print(f"{f.get('va')} | {f.get('rva')} | {decl}")
