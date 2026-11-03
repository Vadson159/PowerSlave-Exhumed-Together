"""Accepted one-window OS input fixture, redirected to continuation S paths."""
from workflow import ROOT,LAB,OUT
from pathlib import Path
source=(LAB.parent/'input_fixture.py').read_text(encoding='utf8')
old="LAB.parent/'background_host/";assert source.count(old)==2
source=source.replace(old,"ROOT/'experiments/m1/background_host/")
exec(compile(source,str(Path(__file__).resolve()),'exec'),globals())
