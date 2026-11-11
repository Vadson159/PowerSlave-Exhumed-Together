import sys, struct, pefile
EXE = r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\play_vote\game\anubis_Shipping_Steam_x64.exe'
pe = pefile.PE(EXE, fast_load=True)
d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
img = pe.get_memory_mapped_image()
ents = [struct.unpack_from('<III', img, d.VirtualAddress + i * 12) for i in range(d.Size // 12)]
def unwind_parent(e):
    # chained unwind info -> parent function start
    b, end, u = e
    flags = img[u] >> 3
    if flags & 4:
        cnt = img[u + 2]
        off = u + 4 + ((cnt + 1) & ~1) * 2
        return struct.unpack_from('<III', img, off)
    return None
for a in sys.argv[1:]:
    x = int(a, 16)
    for e in ents:
        if e[0] <= x < e[1]:
            p = e; chain = []
            while True:
                q = unwind_parent(p)
                if not q: break
                chain.append(q); p = q
            print(a, 'range', hex(e[0]), hex(e[1]), 'root', hex(p[0]), hex(p[1]))
