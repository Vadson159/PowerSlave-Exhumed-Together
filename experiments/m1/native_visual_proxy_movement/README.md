# DOC-58A completion (N58A_C01 + N58A_C02)

Result: bounded PASS. C01: CROSS_SECTOR/LIFECYCLE/SAME_SECTOR PASS, INTERPOLATION PARTIAL. C02 (user-approved second process, `runner.RUN='N58A_C02'`, gate in `COMPLETION/GATE_N58A_C02`, `review_interpolation.py`): INTERPOLATION PASS with the fixed command 4. See `docs/58A_NATIVE_VISUAL_PROXY_COMPLETION.md`. `workflow.OUT` now points to `docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION`; `runner.py` only accepts run `N58A_C01` (already used — a new run needs a new name and permission). `cross_sector.js` holds the pure attestation/membership/sprite-identity checks shared by the probe and `offline_test.js`. `review.py` is the historical N58A_01 review and is not re-runnable against the new OUT.

Completion commands (from the S: root, Python `-X utf8 -B`): `workflow.py baseline`, `precheck.py`, REA spawn `runner.py launch N58A_C01` + resume, `runner.py bind|action resume|action ui_start|gate`, mailbox `command_{1,3,5,7,2}.js`, `runner.py watch`, `rea_extract.py`, `review_completion.py`, `finalize_completion.py postrun|changed`, `workflow.py integrity`. Command 4 was not sent in N58A_C01; its fix was executed in N58A_C02 (mailbox 1,3,4,5,2).

# DOC-58A bounded movement experiment

Result: PARTIAL. See `docs/58A_NATIVE_VISUAL_PROXY_MOVEMENT.md` and current-run Evidence. DOC-58B is not implemented.

`controller.js` owns status/once-only/thread/lifetime guards. `movement_native.js` is the private exact-build adapter. It permits four geometry/frustum-approved positions for one stock Actor1007, normalized yaw, native interpolant reset, and same-sector LinkSector. Other destination sectors are rejected. The API does not expose raw Actor pointers or writes to callers.

`one_actor.js`, inventory/camera/common helpers were copied from accepted DOC57 into this lab, preserving the originals. `runner.py` reuses the accepted suspended launcher, isolation and OS observer; it deliberately refuses another run name or reusing `N58A_01`. `navigate.py` extends the existing owned-window input fixture with normal keys and bounded relative mouse input; it is not an injector.

All commands must run from the S: project root, with TEMP/TMP set to this lab's scratch and Python `-B`. Existing saved evidence is exclusive-create and must not be overwritten.

Executed commands:

```text
C:/Python314/python.exe -X utf8 -B experiments/m1/native_visual_proxy_movement/workflow.py baseline
C:/Python314/python.exe -X utf8 -B experiments/m1/native_visual_proxy_movement/focused_ghidra.py function <focused addresses> --output m1/NATIVE_VISUAL_PROXY_58A/static
C:/Python314/python.exe -X utf8 -B experiments/m1/native_visual_proxy_movement/precheck.py
```

Native launch/actions ran through existing REA MCP Frida spawn/resume of installed Python, then attach to the single owned KEX PID. Initial bound probe never spawned; user input invalidated its gate. The active native version is `N58A_01/probe_bound_v2.js`, with exact hashes in `REBIND_BEFORE_SPAWN.json`. Mailboxes modify only allocated Frida command memory. KEX writes/calls execute on the attested Simulation Thread. Current `controller.js` additionally catches adapter errors into explicit statuses; that final guard hardening has offline verification only.

Final controlling-logic verification (19 cases, no native claim):

```text
C:/Program Files/nodejs/node.exe experiments/m1/native_visual_proxy_movement/offline_test.js
C:/Python314/python.exe -X utf8 -B experiments/m1/native_visual_proxy_movement/review.py
C:/Python314/python.exe -X utf8 -B experiments/m1/native_visual_proxy_movement/workflow.py integrity
```

Review already created immutable outputs; a repeat requires a separate output directory in a separately authorized follow-up. `workflow.syntax()` and `workflow.command()` retain argv/cwd/exit code/stdout. Network regressions were not rerun because production/ENet/protocol were unchanged. Original/saves and all DOC53/55/56/57 evidence remain protected.
