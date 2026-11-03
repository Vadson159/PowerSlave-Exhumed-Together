"""One narrow read-only pass on the saved Ghidra project, no auto-analysis."""
from pathlib import Path
import json, os, subprocess
from audit import ROOT, LAB, OUT, dump, stamp

prior=sorted((OUT/'static').glob('invocation_*.json'))[-1]
a=json.loads(prior.read_text(encoding='utf8'))['arguments']
i=a.index('-scriptPath')
a[i+1]=str(LAB)+';'+str(ROOT/'docs/research')
j=a.index('-postScript')
a=a[:j]+['-postScript','BackgroundFields.java',str(OUT/'static/FIELD_OPERANDS.json')]
s=stamp()
a+=['-log',str(OUT/'static'/('headless_fields_'+s+'.log')),'-scriptlog',str(OUT/'static'/('script_fields_'+s+'.log'))]
dump(OUT/'static'/('invocation_fields_'+s+'.json'),dict(arguments=a,no_target_execution=True))
env=os.environ.copy(); env.update(TEMP=str(ROOT/'docs/research/runtime'),TMP=str(ROOT/'docs/research/runtime'))
with (OUT/'static'/('console_fields_'+s+'.log')).open('xb') as f:
    r=subprocess.run(a,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
assert r.returncode==0
p=OUT/'static/FIELD_OPERANDS.json'; assert p.is_file()
o=json.loads(p.read_text()); print(json.dumps(o,indent=2))
