"""Re-evaluate the original CTest XML misplaced by a relative --out; do not rerun suites."""
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
out=ROOT/'docs/evidence/m1/PSE_TOGETHER_P1/OFFLINE_01'
source=ROOT/'b60/docs/evidence/m1/PSE_TOGETHER_P1/OFFLINE_01/ctest_junit.xml'
target=out/'ctest_junit.xml'
shutil.copy2(source,target)
expected=json.loads((ROOT/'experiments/pse_together/tools/expected_suites.json').read_text(encoding='utf8'))['suites']
result=json.loads((out/'RESULT.json').read_text(encoding='utf8'))
result['recovery']={'cause':'relative --out was interpreted by ctest under b60; run_offline.py now resolves it',
    'original_result':'RESULT.json','raw_xml_source':source.relative_to(ROOT).as_posix(),
    'raw_xml_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'tests_repeated':False}
got={tc.get('name'):tc for tc in ET.parse(target).getroot().iter('testcase')}
result['suites']=[]
for name,spec in expected.items():
    tc=got.get(name);rec={'name':name,'verdict':'MISSING'}
    if tc is not None:
        text=tc.find('system-out').text or '';matches=[bool(re.search(p,text,re.M)) for p in spec['patterns']]
        (out/'logs'/f'{name}.log').write_text(text,encoding='utf8')
        rec.update(status=tc.get('status'),seconds=tc.get('time'),matched=matches,
                   verdict='PASS' if tc.get('status')=='run' and tc.find('failure') is None and all(matches) else 'FAIL')
    result['suites'].append(rec)
result['passed']=sum(r['verdict']=='PASS' for r in result['suites'])
result['unexpected_registered']=sorted(set(got)-set(expected))
result['status']='PASS' if result['passed']==len(expected) and not result['unexpected_registered'] and result['steps']['ctest_exit']==0 else 'FAIL'
with (out/'RECOVERED_RESULT.json').open('x',encoding='utf8') as f: json.dump(result,f,indent=2,ensure_ascii=False)
print(result['status'],result['passed'],'/',len(expected),'without repeating tests')
