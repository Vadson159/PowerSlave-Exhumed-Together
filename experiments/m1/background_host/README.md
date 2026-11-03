# DOC53 background host simulation

Overall PARTIAL. Read [DOC53](../../../docs/53_HOST_BACKGROUND_SIMULATION.md).
One two-KEX scenario proved background READY→P2 spawn, W/S/Release and normal
OS exit without fake activation. Positive Attack/mouse-delta fixtures remain
unverified; old DOC52 terminal root cause remains UNKNOWN. No automatic next stage.

The changed native bootstrap leases only the proven GameInstance focus byte+319
around original StateTick. Original responder/events, Esc/inventory/TravelScreen
and other native gates remain in control. `MenuByte` / policy `.menu` / journal
`menu` are early analyst labels for raw+20c (TravelScreen), not ordinary Esc.
This is explicitly documented to prevent a repeat of Gemini's false assumption.

No production changes were made after the one runtime build was frozen.
The accepted-for-this-scenario Host artifact is
`docs/evidence/m1/HOST_BACKGROUND_53/artifacts/pse_m11_bootstrap.dll`, SHA256
e0c7c233ea7bc73a9fb85db88bfb3a33dad48cfa4125ece5fdbe92d6a71ae52d.
The Client DLL is the retained DOC51 MAIN4887fdda…def3.

`workflow.py build/tests/freeze/preflight`, `contract.cpp` and the timing-only
suite wrapper retain argv/hash/logs. Existing tests and production transport are
unchanged. Default timer suite44/46 and separate scoped1ms suite46/46 are both
retained; do not overwrite either outcome or call them the same conditions.

`runner.py` is deliberately bound to the completed N53_HOST_MAIN_01 and
N53_CLIENT_MAIN_01 names and refuses another launch over existing evidence.
It reuses the accepted launcher/guard/loader. The completed sequence was:

1. Fresh exact-build Host, guarded suspended launch, DLL bootstrap, actual gameplay.
2. Fresh retained Client, guarded launch/gameplay; before Permit prove Host's
   original singleplayer pause while actual Client foreground.
3. Real Host return, Host/Client mailbox permits; Host listener starts after the
   existing attestation. Host W held; real Client foreground → READY → background
   native P2 spawn. No Host focus request to pass this phase.
4. Client W/S with repeats/releases; verify actual foreground and native samples.
5. Real Host return/W, Esc, real Client foreground while Host Esc, return/Resume,
   Host S held and loss; background neutralization. Mouse fixture positive input
   failed and is not treated as a tested Attack hold.
6. Native WM_CLOSE Client, then Host; same retained OS handles must signal0/code0.
   Observers capture stacks/dump on delayed post-final signal before cleanup.

`foreground_fixture.py` affects only the two owned test windows. It temporarily
attaches helper input queues only when an ordinary real foreground request is
rejected and detaches in finally. No synthetic focus messages. Never ship it as
the mod's background-simulation mechanism. `ui53.py` posts test keys, not physical
keyboard input. All request/actual PID records are retained.

`watch.py` reuses DOC52 process-object/PSS APIs; no automatic game termination.
`review.py` is file-only and asserts37 consistency checks on the completed run.
It writes CREATE_NEW reports; do not rerun it over the final reports. `audit.py`
checks historical/original/copy/profile integrity. All25 REA sessions and both
observers were closed after genuine normal process exit.
