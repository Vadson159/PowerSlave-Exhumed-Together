"""Retain and duplicate the same process object before native work through exit."""
import importlib.util,json,sys
from workflow import LAB,ROOT,OUT
spec=importlib.util.spec_from_file_location('watch53f_base',LAB.parent/'background_host/watch.py')
m=importlib.util.module_from_spec(spec)
sys.path.insert(0,str(LAB.parent/'background_host'));spec.loader.exec_module(m)
# The accepted watcher selects a run from role; supply a tiny path adapter instead
# of editing its historical source.
role,name=sys.argv[1:3]
source=(LAB.parent/'background_host/watch.py').read_text(encoding='utf8')
source=source.replace("from audit import ROOT, LAB, OUT", "from workflow import ROOT, LAB, OUT")
source=source.replace("run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')","run=ROOT/'docs/evidence/m1'/"+repr(name))
scope={'__name__':'doc53f_watch','__file__':str(LAB/'watch.py')}
exec(compile(source,str(LAB/'watch.py'),'exec'),scope)
scope['watch'](role)
