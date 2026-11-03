import json

with open('PowerSlave_Exhumed_Together_mod/docs/evidence/E009_MAIN_TYPE_STRING_ANCHORS.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

for item in data:
    s = str(item)
    if 'pickup' in s.lower():
        print(item)
