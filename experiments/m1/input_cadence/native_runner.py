"""Accepted isolated native launcher selecting DOC49's fresh DLL/profile."""
from pathlib import Path
import importlib.util,shutil,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];NATIVE=LAB.parent/'native_bootstrap'
sys.path.insert(0,str(NATIVE))
spec=importlib.util.spec_from_file_location('m11_runner49',NATIVE/'runner.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.IMPLEMENTATION=m.OUT/'M12B_CADENCE_49';m.prepare.PROFILE=LAB/'profile'
if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='preflight':m.preflight()
    elif cmd=='launch':
        assert sys.argv[2]=='M12B_CADENCE_NATIVE_01' and not (m.OUT/sys.argv[2]).exists();m.launch(sys.argv[2])
    elif cmd=='bind':
        name=sys.argv[2];m.bind(name);run,_=m.owned_run(name)
        for folder in ['native_bootstrap/src','native_bootstrap/third_party','network_input/src']:
            shutil.copytree(LAB.parent/folder,run/'native_artifact/shared'/folder)
    elif cmd=='gate':m.gate(sys.argv[2])
    elif cmd=='action':m.action(sys.argv[2],sys.argv[3],sys.argv[4:])
    else:raise ValueError(cmd)
