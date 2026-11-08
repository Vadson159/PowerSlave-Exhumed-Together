"""Declarative Scenario Runner for Two-Process Input Automation.

Enables execution of structured test input sequences:
- Loads declarative scenario definitions from JSON / dict
- Validates step schema and parameter bounds before execution
- Sequentially executes focus, keys, relative mouse, hold, and handoff actions
- Emits detailed step-by-step reports and execution logs
"""

import datetime
import json
from pathlib import Path
import sys
import traceback
from typing import Any, Dict, List, Optional, Union

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from input_controller import InputController
    from session import TwoProcessSession
else:
    from .input_controller import InputController
    from .session import TwoProcessSession


class ScenarioRunner:
    """Executes declarative input scenarios across host and client processes."""

    def __init__(self, session: Optional[TwoProcessSession] = None, input_controller: Optional[InputController] = None):
        self.session = session or TwoProcessSession()
        self.controller = input_controller or InputController(self.session)

    @staticmethod
    def validate_step(step: Dict[str, Any], index: int):
        if not isinstance(step, dict):
            raise ValueError(f"Step {index} must be a dict, got {type(step)}")
        action = step.get('action')
        if not action:
            raise ValueError(f"Step {index} missing 'action'")
        action = action.lower()

        if action == 'focus':
            role = step.get('role')
            if role not in ['host', 'client']:
                raise ValueError(f"Step {index} 'focus' requires role 'host' or 'client', got '{role}'")
        elif action == 'key':
            role = step.get('role', 'host')
            if role not in ['host', 'client']:
                raise ValueError(f"Step {index} 'key' invalid role '{role}'")
            key = step.get('key')
            if not key or not isinstance(key, str):
                raise ValueError(f"Step {index} 'key' missing or invalid key")
            hold = float(step.get('hold', 0.05))
            if hold <= 0 or hold > 10.0:
                raise ValueError(f"Step {index} hold time out of bounds: {hold}")
        elif action == 'mouse':
            role = step.get('role', 'host')
            dx = int(step.get('dx', 0))
            if abs(dx) > 2000:
                raise ValueError(f"Step {index} mouse dx too large: {dx}")
        elif action == 'hold':
            sec = float(step.get('seconds', step.get('duration', 0.1)))
            if sec <= 0 or sec > 300:
                raise ValueError(f"Step {index} hold duration out of bounds: {sec}")
        else:
            raise ValueError(f"Step {index} unknown action: '{action}'")

    def validate_scenario(self, scenario: Dict[str, Any]):
        if not isinstance(scenario, dict):
            raise ValueError("Scenario must be a dictionary")
        steps = scenario.get('steps')
        if not isinstance(steps, list):
            raise ValueError("Scenario must contain a 'steps' list")
        for i, step in enumerate(steps):
            self.validate_step(step, i)

    def execute_scenario(self, scenario: Union[Dict[str, Any], Path, str], dry_run: bool = False) -> Dict[str, Any]:
        """Executes the scenario and returns a comprehensive execution report."""
        if isinstance(scenario, (str, Path)):
            p = Path(scenario)
            scenario_data = json.loads(p.read_text(encoding='utf-8-sig'))
        else:
            scenario_data = scenario

        self.validate_scenario(scenario_data)

        name = scenario_data.get('name', 'unnamed_scenario')
        steps = scenario_data['steps']
        start_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
        step_results: List[Dict[str, Any]] = []
        overall_status = 'PASS'
        error_msg = None

        if dry_run:
            return {
                'status': 'DRY_RUN_PASS',
                'name': name,
                'total_steps': len(steps),
                'validated': True,
                'steps': steps,
            }

        try:
            for i, step in enumerate(steps):
                action = step['action'].lower()
                step_record = {'step_index': i, 'action': action, 'params': step}

                if action == 'focus':
                    res = self.controller.focus(step['role'])
                    step_record['result'] = res
                elif action == 'key':
                    role = step.get('role', 'host')
                    key_name = step['key']
                    hold_s = float(step.get('hold', 0.05))
                    handoff = bool(step.get('handoff', False))
                    res = self.controller.key(role, key_name, hold_s, handoff=handoff)
                    step_record['result'] = res
                elif action == 'mouse':
                    role = step.get('role', 'host')
                    dx = int(step.get('dx', 0))
                    res = self.controller.mouse(role, dx)
                    step_record['result'] = res
                elif action == 'hold':
                    sec = float(step.get('seconds', step.get('duration', 0.1)))
                    res = self.controller.hold(sec)
                    step_record['result'] = res

                step_results.append(step_record)
        except BaseException as e:
            overall_status = 'FAILED'
            error_msg = str(e)
            step_results.append({
                'step_index': len(step_results),
                'error': error_msg,
                'traceback': traceback.format_exc(),
            })

        finished_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
        report = {
            'status': overall_status,
            'scenario_name': name,
            'description': scenario_data.get('description', ''),
            'started_utc': start_utc,
            'finished_utc': finished_utc,
            'total_steps': len(steps),
            'completed_steps': len([r for r in step_results if 'error' not in r]),
            'error': error_msg,
            'step_results': step_results,
        }

        # Save report
        out_dir = self.session.config.out_dir
        if out_dir:
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
            p = out_dir / f'SCENARIO_{name}_{stamp}.json'
            with open(p, 'w', encoding='utf-8') as f:
                json.dump(report, f, indent=2, ensure_ascii=False)
            report['report_path'] = str(p)

        return report

    @staticmethod
    def build_doc58b_run02_scenario() -> Dict[str, Any]:
        """Constructs the canonical input scenario used in DOC-58B Run 02."""
        return {
            'name': 'doc58b_run02_movement',
            'description': 'DOC-58B Run 02 Host positioning (2x W 0.03s), client spawn wait, strafe A/D with handoff, mouse turn, and forward W.',
            'steps': [
                # 1. Focus host and position P1 slightly off P2
                {'action': 'focus', 'role': 'host'},
                {'action': 'hold', 'seconds': 0.5},
                {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.03},
                {'action': 'hold', 'seconds': 0.5},
                {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.03},
                {'action': 'hold', 'seconds': 1.0},
                # 2. Focus client for connection and native MapReady / Spawn
                {'action': 'focus', 'role': 'client'},
                {'action': 'hold', 'seconds': 2.0},
                # 3. Host movement sequence with strafe A and handoff
                {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.06},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.08, 'handoff': True},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.06, 'handoff': True},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.08, 'handoff': True},
                {'action': 'hold', 'seconds': 0.5},
                {'action': 'key', 'role': 'host', 'key': 'A', 'hold': 0.2},
                {'action': 'hold', 'seconds': 0.5},
                # 4. Strafe D and relative mouse turn
                {'action': 'key', 'role': 'host', 'key': 'D', 'hold': 0.12},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'D', 'hold': 0.06, 'handoff': True},
                {'action': 'mouse', 'role': 'host', 'dx': 120},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'D', 'hold': 0.08},
                {'action': 'hold', 'seconds': 0.5},
                # 5. Advance forward W
                {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.1},
                {'action': 'hold', 'seconds': 0.4},
                {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.15},
                {'action': 'hold', 'seconds': 0.5},
                {'action': 'key', 'role': 'host', 'key': 'W', 'hold': 0.3},
                {'action': 'hold', 'seconds': 1.0},
                # 6. Return focus to client
                {'action': 'focus', 'role': 'client'},
                {'action': 'hold', 'seconds': 1.0},
            ],
        }
