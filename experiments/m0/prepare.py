"""Create and verify an ordinary independent game copy; never hardlinks or writes source files."""
from pathlib import Path
import datetime, hashlib, json, os, re, shutil

ROOT=Path(__file__).resolve().parents[2]
LAB=Path(__file__).resolve().parent
SOURCE=Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed')
COPY=LAB/'game'
PROFILE=LAB/'profile'
OUT=ROOT/'docs/evidence/m0'
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def snapshot(root):
    if not root.exists():return {'path':str(root),'exists':False,'files':[]}
    return {'path':str(root),'exists':True,'files':[{'relative_path':str(p.relative_to(root)),
       'size':p.stat().st_size,'sha256':digest(p)} for p in sorted(root.rglob('*')) if p.is_file()]}
def main():
    for p in (COPY,PROFILE,OUT,LAB/'profile/appdata',LAB/'profile/localappdata',LAB/'profile/temp'):
        if not p.resolve().is_relative_to(ROOT):raise RuntimeError('Outside workspace')
        p.mkdir(parents=True,exist_ok=True)
    exe=SOURCE/'anubis_Shipping_Steam_x64.exe'
    if digest(exe)!=EXPECTED:raise RuntimeError('Source executable build changed')
    existing=OUT/'M0_001_PREPARATION.json'
    if existing.exists():
        print('Existing preparation manifest retained; verify before reuse')
        return
    before=snapshot(SOURCE)
    copy_records=[]
    for src in sorted(SOURCE.rglob('*')):
        if src.is_symlink():raise RuntimeError('Source symlink not admitted: '+str(src))
        if not src.is_file():continue
        dest=COPY/src.relative_to(SOURCE);dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():
            if digest(src)!=digest(dest):raise RuntimeError('Existing differing copy: '+str(dest))
        else:shutil.copy2(src,dest)
        if os.path.samefile(src,dest):raise RuntimeError('Copy aliases source')
        h=digest(dest)
        if h!=digest(src):raise RuntimeError('Copy verification failed')
        copy_records.append({'relative_path':str(src.relative_to(SOURCE)),'sha256':h,'size':dest.stat().st_size})
    appmanifest=SOURCE.parent.parent/'appmanifest_1678430.acf'
    text=appmanifest.read_text(encoding='utf8')
    appid=re.search(r'"appid"\s+"(\d+)"',text).group(1)
    if appid!='1678430':raise RuntimeError('App identity mismatch')
    # Documented development launch mechanism; prevents Steam restart routing to installed executable.
    (COPY/'steam_appid.txt').write_text(appid+'\n',encoding='ascii')
    protected=[Path(r'C:\Users\vadso\Saved Games\Nightdive Studios\Powerslave EX')]
    userdata=Path(r'C:\Program Files (x86)\Steam\userdata')
    if userdata.exists():protected += [p/'1678430' for p in userdata.iterdir() if p.is_dir() and (p/'1678430').exists()]
    report={'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'M0 test environment preparation; no game launched',
       'source':str(SOURCE),'copy':str(COPY),'profile':str(PROFILE),'binary_sha256':EXPECTED,'appid':appid,
       'buildid':re.search(r'"buildid"\s+"(\d+)"',text).group(1),'independent_copy_files':copy_records,
       'original_installation_before':before,'protected_profiles_before':[snapshot(p) for p in protected],
       'note':'No original profile copied into test profile; no networking or achievement code modifications'}
    existing.write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps({'copied_files':len(copy_records),'copied_bytes':sum(r['size'] for r in copy_records),'profile':str(PROFILE),'protected_profile_roots':len(protected)}))

if __name__=='__main__':main()
