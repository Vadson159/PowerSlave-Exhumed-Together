"""Launch only the verified workspace test copy, suspended for early read-only instrumentation.
No DLL injection is performed by this launcher. Resume only after the observer is loaded.
"""
from pathlib import Path
import argparse, ctypes, datetime, json, os, subprocess, sys
from prepare import ROOT,LAB,COPY,PROFILE,OUT,EXPECTED,digest,snapshot

def main():
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--quiet',action='store_true',help='Write reports only; suitable for REA spawn stdout ownership');a=p.parse_args()
    if not a.run.replace('_','').replace('-','').isalnum():raise ValueError('Invalid run name')
    run=OUT/a.run;run.mkdir(parents=True,exist_ok=False)
    exe=COPY/'anubis_Shipping_Steam_x64.exe'
    if not exe.resolve().is_relative_to(ROOT) or digest(exe)!=EXPECTED:raise RuntimeError('Invalid target copy/build')
    protected=[Path(r'C:\Users\vadso\Saved Games\Nightdive Studios\Powerslave EX')]
    prep=json.loads((OUT/'M0_001_PREPARATION.json').read_text())
    protected += [Path(r['path']) for r in prep['protected_profiles_before'] if r['exists']]
    before=[snapshot(p) for p in dict.fromkeys(protected)]
    profile=PROFILE/a.run;profile.mkdir(parents=True,exist_ok=False)
    # Relative native saves/config must not be shared by independent runs (C09 menu witness).
    # Steam's documented development AppID file must also be present in the process CWD.
    (profile/'steam_appid.txt').write_text('1678430\n',encoding='ascii')
    for d in ['roaming','local','temp','user','early_saved_games','early_native_logs','gpu_cache','steam_overlay',
              'gpu_cache/steam','gpu_cache/steam/AMDv1','gpu_cache/steam/fozpipelinesv6']:(profile/d).mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(APPDATA=str(profile/'roaming'),LOCALAPPDATA=str(profile/'local'),
        TEMP=str(profile/'temp'),TMP=str(profile/'temp'),USERPROFILE=str(profile/'user'))
    # Exact disable_environment keys from installed Valve Vulkan-layer manifests. These optional
    # graphics layers are distinct from Steamworks/achievements; no game or Steam binaries patched.
    # Added after C05 outside-workspace cache/log evidence; needs fresh runtime isolation acceptance.
    env.update(DISABLE_VK_LAYER_VALVE_steam_fossilize_1='1',DISABLE_VK_LAYER_VALVE_steam_overlay_1='1')
    argv=[str(exe),'-basepath',str(COPY),'-userpath',str(profile),'-window','-width','960','-height','540','-skipmovies','-stdout']
    (run/'protected_before.json').write_text(json.dumps(before,indent=2),encoding='utf8')
    with (run/'stdout.log').open('wb') as out,(run/'stderr.log').open('wb') as err:
        # REA-spawned parent may have no valid inherited STD_INPUT_HANDLE. Gameplay uses Win32/SDL,
        # not stdin; an explicit NUL input handle keeps both launch providers reproducible.
        proc=subprocess.Popen(argv,cwd=profile,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=0x00000004)
    from ctypes import wintypes as W
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    class TE(ctypes.Structure):
        _fields_=[('size',W.DWORD),('usage',W.DWORD),('tid',W.DWORD),('pid',W.DWORD),('base',W.LONG),('delta',W.LONG),('flags',W.DWORD)]
    k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
    k.Thread32First.argtypes=[W.HANDLE,ctypes.POINTER(TE)];k.Thread32Next.argtypes=k.Thread32First.argtypes
    k.CloseHandle.argtypes=[W.HANDLE]
    snap=k.CreateToolhelp32Snapshot(4,0);te=TE();te.size=ctypes.sizeof(te);threads=[]
    if k.Thread32First(snap,ctypes.byref(te)):
        while True:
            if te.pid==proc.pid:threads.append(te.tid)
            if not k.Thread32Next(snap,ctypes.byref(te)):break
    k.CloseHandle(snap)
    if len(threads)!=1:
        raise RuntimeError('Cannot unambiguously identify suspended primary thread: '+str(threads))
    report={'run':a.run,'pid':proc.pid,'target':str(exe),'binary_sha256':EXPECTED,'argv':argv,'cwd':str(profile),
       'profile':str(profile),'creationflags':'CREATE_SUSPENDED','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
       'primary_thread_id':threads[0],'state':'SUSPENDED, observer not yet loaded',
       'graphics_layer_disable_environment':{key:env[key] for key in ['DISABLE_VK_LAYER_VALVE_steam_fossilize_1','DISABLE_VK_LAYER_VALVE_steam_overlay_1']},
       'note':'No network or achievement implementation; test copy only'}
    (run/'launch.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    if not a.quiet:print(json.dumps(report),flush=True)
    code=proc.wait()
    (run/'exit.json').write_text(json.dumps({'exit_code':code,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}),encoding='utf8')
    (run/'protected_after.json').write_text(json.dumps([snapshot(p) for p in dict.fromkeys(protected)],indent=2),encoding='utf8')
    if not a.quiet:print(json.dumps({'pid':proc.pid,'exit_code':code}),flush=True)

if __name__=='__main__':main()
