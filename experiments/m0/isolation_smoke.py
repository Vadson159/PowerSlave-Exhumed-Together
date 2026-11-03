"""Disposable non-game process for exercising the native write-open guard.

Run suspended under Frida with isolation_guard.js installed before resume. The outside
test uses a confirmed nonexistent parent: a broken guard still cannot create that file.
No source game/profile/Steam data is opened for writing. Python must use -B.
"""
from pathlib import Path
from ctypes import wintypes as W
import ctypes, datetime, json, sys

def main():
    root=Path(__file__).resolve().parents[2]
    out=Path(sys.argv[1]).resolve()
    if not out.is_relative_to(root) or not out.is_dir():raise ValueError('Owned output required')
    inside=out/'guard_inside.tmp'
    outside=Path('C:/__m0_guard_denied_20261101_absent_parent/probe.tmp')
    if outside.parent.exists():raise RuntimeError('Outside smoke parent must not exist')
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,ctypes.c_void_p,W.DWORD,W.DWORD,W.HANDLE]
    k.CreateFileW.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    invalid=ctypes.c_void_p(-1).value
    rows=[]
    for name,path,disposition in [('inside_create',inside,1),('outside_create',outside,1),('outside_read',outside,3)]:
        access=0x80000000 if name=='outside_read' else 0x40000000
        ctypes.set_last_error(0)
        handle=k.CreateFileW(str(path),access,7,None,disposition,0x80,None)
        error=ctypes.get_last_error()
        if handle!=invalid:k.CloseHandle(handle)
        passed=(handle!=invalid) if name=='inside_create' else (handle==invalid and error==(3 if name=='outside_read' else 5))
        rows.append({'test':name,'path':str(path),'valid_handle':handle!=invalid,'winerror':error,'passed':passed})
    result={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'PASS' if all(r['passed'] for r in rows) else 'FAIL',
      'tests':rows,'outside_parent_absent':not outside.parent.exists(),'note':'Guard smoke only, no gameplay/native lifecycle acceptance'}
    (out/'SMOKE_RESULT.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    raise SystemExit(result['status']!='PASS')

if __name__=='__main__':main()
