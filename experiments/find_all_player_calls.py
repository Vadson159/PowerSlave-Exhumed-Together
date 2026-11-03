import os, glob

script_files = glob.glob(r'PowerSlave_Exhumed_Together_mod\experiments\kpf_extracted\scripts\**\*.txt', recursive=True)
print(f"Total script files: {len(script_files)}")

results = []
for sf in script_files:
    rel = os.path.relpath(sf, r'PowerSlave_Exhumed_Together_mod\experiments\kpf_extracted')
    with open(sf, 'r', encoding='utf-8') as f:
        for line_no, line in enumerate(f, 1):
            if 'Player()' in line:
                results.append((rel, line_no, line.strip()))

print(f"Total Player() calls found: {len(results)}")
for rel, line_no, text in results:
    print(f"{rel}:{line_no}: {text}")
