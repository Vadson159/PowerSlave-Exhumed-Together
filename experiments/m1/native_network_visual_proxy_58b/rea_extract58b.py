"""DOC58B copy of the accepted DOC56 extractor (output root changed only).
Copy REA records of one Frida session from an exported Evidence bundle into a run folder.

Claude's REA MCP returns Frida results inline; large observations are written to disk only by
export_evidence_bundle. Each extracted file is {"structuredContent": <unchanged bundle record>}
(the envelope the accepted DOC51-55 gate reads) plus an explicit provenance block.
Usage: rea_extract.py <bundle.json> <run> <session_id> <status_tag>
"""
from pathlib import Path
import hashlib,json,sys
from workflow import ROOT,OUT,dump
def main():
    bundle,run,session,tag=sys.argv[1:5]
    path=Path(bundle);data=json.loads(path.read_text(encoding='utf8'));folder=OUT/run
    assert folder.is_dir() and run.startswith('N58B_')
    mine=[r for r in data['records'] if r.get('parameters',{}).get('session_id')==session]
    provenance=dict(bundle=str(path),bundle_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),session_id=session,
        note='Unmodified REA Evidence record exported by export_evidence_bundle; envelope added for the accepted gate reader')
    written=[]
    for r in mine:
        if r['operation']=='load_frida_script':
            name='LOAD_'+(Path(r['normalized_result']['source_path']).stem.upper() if r['normalized_result'].get('source_path') else 'INLINE_'+r['normalized_result']['script_id'][:8].upper())+'.json'
            if not (folder/name).exists():dump(folder/name,dict(structuredContent=r,provenance=provenance));written.append(name)
    status=[r for r in mine if r['operation']=='frida_session_status']
    if status:
        # Bundle records use canonical (digest) order, not time order; observations are cumulative.
        last=max(status,key=lambda r:(len(r['normalized_result'].get('messages',[])),r['normalized_result'].get('state')=='detached'));name=tag+'.json'
        dump(folder/name,dict(structuredContent=last,provenance=provenance));written.append(name)
        n=last['normalized_result'];print(json.dumps(dict(state=n.get('state'),pid=n.get('pid'),messages=len(n.get('messages',[])),truncated=n.get('messages_truncated'))))
    print(json.dumps(dict(written=written,records=len(mine))))
if __name__=='__main__':main()
