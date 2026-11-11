"""dis.py <rva-hex> [count]   - disassemble at RVA;   dis.py find <regex> [window-regex]  - linear sweep of .text"""
import sys, re, pefile, capstone, pickle, os
EXE = r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\play_vote\game\anubis_Shipping_Steam_x64.exe'
pe = pefile.PE(EXE, fast_load=True)
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
T0, T1 = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
CACHE = os.path.join(os.environ.get('TEMP', '.'), 'pse_text_ins.pkl')


def sweep():
    if os.path.exists(CACHE): return pickle.load(open(CACHE, 'rb'))
    out = []
    a = T0
    while a < T1:
        got = False
        for i in md.disasm(img[a:min(a + 0x10000, T1)], a):
            out.append((i.address, i.size, i.mnemonic, i.op_str)); a = i.address + i.size; got = True
        if not got: a += 1
        else:
            pass
    pickle.dump(out, open(CACHE, 'wb'))
    return out


if sys.argv[1] == 'find':
    pat = re.compile(sys.argv[2]); win = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
    ins = sweep()
    for k, (a, s, m, o) in enumerate(ins):
        if pat.search(m + ' ' + o):
            if win:
                ctx = ins[max(0, k - 25):k]
                if not any(win.search(x[2] + ' ' + x[3]) for x in ctx): continue
            print(hex(a), m, o)
else:
    a = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    for i in md.disasm(img[a:a + n * 15], a):
        print(hex(i.address), i.mnemonic, i.op_str); n -= 1
        if n <= 0: break
