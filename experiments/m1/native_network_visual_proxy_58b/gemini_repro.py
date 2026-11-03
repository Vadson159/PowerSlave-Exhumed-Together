"""Re-run the unchanged Gemini DOC58D.1 suite from an isolated build dir; results land in 58B evidence, never in the Gemini folder."""
from workflow import ROOT,LAB,OUT,GEMINI,CM,sha,dump,command,stamp
import shutil
def main():
    before={p.name:sha(p) for p in GEMINI.iterdir() if p.is_file()}
    b=LAB/'build_gemini'
    command([CM,'-S',GEMINI,'-B',b,'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'GEMINI_CONFIGURE')
    command([CM,'--build',b,'--config','Release','--target','test_doc58d1_reconciled'],'GEMINI_BUILD')
    run=OUT/'gemini_repro';run.mkdir(exist_ok=True)
    d=command([b/'Release/test_doc58d1_reconciled.exe'],'GEMINI_RUN',cwd=run,require=False)
    after={p.name:sha(p) for p in GEMINI.iterdir() if p.is_file()}
    dump(OUT/('GEMINI_REPRO_'+stamp()+'.json'),dict(exit_code=d['exit_code'],gemini_files_unchanged=before==after,results=str((run/'TEST_RESULTS.json').relative_to(ROOT)),
        results_sha256=sha(run/'TEST_RESULTS.json'),header_sha256=sha(GEMINI/'doc58d1_hardened_visual_proxy_state_machine.hpp'),guard_sha256=sha(GEMINI/'multi_slot_snapshot_guard.hpp'),test_sha256=sha(GEMINI/'test_doc58d1_reconciled.cpp')))
main()
