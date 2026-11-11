"""Put the current build into a TEST install root (made by p27.py install): python -X utf8 deploy_test.py <root> [build-dir]

Copies pse_together.dll (+ loader pieces) into <root>/game and writes pe-together.ini (payload SHA-256, UTF-16) the loader checks.
Never point it at the user's own install or the Steam folder.
"""
import hashlib, shutil, sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
build = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(__file__).resolve().parents[3] / 'b99'
game = root / 'game'
if not (game / 'anubis_Shipping_Steam_x64.exe').exists() or not (root / 'install_manifest.json').exists():
    raise SystemExit('not a p27 test install root: ' + str(root))
if 'steamapps' in str(root).lower():
    raise SystemExit('refusing a Steam folder')
pairs = [(build / 'dll' / 'pse_together.dll', game / 'pse_together.dll'), (build / 'p27' / 'SDL2.dll', game / 'SDL2.dll'),
         (build / 'p27' / 'pe-together.dll', game / 'pe-together.dll'), (build / 'p27' / 'pe-play.exe', root / 'pe-play.exe')]
for src, dst in pairs:
    shutil.copyfile(src, dst)
digest = hashlib.sha256((game / 'pse_together.dll').read_bytes()).hexdigest()
(game / 'pe-together.ini').write_text('[payload]\nsha256=' + digest + '\n', encoding='utf-16')
print('deployed', digest[:16], 'to', root)
