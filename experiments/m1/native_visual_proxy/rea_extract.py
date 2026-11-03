"""Reuse the accepted DOC56 complete-record envelope adapter in the DOC57 domain."""
from pathlib import Path
from workflow import ROOT,LAB,OUT,dump
s=(LAB.parent/'native_map_ready/rea_extract.py').read_text(encoding='utf8')
for before,after in [('from workflow import ROOT,dump','from workflow import ROOT,OUT,dump'),("folder=ROOT/'docs/evidence/m1'/run",'folder=OUT/run'),("run.startswith('N56_')","run.startswith('N57_')")]:
    assert s.count(before)==1,before;s=s.replace(before,after)
exec(compile(s,str(Path(__file__).resolve()),'exec'),globals())
