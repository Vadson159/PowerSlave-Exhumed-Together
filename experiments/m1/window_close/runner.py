"""DOC-48 uses the accepted launcher/guard/loader with a fresh build/profile."""
from pathlib import Path
import importlib.util, shutil, sys
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
NATIVE=LAB.parent/'native_bootstrap'
sys.path.insert(0,str(NATIVE))
spec=importlib.util.spec_from_file_location('m11_runner',NATIVE/'runner.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
m.LAB=LAB
m.IMPLEMENTATION=m.OUT/'M12B_WINDOW_CLOSE_48'
m.prepare.PROFILE=LAB/'profile'
if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='preflight': m.preflight()
    elif cmd=='launch':
        # Exactly one game launch is authorized. No retry even if preflight later fails.
        assert sys.argv[2]=='M12B_WINDOW_CLOSE_01'
        assert not (m.OUT/sys.argv[2]).exists()
        m.launch(sys.argv[2])
    elif cmd=='bind':
        name=sys.argv[2];m.bind(name)
        run,_=m.owned_run(name)
        for folder in ['native_bootstrap/src','native_bootstrap/third_party','network_input/src']:
            shutil.copytree(LAB.parent/folder,run/'native_artifact/shared'/folder)
    elif cmd=='gate': m.gate(sys.argv[2])
    elif cmd=='action': m.action(sys.argv[2],sys.argv[3],sys.argv[4:])
    else: raise ValueError(cmd)
