import os, glob

map_scripts = glob.glob(r'PowerSlave_Exhumed_Together_mod\experiments\kpf_extracted\scripts\map\*_script.txt')
print(f"Found {len(map_scripts)} map scripts:")
for s in map_scripts:
    print(f"\n==================== {os.path.basename(s)} ====================")
    with open(s, 'r', encoding='utf-8') as f:
        content = f.read()
    print(content)
