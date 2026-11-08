"""Reusable Two-Process Test Harness for PowerSlave Exhumed Together (S0.5 / Package P2).

Provides unified orchestration for two-process (Host + Client) runtime experiments:
- Launcher & Isolation management (CREATE_SUSPENDED, clean profiles, native DLL bootstrap)
- Robust Window Focus & Foreground activation (Win32 with AttachThreadInput fallback)
- Scriptable & Declarative Input Scenarios (posted keys, mouse, hold, handoff)
- Generic REA Evidence Extraction (scripts, session status)
- Comprehensive Evidence Review & Contract Gate Verification
"""

from .session import TwoProcessConfig, TwoProcessSession
from .window_manager import WindowManager
from .input_controller import InputController
from .scenario_runner import ScenarioRunner
from .rea_extractor import ReaExtractor
from .evidence_reviewer import EvidenceReviewer

__all__ = [
    'TwoProcessConfig',
    'TwoProcessSession',
    'WindowManager',
    'InputController',
    'ScenarioRunner',
    'ReaExtractor',
    'EvidenceReviewer',
]
