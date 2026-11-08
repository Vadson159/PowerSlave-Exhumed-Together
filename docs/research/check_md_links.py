"""Check relative Markdown links (files and #anchors are not fetched over the network).

Usage: python -X utf8 -I docs/research/check_md_links.py FILE.md [FILE.md ...] [--out RESULT.json]
A link passes when its target path (relative to the Markdown file) exists. http(s)/mailto
links and pure #anchors are listed but not checked. Exit code 0 only if no link is broken.
"""
import argparse, datetime, hashlib, json, pathlib, re, sys
from urllib.parse import unquote

LINK = re.compile(r'(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
FENCE = re.compile(r'^```')


def links(text):
    in_fence = False
    for no, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line.strip()):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for m in LINK.finditer(line):
            yield no, m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--out')
    a = ap.parse_args()
    report, broken = [], 0
    for f in a.files:
        p = pathlib.Path(f).resolve()
        text = p.read_text(encoding='utf-8')
        rows = []
        for no, target in links(text):
            if re.match(r'^(https?:|mailto:)', target) or target.startswith('#'):
                rows.append({'line': no, 'target': target, 'result': 'SKIPPED_EXTERNAL_OR_ANCHOR'})
                continue
            path = unquote(target.split('#', 1)[0])
            path = re.sub(r':\d+$', '', path)
            ok = (p.parent / path).exists()
            broken += 0 if ok else 1
            rows.append({'line': no, 'target': target, 'result': 'OK' if ok else 'BROKEN'})
        report.append({'file': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                       'links': len(rows), 'broken': sum(r['result'] == 'BROKEN' for r in rows), 'rows': rows})
    out = {'schema': 'pse-md-link-check-v1', 'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
           'files': report, 'broken_total': broken}
    if a.out:
        pathlib.Path(a.out).write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'files': [(r['file'].split('\\')[-1], r['links'], r['broken']) for r in report], 'broken_total': broken},
                     ensure_ascii=False))
    for r in report:
        for row in r['rows']:
            if row['result'] == 'BROKEN':
                print('BROKEN', r['file'], row['line'], row['target'])
    return 0 if broken == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
