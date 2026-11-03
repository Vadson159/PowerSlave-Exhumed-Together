"""Read-only local PE/KPF inventory. Only research output under this mod is written.
This is an analyst parser, not REA output. No DLL/EXE is loaded or executed.
"""
from pathlib import Path
import datetime as dt, hashlib, json, re, struct, zipfile

ROOT=Path(__file__).resolve().parents[2]
GAME=Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed')
REF=ROOT.parent
OUT=ROOT/'docs/evidence'
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def stamp(t):return dt.datetime.fromtimestamp(t,dt.timezone.utc).isoformat()
def write(name,data):
    p=OUT/name
    if not p.resolve().is_relative_to(ROOT):raise ValueError('output outside mod')
    p.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'written':name,'bytes':p.stat().st_size}))

class PE:
    def __init__(self,path):
        self.path=Path(path);self.b=self.path.read_bytes();b=self.b
        if b[:2]!=b'MZ':raise ValueError('MZ missing')
        pe=self.u32(0x3c)
        if b[pe:pe+4]!=b'PE\0\0':raise ValueError('PE missing')
        self.machine,self.nsects,self.ts,_,_,opsize,self.flags=struct.unpack_from('<HHIIIHH',b,pe+4)
        self.op=op=pe+24;self.magic=self.u16(op);self.is64=self.magic==0x20b
        if self.magic not in (0x20b,0x10b):raise ValueError('Unknown optional header')
        self.base=self.u64(op+24) if self.is64 else self.u32(op+28)
        self.entry=self.u32(op+16);self.imgsize=self.u32(op+56);self.hdrsize=self.u32(op+60)
        dirop=op+(112 if self.is64 else 96);ndirs=self.u32(op+(108 if self.is64 else 92))
        self.dirs=[struct.unpack_from('<II',b,dirop+8*i) for i in range(min(ndirs,16))]
        self.sections=[]
        for i in range(self.nsects):
            o=op+opsize+40*i;name=b[o:o+8].rstrip(b'\0').decode('ascii',errors='replace')
            vs,rv,rs,rp=struct.unpack_from('<IIII',b,o+8);fl=self.u32(o+36)
            self.sections.append(dict(name=name,virtual_size=vs,rva=rv,raw_size=rs,raw_pointer=rp,flags=hex(fl)))
    def u16(self,o):return struct.unpack_from('<H',self.b,o)[0]
    def u32(self,o):return struct.unpack_from('<I',self.b,o)[0]
    def u64(self,o):return struct.unpack_from('<Q',self.b,o)[0]
    def off(self,rva):
        if 0<=rva<self.hdrsize:return rva
        for s in self.sections:
            delta=rva-s['rva']
            if 0<=delta<s['raw_size'] and s['raw_pointer']+delta<len(self.b):return s['raw_pointer']+delta
        raise ValueError(f'RVA not file-backed: {rva:#x}')
    def loc(self,o):
        if o<self.hdrsize:r=o
        else:
            s=next((s for s in self.sections if s['raw_pointer']<=o<s['raw_pointer']+s['raw_size']),None)
            if not s:return {'file_offset':hex(o),'rva':None,'preferred_va':None}
            r=s['rva']+o-s['raw_pointer']
        return {'file_offset':hex(o),'rva':hex(r),'preferred_va':hex(self.base+r)}
    def z(self,o):return self.b[o:self.b.index(b'\0',o)].decode('ascii',errors='replace')
    def directory(self,i):return self.dirs[i] if i<len(self.dirs) else (0,0)
    def imports(self):
        rv,size=self.directory(1);out=[]
        if not rv:return out
        off=self.off(rv)
        for i in range(10000):
            o=off+20*i;ilt,ts,fw,nr,iat=struct.unpack_from('<IIIII',self.b,o)
            if not any((ilt,ts,fw,nr,iat)):break
            dll=self.z(self.off(nr));tab=self.off(ilt or iat);funcs=[];step=8 if self.is64 else 4
            for j in range(50000):
                val=self.u64(tab+j*step) if self.is64 else self.u32(tab+j*step)
                if not val:break
                f={'iat_rva':hex(iat+j*step),'iat_preferred_va':hex(self.base+iat+j*step)}
                if val & (1 << (step*8-1)):f['ordinal']=val&0xffff
                else:
                    no=self.off(val);f.update(name=self.z(no+2),hint=self.u16(no))
                funcs.append(f)
            out.append({'dll':dll,'symbols':funcs})
        return out
    def exports(self):
        rv,size=self.directory(0)
        if not rv:return []
        o=self.off(rv);ordinalbase,nf,nn,af,an,ao=struct.unpack_from('<IIIIII',self.b,o+16)
        names={}
        for i in range(nn):names[self.u16(self.off(ao)+i*2)]=self.z(self.off(self.u32(self.off(an)+i*4)))
        out=[]
        for i in range(nf):
            r=self.u32(self.off(af)+i*4)
            if not r:continue
            out.append({'ordinal':ordinalbase+i,'name':names.get(i),'rva':hex(r),'preferred_va':hex(self.base+r),
                        'forwarder':self.z(self.off(r)) if rv<=r<rv+size else None})
        return out
    def debug(self):
        rv,size=self.directory(6);out=[]
        if not rv:return out
        o=self.off(rv)
        for i in range(size//28):
            p=o+28*i;typ=self.u32(p+12);n=self.u32(p+16);fp=self.u32(p+24)
            r={'type':typ,'size':n,'file_offset':hex(fp)}
            if typ==2 and self.b[fp:fp+4]==b'RSDS':r.update(rsds_guid_bytes=self.b[fp+4:fp+20].hex(),age=self.u32(fp+20),pdb_path=self.z(fp+24))
            out.append(r)
        return out
    def pdata(self):
        rv,size=self.directory(3)
        if not rv or self.machine!=0x8664:return []
        o=self.off(rv)
        return [{'begin_rva':hex(a),'end_rva_exclusive':hex(b),'unwind_rva':hex(c)} for a,b,c in struct.iter_unpack('<III',self.b[o:o+size])]
    def metadata(self):
        st=self.path.stat();imp=self.imports();exp=self.exports();dllchars=self.u16(self.op+70)
        return {'path':str(self.path),'size':st.st_size,'sha256':sha(self.path),'last_write_utc':stamp(st.st_mtime),
                'creation_time_utc_not_release_date':stamp(st.st_ctime),'machine':hex(self.machine),
                'architecture':{0x8664:'x86_64',0x14c:'x86',0xaa64:'arm64'}.get(self.machine,'unknown'),
                'optional_header_magic':hex(self.magic),'preferred_image_base':hex(self.base),
                'entry_rva':hex(self.entry),'entry_preferred_va':hex(self.base+self.entry),'entry_file_offset':hex(self.off(self.entry)),
                'image_size':hex(self.imgsize),'coff_timestamp_raw':self.ts,
                'coff_timestamp_warning':'Not an authenticated build/release date; reproducible builds may encode other values',
                'dll_characteristics':hex(dllchars),'aslr_dynamic_base_flag':bool(dllchars&0x40),
                'high_entropy_va_flag':bool(dllchars&0x20),'nx_compatible_flag':bool(dllchars&0x100),
                'sections':self.sections,'data_directories':[{'index':i,'rva_or_security_file_offset':hex(r),'size':s} for i,(r,s) in enumerate(self.dirs)],
                'debug':self.debug(),'imports':imp,'exports':exp,
                'limitations':['Parser decodes standard import table; delay imports are not decoded','Import presence proves static dependency, not observed call or subsystem behavior','No target execution; version strings acquired separately using Windows FileVersionInfo']}

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    binaries=sorted(GAME.rglob('*.exe'))+sorted(GAME.rglob('*.dll'))+[REF/'PowerSlave_EX_-_CO-OP_Edition/anubis.exe']
    manifests=[]
    for path in binaries:
        pe=PE(path);m=pe.metadata();manifests.append(m)
        tag='main2022' if path.name=='anubis_Shipping_Steam_x64.exe' else 'coop2015' if path.name=='anubis.exe' else path.stem
        write('PE_'+tag+'.json',m)
        if tag not in ('main2022','coop2015'):continue
        strings=[]
        for match in re.finditer(rb'[\x20-\x7e]{4,}',pe.b):
            strings.append(dict(encoding='ascii',value=match.group().decode('ascii'),**pe.loc(match.start())))
        for match in re.finditer(rb'(?:[\x20-\x7e]\x00){6,}',pe.b):
            strings.append(dict(encoding='utf16le-ascii-subset',value=match.group().decode('utf-16le'),**pe.loc(match.start())))
        write('STRINGS_'+tag+'.json',{'binary_sha256':m['sha256'],'method':'ASCII printable runs >=4; UTF16LE ASCII subset >=6; not exhaustive Unicode','count':len(strings),'strings':strings})
        write('PDATA_'+tag+'.json',{'binary_sha256':m['sha256'],'records':pe.pdata(),'limitations':['Unwind function ranges are not semantic identities; chained entries possible']})
    write('E005_BINARY_MANIFEST.json',{'captured_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'method':'inventory.py standard library PE parser','binaries':manifests})
    inv=[]
    for p in sorted(GAME.rglob('*')):
        if p.is_file():inv.append({'relative_path':str(p.relative_to(GAME)),'size':p.stat().st_size,'last_write_utc':stamp(p.stat().st_mtime)})
    write('E006_GAME_FILES.json',{'root':str(GAME),'files':inv,'count':len(inv)})
    kpf=GAME/'PowerslaveEX.kpf'
    with zipfile.ZipFile(kpf) as z:
        members=[{'name':m.filename,'size':m.file_size,'compressed_size':m.compress_size,'crc32':hex(m.CRC),'compression':m.compress_type,'flags':m.flag_bits,'local_header_offset':m.header_offset} for m in z.infolist()]
    write('E007_KPF_INVENTORY.json',{'path':str(kpf),'sha256':sha(kpf),'size':kpf.stat().st_size,'format':'ZIP per Python zipfile central-directory parse','member_count':len(members),'members':members,'limitations':['CRC values read from central directory, not all payload CRCs checked','No extraction, target changes or code execution']})

if __name__=='__main__':main()
