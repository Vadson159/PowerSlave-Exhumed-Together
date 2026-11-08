"""Tests for ReaExtractor."""

import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

TOOL_DIR = Path(__file__).resolve().parent.parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from rea_extractor import ReaExtractor
from session import find_project_root


class TestReaExtractor(unittest.TestCase):

    def setUp(self):
        self.tmp_dir = Path(tempfile.mkdtemp(prefix='rea_test_'))
        self.extractor = ReaExtractor()

    def tearDown(self):
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_extract_synthetic_bundle(self):
        bundle = {
            'records': [
                {
                    'operation': 'load_frida_script',
                    'parameters': {'session_id': 'sess_123'},
                    'normalized_result': {
                        'session_id': 'sess_123',
                        'source_path': '/experiments/m1/test_probe.js',
                    },
                },
                {
                    'operation': 'load_frida_script',
                    'parameters': {'session_id': 'sess_123'},
                    'normalized_result': {
                        'session_id': 'sess_123',
                        'source_path': None,
                        'script_id': 'abcd1234ef',
                    },
                },
                {
                    'operation': 'frida_session_status',
                    'parameters': {'session_id': 'sess_123'},
                    'normalized_result': {
                        'session_id': 'sess_123',
                        'state': 'detached',
                        'pid': 1234,
                        'messages': [{'type': 'send', 'payload': {'event': 'test'}}],
                    },
                },
            ]
        }
        bundle_path = self.tmp_dir / 'bundle.json'
        bundle_path.write_text(json.dumps(bundle), encoding='utf-8')

        run_folder = self.tmp_dir / 'RUN_01'
        res = self.extractor.extract(bundle_path, run_folder, 'sess_123', status_tag='STATUS_EXIT')
        self.assertEqual(res['status'], 'PASS')
        self.assertIn('LOAD_TEST_PROBE.json', res['written_files'])
        self.assertIn('LOAD_INLINE_ABCD1234.json', res['written_files'])
        self.assertIn('STATUS_EXIT.json', res['written_files'])

        # Check provenance
        content = json.loads((run_folder / 'STATUS_EXIT.json').read_text(encoding='utf-8'))
        self.assertIn('provenance', content)
        self.assertEqual(content['provenance']['session_id'], 'sess_123')
        self.assertIn('structuredContent', content)

    def test_extract_real_bundle_01(self):
        root = find_project_root()
        bundle_path = root / 'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/rea_bundles/bundle_01_host_gameplay.json'
        if not bundle_path.is_file():
            self.skipTest("bundle_01_host_gameplay.json not found")

        # Discover first session_id in the bundle
        data = json.loads(bundle_path.read_text(encoding='utf-8'))
        session_id = None
        for r in data.get('records', []):
            session_id = r.get('parameters', {}).get('session_id')
            if session_id:
                break
        self.assertIsNotNone(session_id)

        run_folder = self.tmp_dir / 'HOST_01'
        res = self.extractor.extract(bundle_path, run_folder, session_id, status_tag='STATUS_EXIT')
        self.assertEqual(res['status'], 'PASS')
        self.assertGreater(res['total_session_records'], 0)


if __name__ == '__main__':
    unittest.main()
