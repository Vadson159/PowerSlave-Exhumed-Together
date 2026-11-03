"""DOC55 uses the preceding command/baseline helpers, in separate output/build roots."""
from pathlib import Path
import importlib.util,json,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/NATIVE_SNAPSHOT_55'
spec=importlib.util.spec_from_file_location('workflow53f_reused',LAB.parent/'doc53_followup/workflow.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.ROOT=ROOT;m.OUT=OUT
CM=m.CM;sha=m.sha;dump=m.dump;stamp=m.stamp;game_check=m.game_check;command=m.command
TARGETS=m.TARGETS+['snapshot_contract'];m.TARGETS=TARGETS
if __name__=='__main__':
    gate=json.loads((ROOT/'docs/evidence/m1/DOC53_FOLLOWUP/DOC55_GATE.json').read_text(encoding='utf8'));assert gate['status']=='PASS_TO_DOC55'
    {'baseline':m.baseline,'build':m.build,'tests':m.tests,'integrity':m.integrity}[sys.argv[1]]()
