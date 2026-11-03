import json, os

dossier_dir = r'PowerSlave_Exhumed_Together_mod\docs\evidence\ghidra_2022'

def inspect_func(va, max_lines=150):
    fname = os.path.join(dossier_dir, f'G_FUNC_{va}.json')
    if not os.path.exists(fname):
        print(f"File not found: {fname}")
        return
    with open(fname, 'r', encoding='utf-8') as f:
        data = json.load(f)
    print(f"=== {va} ===")
    print("Function info:", data.get('function'))
    decomp = data.get('pseudocode_non_source', '')
    lines = decomp.splitlines()
    print(f"Decompiled code lines: {len(lines)}")
    for i, line in enumerate(lines[:max_lines]):
        print(f"{i+1:3d}: {line}")
    if len(lines) > max_lines:
        print(f"... [truncated, total {len(lines)} lines]")

inspect_func('1401971e0', 100) # ChaseTarget
