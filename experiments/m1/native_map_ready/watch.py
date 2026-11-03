"""Same retained-process-handle observer, in the DOC56 output domain."""
from pathlib import Path
source=(Path(__file__).resolve().parent.parent/'doc53_followup/watch.py').read_text(encoding='utf8')
exec(compile(source,str(Path(__file__).resolve()),'exec'),globals())
