"""Reuse the accepted isolated runner with fresh DOC55 names and no attack bind."""
from pathlib import Path
source=(Path(__file__).resolve().parent.parent/'doc53_followup/runner.py').read_text(encoding='utf8')
source=source.replace("name.startswith('N53F_')","name.startswith('N55_')")
source=source.replace("('bind k \"attack\"\\n' if role=='host' else '')","''")
exec(compile(source,str(Path(__file__).resolve()),'exec'),globals())
