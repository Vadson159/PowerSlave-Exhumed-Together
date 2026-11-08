"""Generic REA Evidence Extractor for Frida Sessions.

Extracts individual script execution logs and session status records from REA Evidence bundles:
- Eliminates hardcoded run name prefixes
- Handles both file-based scripts (LOAD_<NAME>.json) and inline diagnostic probes (LOAD_INLINE_<ID>.json)
- Canonicalizes message histories from cumulative session snapshots
- Emits standardized envelope with full provenance block
"""

import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional, Union

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import compute_sha256
else:
    from .session import compute_sha256


class ReaExtractor:
    """Extracts Frida session records from an exported REA bundle into an evidence run folder."""

    def __init__(self):
        pass

    def extract(
        self,
        bundle_path: Union[str, Path],
        run_folder: Union[str, Path],
        session_id: str,
        status_tag: str = 'STATUS_EXIT',
        overwrite: bool = False,
    ) -> Dict[str, Any]:
        bundle_p = Path(bundle_path).resolve()
        if not bundle_p.is_file():
            raise FileNotFoundError(f"Bundle file not found: {bundle_p}")

        folder = Path(run_folder).resolve()
        folder.mkdir(parents=True, exist_ok=True)

        bundle_bytes = bundle_p.read_bytes()
        bundle_sha256 = hashlib.sha256(bundle_bytes).hexdigest()
        data = json.loads(bundle_bytes.decode('utf-8-sig'))

        records = data.get('records', [])
        session_records = [
            r for r in records
            if r.get('parameters', {}).get('session_id') == session_id
        ]

        if not session_records:
            # Also try matching on normalized_result.session_id
            session_records = [
                r for r in records
                if r.get('normalized_result', {}).get('session_id') == session_id
            ]

        provenance = {
            'bundle': str(bundle_p),
            'bundle_sha256': bundle_sha256,
            'session_id': session_id,
            'note': 'Extracted REA Evidence record exported by export_evidence_bundle',
        }

        written_files: List[str] = []

        # 1. Extract loaded scripts
        for r in session_records:
            if r.get('operation') == 'load_frida_script':
                res = r.get('normalized_result', {})
                source_path = res.get('source_path')
                if source_path:
                    stem = Path(source_path).stem.upper()
                    name = f'LOAD_{stem}.json'
                else:
                    script_id = res.get('script_id', 'UNKNOWN')[:8].upper()
                    name = f'LOAD_INLINE_{script_id}.json'

                dest = folder / name
                if overwrite or not dest.exists():
                    with open(dest, 'w', encoding='utf-8') as f:
                        json.dump({'structuredContent': r, 'provenance': provenance}, f, indent=2, ensure_ascii=False)
                    written_files.append(name)

        # 2. Extract final session status
        status_records = [r for r in session_records if r.get('operation') == 'frida_session_status']
        status_summary: Optional[Dict[str, Any]] = None

        if status_records:
            # Bundle records use canonical digest order; observations are cumulative.
            # Select the status with greatest message count or detached state.
            last = max(
                status_records,
                key=lambda r: (
                    len(r.get('normalized_result', {}).get('messages', [])),
                    r.get('normalized_result', {}).get('state') == 'detached'
                )
            )
            dest = folder / f'{status_tag}.json'
            if overwrite or not dest.exists():
                with open(dest, 'w', encoding='utf-8') as f:
                    json.dump({'structuredContent': last, 'provenance': provenance}, f, indent=2, ensure_ascii=False)
                written_files.append(dest.name)

            norm = last.get('normalized_result', {})
            status_summary = {
                'state': norm.get('state'),
                'pid': norm.get('pid'),
                'messages': len(norm.get('messages', [])),
                'truncated': norm.get('messages_truncated', False),
            }

        return {
            'status': 'PASS',
            'session_id': session_id,
            'total_session_records': len(session_records),
            'written_files': written_files,
            'status_summary': status_summary,
            'bundle_sha256': bundle_sha256,
        }
