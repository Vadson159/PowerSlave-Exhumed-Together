"""Tests for ScenarioRunner."""

from pathlib import Path
import sys
import unittest

TOOL_DIR = Path(__file__).resolve().parent.parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from scenario_runner import ScenarioRunner


class TestScenarioRunner(unittest.TestCase):

    def setUp(self):
        self.runner = ScenarioRunner()

    def test_step_validation_valid(self):
        valid_steps = [
            {'action': 'focus', 'role': 'host'},
            {'action': 'focus', 'role': 'client'},
            {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.03},
            {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.1, 'handoff': True},
            {'action': 'mouse', 'role': 'host', 'dx': 120},
            {'action': 'hold', 'seconds': 2.5},
        ]
        for i, s in enumerate(valid_steps):
            self.runner.validate_step(s, i)

    def test_step_validation_invalid(self):
        with self.assertRaises(ValueError):
            self.runner.validate_step({'action': 'unknown'}, 0)
        with self.assertRaises(ValueError):
            self.runner.validate_step({'action': 'focus', 'role': 'invalid'}, 1)
        with self.assertRaises(ValueError):
            self.runner.validate_step({'action': 'key', 'role': 'host', 'key': 'W', 'hold': -1}, 2)
        with self.assertRaises(ValueError):
            self.runner.validate_step({'action': 'hold', 'seconds': 1000}, 3)

    def test_canonical_doc58b_scenario(self):
        scen = self.runner.build_doc58b_run02_scenario()
        self.runner.validate_scenario(scen)
        res = self.runner.execute_scenario(scen, dry_run=True)
        self.assertEqual(res['status'], 'DRY_RUN_PASS')
        self.assertEqual(res['total_steps'], 33)
        self.assertTrue(res['validated'])


if __name__ == '__main__':
    unittest.main()
