"""Tests for Doc58bReproduction and EvidenceReviewer."""

from pathlib import Path
import sys
import unittest

TOOL_DIR = Path(__file__).resolve().parent.parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from doc58b_reproduction import Doc58bReproduction
from evidence_reviewer import EvidenceReviewer
from session import find_project_root


class TestDoc58bReproduction(unittest.TestCase):

    def test_reproduction_pass(self):
        root = find_project_root()
        repro = Doc58bReproduction(root)
        res = repro.run_reproduction()

        self.assertEqual(res['status'], 'PASS')
        self.assertEqual(res['overall_verdict'], 'BOUNDED_PASS')
        self.assertEqual(res['discrepancies'], [])
        self.assertEqual(res['accounting']['host_captured'], 14844)
        self.assertEqual(res['accounting']['host_worker_send'], 14844)
        self.assertEqual(res['accounting']['client_sim_accepted'], 14844)
        self.assertEqual(res['accounting']['client_rejected'], 0)
        self.assertEqual(res['accounting']['correlated_rows'], 6232)
        self.assertEqual(res['accounting']['applied_rows'], 2478)
        self.assertEqual(res['accounting']['bit_exact_mismatches'], [])
        self.assertEqual(res['render']['ours'], 11628)
        self.assertEqual(res['render']['native_lerp_samples'], 2109)
        self.assertEqual(res['gates']['GATE-PSE-58C-01'], 'OPEN')
        self.assertEqual(res['gates']['GATE-PSE-58C-02'], 'VERIFIED')
        self.assertEqual(res['gates']['GATE-PSE-58C-03'], 'VERIFIED')
        self.assertEqual(res['gates']['GATE-PSE-58C-04'], 'DEFERRED')
        self.assertEqual(res['gates']['GATE-PSE-58C-05'], 'VERIFIED')
        self.assertEqual(res['gates']['GATE-PSE-58C-06'], 'DEFERRED')
        self.assertEqual(res['gates']['GATE-PSE-58C-07'], 'VERIFIED')
        self.assertEqual(res['gates']['GATE-PSE-58D-08'], 'PARTIAL')
        self.assertEqual(res['gates']['GATE-PSE-58D-09'], 'VERIFIED')


if __name__ == '__main__':
    unittest.main()
