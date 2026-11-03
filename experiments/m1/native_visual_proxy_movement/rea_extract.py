"""Unmodified accepted complete REA record-envelope extractor, DOC58A completion output domain."""
from workflow import ROOT,LAB,OUT,dump
from pathlib import Path
s=(ROOT/'experiments/m1/native_map_ready/rea_extract.py').read_text(encoding='utf8')
for a,b in [('from workflow import ROOT,dump','from workflow import ROOT,OUT,dump'),("folder=ROOT/'docs/evidence/m1'/run",'folder=OUT/run'),("run.startswith('N56_')","run.startswith('N58A_C')"),("r['normalized_result'].get('source_path','inline')","(r['normalized_result'].get('source_path') or ('inline_'+r['evidence_id'][3:15]))")]:
    assert s.count(a)==1,a;s=s.replace(a,b)
exec(compile(s,str(Path(__file__).resolve()),'exec'),globals())
