"""Disposable Frida fixture target, with CWD/TEMP/TMP/output exclusively on S."""
from workflow import ROOT,OUT,SCRATCH,dump
import ctypes,os,sys,time
os.chdir(SCRATCH)
tag=sys.argv[1]
assert tag.replace('_','').isalnum()
dump(OUT/(tag+'_READY.json'),dict(pid=os.getpid(),cwd=os.getcwd(),temp=os.environ['TEMP'],tmp=os.environ['TMP'],kex=False))
deadline=time.monotonic()+120
while time.monotonic()<deadline:
    if (OUT/(tag+'_DONE')).exists():break
    time.sleep(.05)
dump(OUT/(tag+'_EXIT.json'),dict(pid=os.getpid(),exit_code=0,kex=False))
