"""Same bounded OS-input fixture, with DOC55 workflow/output and fresh run IDs."""
from pathlib import Path
source=(Path(__file__).resolve().parent.parent/'doc53_followup/input_fixture.py').read_text(encoding='utf8')
exec(compile(source,str(Path(__file__).resolve()),'exec'),globals())
