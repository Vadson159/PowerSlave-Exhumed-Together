import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

def print_func(va):
    fname = os.path.join(dossier_dir, f'G_FUNC_{va}.json')
    if not os.path.exists(fname):
        print(f"File not found: {fname}")
        return
    with open(fname, 'r', encoding='utf-8') as f:
        data = json.load(f)
    print(f"=== {va} ===")
    decomp = data.get('pseudocode_non_source', '')
    for i, line in enumerate(decomp.splitlines()[:60], 1):
        print(f"{i:3d}: {line}")

print_func('1401d8ac0') # Artifact pickup
print_func('1401d8870') # Ankh pickup
