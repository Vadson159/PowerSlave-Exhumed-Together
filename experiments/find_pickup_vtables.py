import struct

# Let's search in PE file for pointers to RTTI complete object locators of these pickups
# PE path:
pe_path = r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe'
with open(pe_path, 'rb') as f:
    buf = f.read()

# image base = 0x140000000
# let's find the RTTI type descriptor RVAs
type_rvas = {
    'Pickup': 0xa51900,
    'Weapon': 0xa518a0,
    'Health': 0xa51840,
    'MaxHealth': 0xa51870,
    'Ammo': 0xa518d0,
    'Power': 0xa51758,
    'Quest': 0xa51788,
    'TeamDoll': 0xa517b8,
    'Key': 0xa517e8,
    'Artifact': 0xa51810,
    'Map': 0xa51730
}

# In x64 MSVC RTTI, CompleteObjectLocator has:
# uint32 signature (1 for 64-bit)
# uint32 offset
# uint32 cdOffset
# int32 rvaTypeDescriptor
# int32 rvaClassDescriptor
# int32 rvaSelf

for name, trva in type_rvas.items():
    pattern = struct.pack('<IIIi', 1, 0, 0, trva)
    idx = 0
    while True:
        pos = buf.find(pattern, idx)
        if pos == -1:
            break
        # pos in file -> VA
        # In this PE, image base is 0x140000000
        # Let's see what section pos is in
        # We can find vtables by searching for pointers to this COL (pos RVA + image base)
        col_rva = pos # roughly, need exact section mapping
        print(f"COL for {name} found at file offset 0x{pos:x}")
        idx = pos + 1
