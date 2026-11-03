import struct

exe = r"s:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\experiments\m0\game\anubis_Shipping_Steam_x64.exe"
with open(exe, "rb") as f:
    data = f.read()

# Let's search for 4c 8d 05 c8 48 36 00
sig = bytes.fromhex("4c8d05c8483600")
pos = data.find(sig)
print(f"Signature found at file offset: 0x{pos:x}")
# file offset to RVA: pos - 0x400 + 0x1000
rva = pos - 0x400 + 0x1000
print(f"RVA: 0x{rva:x}")

def get_str(target_rva):
    target_off = target_rva - 0x482000 + 0x480600
    end = data.find(b"\x00", target_off)
    return data[target_off:end].decode("ascii", errors="ignore")

curr = pos
for i in range(13):
    instr = data[curr:curr+14]
    disp = struct.unpack("<i", instr[3:7])[0]
    curr_rva = curr - 0x400 + 0x1000
    target_rva = curr_rva + 7 + disp
    str_val = get_str(target_rva)
    action_idx = instr[8] if instr[7] == 0xb2 else 0
    held_off = 4 + 4 * action_idx
    print(f"Action {action_idx:2d} (bit {action_idx:2d}): \"{str_val}\" | held offset {held_off:2d} (0x{held_off:02x})")
    curr += 14
