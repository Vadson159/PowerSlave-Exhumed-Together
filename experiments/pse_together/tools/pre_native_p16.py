"""P16 uses accepted pre-native checks, frozen artifacts, foreign-process refusal."""
from pathlib import Path
path=Path(__file__).with_name('pre_native_p7.py')
source=path.read_text(encoding='utf8').replace("default='b80'","default='b88'").replace("pse_dll_{role}_p7","pse_dll_{role}_p16")
source=source.replace("for x in p.relative_to(LAB).parts)","for x in p.relative_to(LAB).parts[:-1])")
scope={'__name__':'p16_pre_native','__file__':str(path)}
exec(compile(source,str(path), 'exec'),scope)
if __name__=='__main__':raise SystemExit(scope['main']())
