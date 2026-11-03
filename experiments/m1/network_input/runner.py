"""Reuse the accepted isolated launcher/OS guard, selecting the combined DLL.
No KEX functions are called from this diagnostic Python/JS harness.
"""
from pathlib import Path
import importlib.util, sys, shutil, json
LAB=Path(__file__).resolve().parent
NATIVE=LAB.parent/'native_bootstrap'
sys.path.insert(0,str(NATIVE))
spec=importlib.util.spec_from_file_location('m11_runner',NATIVE/'runner.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
m.LAB=LAB
m.IMPLEMENTATION=m.OUT/'M12A_IMPLEMENTATION_20261102'
m.prepare.PROFILE=LAB/'profile'

if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='preflight': m.preflight()
    elif cmd=='launch': m.launch(sys.argv[2])
    elif cmd=='bind':
        name=sys.argv[2]
        m.bind(name)
        run,_=m.owned_run(name)
        shared=run/'native_artifact/shared_native'
        shutil.copytree(NATIVE/'src',shared/'src')
        shutil.copytree(NATIVE/'third_party',shared/'third_party')
        manifest=[dict(path=str(p.relative_to(m.ROOT)),sha256=m.digest(p)) for p in sorted(NATIVE.rglob('*')) if p.is_file() and not {'build','profile','__pycache__'}.intersection(p.parts)]
        m.dump(run/'SHARED_NATIVE_FILES.json',manifest)
    elif cmd=='gate': m.gate(sys.argv[2])
    elif cmd=='action': m.action(sys.argv[2],sys.argv[3],sys.argv[4:])
    else: raise ValueError('Unknown action')
