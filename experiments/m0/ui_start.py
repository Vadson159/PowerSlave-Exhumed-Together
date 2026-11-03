"""Replay the seven-Return start observed in C08 in a fresh owned M0 profile.

Each key/capture still uses ui.py's validated process path and owned HWND. No global
input, focus forcing, engine calls, or arbitrary menu text. Inspect captures afterwards.
"""
from pathlib import Path
import runpy, sys, time

run=sys.argv[1]
ui=Path(__file__).with_name('ui.py')
for step in range(7):
    sys.argv=[str(ui),run,'key','--key','RETURN'];runpy.run_path(str(ui),run_name='__main__')
    time.sleep(0.55)
    sys.argv=[str(ui),run,'capture'];runpy.run_path(str(ui),run_name='__main__')
