"""Build P8's deterministic, additive KEX sprite package from DOC-13 assets.

No image generation, edits, installs, engine execution or proprietary binary embedding.
The source pixels and 2015 rectangle/rotation definitions are retained verbatim.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'docs/presentation/player_sprites'
DEST = ROOT / 'experiments/pse_together/assets/coop_sprites.kpf'
RAMP = {(0,16,0), (20,24,4), (20,32,0), (48,32,0), (68,28,8),
        (40,52,8), (52,68,16), (76,68,48), (60,88,24), (96,80,52), (112,84,52)}
STATES = ('idle', 'walk', 'shoot', 'death')
ROTATIONS = ('N', 'NE', 'E', 'ES', 'S', 'SW', 'W', 'WN')


def sha(b):
    return hashlib.sha256(b).hexdigest()


def members():
    """Every filename is additive: stock sprite/actor definitions are never replaced."""
    rects = (SOURCE / 'john_sprite.txt').read_text(encoding='utf8')
    anim = (SOURCE / 'john_anim.txt').read_text(encoding='utf8')
    result = {}
    for slot in range(1, 9):
        name = f'john_p{slot}'
        result[f'sprites/player/{name}.png'] = (SOURCE / f'demo_{name}.png').read_bytes()
        result[f'sprites/player/{name}_sprite.txt'] = rects.replace('player/john', f'player/{name}').encode()
        text = anim.replace('john_', name + '_').replace('player/john"', f'player/{name}"')
        result[f'sprites/player/{name}_anim.txt'] = text.encode()
        # The accepted passive proxy sits at authoritative z + 88 with scale 1.5.
        # Its art origin is lowered 59 texture pixels (88.5 world units). Keep
        # the native transform/sector/lifecycle contract intact; visual QA required.
        proxy = text.replace(name + '_', name + '_proxy_')
        proxy = re.sub(r'(,\s*-?\d+\s*,\s*)(-?\d+)(\s*,\s*[01]\s*})',
                       lambda m: m[1] + str(int(m[2]) + 59) + m[3], proxy)
        result[f'sprites/player/{name}_proxy_anim.txt'] = proxy.encode()
    result['PSE_TOGETHER_SPRITES_CREDITS.txt'] = (
        'John player sprites: PC Exhumed sprites ripped by Dunkelschwamm for The Spriters Resource.\n'
        'Source: supplied PowerSlave EX 2015 Co-Op Edition mods/john.kpf; DOC-13 assets.\n'
        'Original art belongs to its respective rights holders. Prior mod inclusion is not a new license.\n'
        'Eight DOC-13 camouflage variants; no gameplay actions, scripts or stock resource overrides.\n'
    ).encode()
    return result


def validate(data):
    from PIL import Image
    import io
    master = Image.open(SOURCE / 'john.png').convert('RGBA')
    original = list(master.getdata())
    result = []
    for slot in range(1, 9):
        prefix = f'sprites/player/john_p{slot}'
        im = Image.open(io.BytesIO(data[prefix + '.png'])).convert('RGBA')
        pixels = list(im.getdata())
        assert im.size == (1024, 315)
        assert len(pixels) == len(original)
        changed = [(a, b) for a, b in zip(original, pixels) if a != b]
        assert all(a[:3] in RAMP and a[3] == b[3] for a, b in changed)
        if slot == 1:
            assert not changed
        else:
            assert changed
        rects = [tuple(map(int, m)) for m in re.findall(r'^sprite\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',
                                                        data[prefix + '_sprite.txt'].decode(), re.M)]
        assert [r[0] for r in rects] == list(range(40))
        assert all(w > 0 and h > 0 and x + w <= im.width and y + h <= im.height for _, x, y, w, h in rects)
        for suffix in ('_anim.txt', '_proxy_anim.txt'):
            text = data[prefix + suffix].decode()
            # Whitelisted grammar: no actions, jumps, Remove or script callbacks.
            tokens = re.findall(r'"[^"]*"|-?\d+|[A-Za-z_][A-Za-z_0-9]*|[{},]', re.sub(r'//[^\n]*', '', text))
            names = {f'john_p{slot}_{"proxy_" if "proxy" in suffix else ""}{s}' for s in STATES}
            allowed = names | {'frame', 'delay', 'sprites'} | {'rotation_' + r for r in ROTATIONS}
            assert all(t in allowed or t in '{} ,' or re.fullmatch(r'-?\d+', t) or t == f'"player/john_p{slot}"' for t in tokens)
            assert set(re.findall(r'^john_\w+', text, re.M)) == names
            # All 14 frames have all eight rotations; final death holds forever.
            assert len(re.findall(r'\bframe\b', re.sub(r'//[^\n]*', '', text))) == 14
            for rot in ROTATIONS:
                assert text.count('rotation_' + rot + '\n') == 14
            assert 'delay 65535' in text
            assert all(0 <= int(s) < 40 for s in re.findall(r'\{\s*"[^"]+",\s*(\d+)', text))
        result.append(dict(slot=slot - 1, texture=prefix + '.png', changed_pixels=len(changed),
                           alpha_preserved=True, non_ramp_pixels_preserved=True, rectangles=40, frames=14))
    assert len({sha(data[f'sprites/player/john_p{i}.png']) for i in range(1, 9)}) == 8
    return result


def build(path=DEST):
    data = members()
    checks = validate(data)
    path = Path(path).resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError('Output outside mod workspace')
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, value in sorted(data.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 4, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, value, compresslevel=9)
    manifest = dict(schema='pse-p8-sprites-v1', package=path.relative_to(ROOT).as_posix(),
                    sha256=sha(path.read_bytes()), slots=checks,
                    members=[dict(path=n, sha256=sha(b), size=len(b)) for n, b in sorted(data.items())],
                    source=[dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p.read_bytes()))
                            for p in [SOURCE / 'john.png', SOURCE / 'john_anim.txt', SOURCE / 'john_sprite.txt'] +
                            [SOURCE / f'demo_john_p{i}.png' for i in range(1, 9)]],
                    runtime_verified=False)
    path.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    return manifest


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=DEST)
    args = ap.parse_args()
    m = build(args.out)
    print(f'P8 ASSETS: {len(m["slots"])} slots, {len(m["members"])} members; sha256={m["sha256"]}; NOT runtime')
