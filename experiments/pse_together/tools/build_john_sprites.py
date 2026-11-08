"""Build the single shared John sprite package (every player uses the same skin).

Additive KEX package: original DOC-13 john.png pixels (unmodified), the 2015
rectangle/rotation definitions, and the same four animations in a normal and a
"proxy" variant. The proxy variant lowers the origin by 59 texture pixels: the
accepted passive proxy sits at authoritative z + 88 with scale 1.5 (59 * 1.5 =
88.5 world units). No actions, scripts or stock resource overrides.

Resource names: player/pse_john_{idle,walk,shoot,death} and
player/pse_john_proxy_{...}. The names carry no slot: all slots share them.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'docs/presentation/player_sprites'
DEST = ROOT / 'experiments/pse_together/assets/coop_sprites_john.kpf'
NAME = 'pse_john'
STATES = ('idle', 'walk', 'shoot', 'death')
ROTATIONS = ('N', 'NE', 'E', 'ES', 'S', 'SW', 'W', 'WN')
PROXY_DROP = 59


def sha(b):
    return hashlib.sha256(b).hexdigest()


def members():
    rects = (SOURCE / 'john_sprite.txt').read_text(encoding='utf8')
    anim = (SOURCE / 'john_anim.txt').read_text(encoding='utf8')
    base = f'sprites/player/{NAME}'
    result = {base + '.png': (SOURCE / 'john.png').read_bytes(),
              base + '_sprite.txt': rects.replace('player/john', f'player/{NAME}').encode()}
    text = anim.replace('john_', NAME + '_').replace('player/john"', f'player/{NAME}"')
    result[base + '_anim.txt'] = text.encode()
    proxy = text.replace(NAME + '_', NAME + '_proxy_')
    proxy = re.sub(r'(,\s*-?\d+\s*,\s*)(-?\d+)(\s*,\s*[01]\s*})',
                   lambda m: m[1] + str(int(m[2]) + PROXY_DROP) + m[3], proxy)
    result[base + '_proxy_anim.txt'] = proxy.encode()
    result['PSE_TOGETHER_SPRITES_CREDITS.txt'] = (
        'John player sprites: PC Exhumed sprites ripped by Dunkelschwamm for The Spriters Resource.\n'
        'Source: supplied PowerSlave EX 2015 Co-Op Edition mods/john.kpf; DOC-13 assets.\n'
        'Original art belongs to its respective rights holders. Prior mod inclusion is not a new license.\n'
        'One shared skin for all players; pixels unmodified; no gameplay actions, scripts or stock overrides.\n'
    ).encode()
    return result


def validate(data):
    from PIL import Image
    import io
    base = f'sprites/player/{NAME}'
    master = Image.open(SOURCE / 'john.png').convert('RGBA')
    im = Image.open(io.BytesIO(data[base + '.png'])).convert('RGBA')
    assert im.size == (1024, 315) and list(im.getdata()) == list(master.getdata())
    rects = [tuple(map(int, m)) for m in re.findall(r'^sprite\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',
                                                    data[base + '_sprite.txt'].decode(), re.M)]
    assert [r[0] for r in rects] == list(range(40))
    assert all(w > 0 and h > 0 and x + w <= im.width and y + h <= im.height for _, x, y, w, h in rects)
    assert f'texture "{base}.png"' in data[base + '_sprite.txt'].decode()
    for suffix, proxy in (('_anim.txt', False), ('_proxy_anim.txt', True)):
        text = data[base + suffix].decode()
        tokens = re.findall(r'"[^"]*"|-?\d+|[A-Za-z_][A-Za-z_0-9]*|[{},]', re.sub(r'//[^\n]*', '', text))
        names = {f'{NAME}_{"proxy_" if proxy else ""}{s}' for s in STATES}
        allowed = names | {'frame', 'delay', 'sprites'} | {'rotation_' + r for r in ROTATIONS}
        # Whitelisted grammar: no actions, jumps, Remove or script callbacks.
        assert all(t in allowed or t in '{} ,' or re.fullmatch(r'-?\d+', t) or t == f'"player/{NAME}"' for t in tokens)
        assert set(re.findall(rf'^{NAME}_\w+', text, re.M)) == names
        assert len(re.findall(r'\bframe\b', re.sub(r'//[^\n]*', '', text))) == 14
        for rot in ROTATIONS:
            assert text.count('rotation_' + rot + '\n') == 14
        assert 'delay 65535' in text and 'john' not in text.replace(NAME, '')
        assert all(0 <= int(s) < 40 for s in re.findall(r'\{\s*"[^"]+",\s*(\d+)', text))
    return dict(texture=base + '.png', pixels_unchanged=True, rectangles=40, frames=14)


def build(path=DEST):
    data = members()
    check = validate(data)
    path = Path(path).resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError('Output outside mod workspace')
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, value in sorted(data.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 7, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, value, compresslevel=9)
    manifest = dict(schema='pse-p8-john-shared-v1', package=path.relative_to(ROOT).as_posix(),
                    sha256=sha(path.read_bytes()), character=NAME, scale=1.5, proxy_origin_drop=PROXY_DROP,
                    slots=[dict(slot=i, character=NAME) for i in range(8)], check=check,
                    members=[dict(path=n, sha256=sha(b), size=len(b)) for n, b in sorted(data.items())],
                    source=[dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p.read_bytes()))
                            for p in [SOURCE / 'john.png', SOURCE / 'john_anim.txt', SOURCE / 'john_sprite.txt']],
                    runtime_verified=False)
    path.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    return manifest


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=DEST)
    args = ap.parse_args()
    m = build(args.out)
    print(f'JOHN SHARED SKIN: {len(m["members"])} members; sha256={m["sha256"]}; NOT runtime')
