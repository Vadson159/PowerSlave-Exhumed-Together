"""DOC56 uses the accepted DOC53F/DOC55 command and baseline helpers in separate output/build roots."""
from pathlib import Path
import importlib.util,json,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56'
spec=importlib.util.spec_from_file_location('workflow53f_reused56',LAB.parent/'doc53_followup/workflow.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.ROOT=ROOT;m.OUT=OUT
CM=m.CM;sha=m.sha;dump=m.dump;stamp=m.stamp;game_check=m.game_check;command=m.command
TARGETS=m.TARGETS+['snapshot_contract','map_ready_contract'];m.TARGETS=TARGETS
SUITES={'pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'}
def build():
    # One more add_subdirectory level than DOC55: native_snapshot/background_host/second_instance/network_input.
    command([CM,'-S',LAB,'-B',LAB/'build','-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE')
    names=sys.argv[2:] or TARGETS
    main=[n for n in names if n not in SUITES];nested=[n for n in names if n in SUITES]
    if main:command([CM,'--build',LAB/'build','--config','Release','--parallel','2','--target']+main,'BUILD')
    if nested:command([CM,'--build',LAB/'build/integration/integration/integration/integration/transport','--config','Release','--parallel','2','--target']+nested,'BUILD_SUITES')
if __name__=='__main__':
    accepted=json.loads((ROOT/'docs/evidence/m1/NATIVE_SNAPSHOT_55/RESULT_SUMMARY_FINAL.json').read_text(encoding='utf8'));assert accepted['verdict']=='PASS'
    {'baseline':m.baseline,'build':build,'tests':m.tests,'integrity':m.integrity}[sys.argv[1]]()
