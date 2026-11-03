"""Reuse the accepted isolated DOC53F/DOC55 runner with fresh DOC56 names, DOC56 artifacts and no attack bind.
Only the build-manifest schema line differs: DOC56 freezes changed/new files and artifacts separately."""
from pathlib import Path
source=(Path(__file__).resolve().parent.parent/'doc53_followup/runner.py').read_text(encoding='utf8')
for a,b in [("name.startswith('N53F_')","name.startswith('N56_')"),
            ("('bind k \"attack\"\\n' if role=='host' else '')","''"),
            ("for row in manifest['sources']:","for row in manifest['changed_existing']+manifest['new_files']+manifest['artifacts']:")]:
    assert source.count(a)==1,a;source=source.replace(a,b)
exec(compile(source,str(Path(__file__).resolve()),'exec'),globals())
