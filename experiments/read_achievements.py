import json

with open('PowerSlave_Exhumed_Together_mod/docs/evidence/E018_ACHIEVEMENT_DEFINITIONS.json', 'r', encoding='utf-8') as f:
    d = json.load(f)

defn = d.get('definition', {})
print(f"Total keys in definition: {len(defn)}")
for k, v in defn.items():
    print(f"{k}: {v}")
