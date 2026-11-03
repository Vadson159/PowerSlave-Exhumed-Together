"""Accepted saved-project exporter, with fresh task-local JVM scratch on S."""
from workflow import ROOT,OUT,SCRATCH
from pathlib import Path
source=ROOT/'docs/research/invoke_ghidra.py'
s=source.read_text(encoding='utf8')
old="runtime=ROOT/'docs/research/runtime'"
assert s.count(old)==1
s=s.replace(old,"runtime=ROOT/'experiments/m1/native_visual_proxy/scratch/continuation/ghidra_runtime'")
scope={'__name__':'__main__','__file__':str(source)}
exec(compile(s,str(source),'exec'),scope)
