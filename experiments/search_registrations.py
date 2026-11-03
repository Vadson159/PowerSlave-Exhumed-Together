import json

with open(r'PowerSlave_Exhumed_Together_mod\docs\evidence\E014_SCRIPT_REGISTRATION_MAP.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

print("Root keys:", list(data.keys()) if isinstance(data, dict) else len(data))

# search in registration entries
entries = data if isinstance(data, list) else data.get('registrations', data.get('methods', []))
print(f"Found {len(entries)} items")
for e in entries:
    name = str(e)
    if any(k in name.lower() for k in ['pickup', 'give', 'weapon', 'item', 'inventory', 'ammo', 'health', 'armor', 'doll', 'key', 'artifact', 'touch', 'use']):
        print(name[:120])
