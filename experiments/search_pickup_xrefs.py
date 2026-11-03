import json

for fname in ['E009_MAIN_TYPE_STRING_ANCHORS.json', 'E013_GHIDRA_ANCHOR_XREFS.json']:
    path = f'PowerSlave_Exhumed_Together_mod/docs/evidence/{fname}'
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    print(f"=== {fname} ===")
    content = json.dumps(data)
    for term in ['kexSlavePickup', 'PickupKey', 'PickupWeapon', 'PickupHealth', 'PickupAmmo', 'PickupPower', 'PickupQuest', 'PickupTeamDoll', 'PickupMap']:
        count = content.count(term)
        if count:
            print(f"  {term}: {count} occurrences")
