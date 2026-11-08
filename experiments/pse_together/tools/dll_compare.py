"""Compare a pse_together DLL with its frozen lab counterpart modulo build identity.

    python -X utf8 -B dll_compare.py <frozen.dll> <unified.dll> [--json OUT]

Build identity that legitimately differs between two builds of the same source (PE TimeDateStamp, checksum,
export-directory timestamp, the IMAGE_DEBUG_DIRECTORY and the CodeView record with PDB GUID/path, and the
'A0x<hash>' anonymous-namespace name suffix derived from the source path) is masked; every
other byte of every section is hashed. Equal masked section hashes mean identical machine code, data, imports and
exports. This is a static build-equivalence check, NOT runtime evidence and NOT a replacement for the frozen hash."""
import hashlib, json, re, struct, sys
from pathlib import Path

def parse(path):
    d = bytearray(Path(path).read_bytes())
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[pe:pe + 4] == b'PE\0\0'
    nsec, = struct.unpack_from('<H', d, pe + 6); osz, = struct.unpack_from('<H', d, pe + 20)
    opt = pe + 24; magic, = struct.unpack_from('<H', d, opt); assert magic == 0x20b
    mask = [(pe + 8, 4), (opt + 64, 4)]                    # TimeDateStamp, CheckSum
    dd = opt + 112
    secs = []
    sp = opt + osz
    for i in range(nsec):
        name = bytes(d[sp:sp + 8]).rstrip(b'\0').decode()
        vsz, va, rsz, rptr = struct.unpack_from('<IIII', d, sp + 8)
        secs.append(dict(name=name, va=va, vsize=vsz, rptr=rptr, rsize=rsz)); sp += 40
    def r2o(rva):
        for s in secs:
            if s['va'] <= rva < s['va'] + max(s['vsize'], s['rsize']): return rva - s['va'] + s['rptr']
        return None
    exp_rva, exp_sz = struct.unpack_from('<II', d, dd)
    if exp_rva:
        o = r2o(exp_rva); mask.append((o + 4, 4))
    dbg_rva, dbg_sz = struct.unpack_from('<II', d, dd + 6 * 8)
    if dbg_rva:
        o = r2o(dbg_rva); mask.append((o, dbg_sz))
        for k in range(dbg_sz // 28):
            size, _, ptr = struct.unpack_from('<III', d, o + k * 28 + 16)
            mask.append((ptr, size))
    for off, n in mask: d[off:off + n] = b'\0' * n
    # MSVC names anonymous-namespace symbols '?A0x<hash of the translation-unit path>'; the hash is build identity, not code.
    anon = [m.start() + 3 for m in re.finditer(rb'A0x[0-9a-f]{8}', bytes(d))]
    for o in anon: d[o:o + 8] = b'0' * 8
    exports = []
    if exp_rva:
        o = r2o(exp_rva); nn, = struct.unpack_from('<I', d, o + 24); names, = struct.unpack_from('<I', d, o + 32)
        no = r2o(names)
        for k in range(nn):
            rv, = struct.unpack_from('<I', d, no + 4 * k); so = r2o(rv); e = d.index(b'\0', so); exports.append(bytes(d[so:e]).decode())
    for s in secs: s['sha256'] = hashlib.sha256(bytes(d[s['rptr']:s['rptr'] + s['rsize']])).hexdigest()
    return dict(anon_sites=len(anon), path=str(path), raw_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(), sections=secs, exports=sorted(exports),
                masked=[dict(offset=o, size=n) for o, n in mask])

def compare(a, b):
    A, B = parse(a), parse(b)
    sec = []
    for sa in A['sections']:
        sb = next((s for s in B['sections'] if s['name'] == sa['name']), None)
        sec.append(dict(name=sa['name'], size_a=sa['rsize'], size_b=sb and sb['rsize'], equal=bool(sb and sb['sha256'] == sa['sha256'])))
    equal = [s['name'] for s in sec if s['equal']]
    return dict(frozen=A['path'], unified=B['path'], frozen_raw_sha256=A['raw_sha256'], unified_raw_sha256=B['raw_sha256'],
                raw_identical=A['raw_sha256'] == B['raw_sha256'], sections=sec,
                section_names_equal=[s['name'] for s in A['sections']] == [s['name'] for s in B['sections']],
                exports_equal=A['exports'] == B['exports'], exports=A['exports'],
                all_sections_equal_masked=all(s['equal'] for s in sec) and len(A['sections']) == len(B['sections']),
                equal_sections=equal, anon_ns_hash_sites_masked=A['anon_sites'], differing_sections=[s['name'] for s in sec if not s['equal']])

if __name__ == '__main__':
    r = compare(sys.argv[1], sys.argv[2])
    if '--json' in sys.argv: Path(sys.argv[sys.argv.index('--json') + 1]).write_text(json.dumps(r, indent=2), encoding='utf8')
    print(json.dumps({k: r[k] for k in ('raw_identical', 'all_sections_equal_masked', 'equal_sections', 'differing_sections', 'exports_equal')}))
