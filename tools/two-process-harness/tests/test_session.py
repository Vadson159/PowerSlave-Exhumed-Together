"""Tests for TwoProcessConfig and TwoProcessSession."""

import json
from pathlib import Path
import sys
import unittest

TOOL_DIR = Path(__file__).resolve().parent.parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from session import TwoProcessConfig, TwoProcessSession, find_project_root, EXPECTED_EXE_SHA256


class TestSession(unittest.TestCase):

    def test_default_config(self):
        root = find_project_root()
        cfg = TwoProcessConfig(root=root)
        self.assertEqual(cfg.host_run, 'N58B_HOST_02')
        self.assertEqual(cfg.client_run, 'N58B_CLIENT_02')
        self.assertEqual(cfg.expected_exe_sha256, EXPECTED_EXE_SHA256)
        self.assertTrue(cfg.out_dir.is_dir())
        self.assertTrue(cfg.host_exe.is_file())
        self.assertTrue(cfg.client_exe.is_file())
        self.assertTrue(cfg.host_dll.is_file())
        self.assertTrue(cfg.client_dll.is_file())

    def test_run_dir_resolution(self):
        cfg = TwoProcessConfig()
        host_dir = cfg.get_run_dir('host')
        client_dir = cfg.get_run_dir('client')
        self.assertTrue(str(host_dir).endswith('N58B_HOST_02'))
        self.assertTrue(str(client_dir).endswith('N58B_CLIENT_02'))

    def test_verify_targets(self):
        cfg = TwoProcessConfig()
        targets = cfg.verify_targets()
        self.assertEqual(targets['host'], EXPECTED_EXE_SHA256)
        self.assertEqual(targets['client'], EXPECTED_EXE_SHA256)

    def test_verify_dlls(self):
        cfg = TwoProcessConfig()
        dlls = cfg.verify_dlls()
        self.assertIn('host', dlls)
        self.assertIn('client', dlls)
        self.assertEqual(len(dlls['host']), 64)
        self.assertEqual(len(dlls['client']), 64)

    def test_serialization(self):
        cfg = TwoProcessConfig(host_run='TEST_HOST', client_run='TEST_CLIENT')
        d = cfg.to_dict()
        cfg2 = TwoProcessConfig.from_dict(d)
        self.assertEqual(cfg2.host_run, 'TEST_HOST')
        self.assertEqual(cfg2.client_run, 'TEST_CLIENT')

    def test_session_records(self):
        cfg = TwoProcessConfig()
        sess = TwoProcessSession(cfg)
        h_record = sess.get_launch_record('host')
        c_record = sess.get_launch_record('client')
        self.assertEqual(h_record['pid'], 39840)
        self.assertEqual(c_record['pid'], 23436)
        self.assertEqual(sess.get_primary_tid('host'), 23616)
        self.assertEqual(sess.get_primary_tid('client'), 19712)


if __name__ == '__main__':
    unittest.main()
