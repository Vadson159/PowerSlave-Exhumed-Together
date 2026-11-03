import re

with open(r'PowerSlave_Exhumed_Together_mod\experiments\kpf_extracted\scripts\p_enemy.txt', 'r', encoding='utf-8') as f:
    lines = f.readlines()

print(f"Total lines in p_enemy.txt: {len(lines)}")

# Find all occurrences of Player(), GetTarget, InflictDamage, SourceOwner, etc.
for i, line in enumerate(lines, 1):
    if any(k in line for k in ['Player()', 'GetTarget', 'InflictDamage', 'GetSourceOwner', 'SetTarget']):
        print(f"{i:4d}: {line.strip()}")
