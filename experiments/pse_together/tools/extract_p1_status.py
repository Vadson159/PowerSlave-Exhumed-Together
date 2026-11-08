"""Select a complete retained REA status from a canonical bundle, not its summary projection."""
import json
import sys
from pathlib import Path
bundle,session,destination=sys.argv[1:4]
data=json.loads(Path(bundle).read_text(encoding='utf8'))
rows=[r for r in data['records'] if r['operation']=='frida_session_status' and r['parameters']['session_id']==session]
assert rows, 'No full retained status for this session'
record=max(rows,key=lambda r:len(r['normalized_result'].get('messages',[])))
assert not record['normalized_result'].get('messages_truncated'), 'REA capture truncated'
with Path(destination).open('x',encoding='utf8') as f: json.dump({'structuredContent':record},f,ensure_ascii=False,indent=2)
print(record['evidence_id'],len(record['normalized_result'].get('messages',[])),'messages')
