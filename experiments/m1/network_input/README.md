# M1.2a native network input

One exact-build DLL combines the accepted M1.1 native bootstrap and DOC-40
ENet Worker/Thread Bridge. One headless loopback process drives slot1/P2;
P1 retains the original native input path. This is an experimental control
integration, with no client game/renderer or replicated KEX world.

Read `docs/44_M1_FIRST_NETWORK_INPUT.md` and both retained runtime summaries
before another run. Native EXE SHA256 is fixed in the reused build profile.
Do not use historical PIDs, generated JS bindings or profiles for a new run.

Build and file-only verification:

```powershell
C:/Python314/python.exe -B experiments/m1/network_input/build.py
C:/Python314/python.exe -B experiments/m1/network_input/runner.py preflight
C:/Python314/python.exe -B experiments/m1/network_input/review.py
```

`m12a_generation_test`, `m12a_adapter_test` and `m12a_pressure_test` are separate
transport tests with real loopback ENet peers and no KEX. Build the last target
explicitly with `build.py m12a_pressure_test`. Original DOC-40 suites build with
`build.py pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance`
and execute through `regression.py`, which retains raw output and exit codes.

For an authorized new native run, use the existing REA Default-desktop provider
to spawn `C:/Python314/python.exe`, with argv starting **`-B`**, followed by this
directory's absolute `runner.py`, `launch`, and a fresh run name. Do not repeat
the executable as argv[0]; this provider adds it. Resume that helper, wait for
`launch.json`, then `runner.py bind <RUN>` from the workspace. Attach to the new
game PID, load its `generated_isolation.js` and `load_native.js`, and execute
`runner.py action <RUN> resume` through a provider helper. The wrapper reuses
M0 isolation and M1.1 loader without editing them or calling KEX from Python/JS.

Wait for the visible title window. Use the owned-HWND helpers to start a fresh
game and inspect each capture. Save full provider status, then run
`runner.py gate <RUN>`. Only a current isolation PASS plus a native P1 tick
witness permits loading that run's `permit_native.js`. After60 further native
P1 updates the DLL opens127.0.0.1:28745 (one peer, token M12A_Loopback).

Start `control.py start <RUN>` as a separate process. It waits for the headless
client to finish and retains console, process ID, command journal and exit code.
The client waits for the empty control baseline before sending inputs. Commands
from `control.py hold/neutral/silent/disconnect/reconnect/quit <RUN>` select a
mode. `control.py sequence <RUN>` runs the bounded Hold→Release→Hold→Silence→
Hold→Disconnect→Reconnect sequence. `silent` keeps polling ENet but sends no
input, so it tests lost release/input expiry without transport disconnection.
`quit` explicitly closes the client; it also has a180s diagnostic lifetime cap.

`network_os_probe.js` observes OS socket ownership/close. `join_os_probe.js`
observes OS waits on thread handles and exit codes. They contain no KEX hook or
game-memory access. Save full status before and after the native exit scenario;
keep the isolation script attached until game termination. StateExit and live-P2
Direct Quit are different scenarios and have separate retained evidence.

The DLL maps only forward/backward to the proven native digital seam. It ignores
other wire buttons, strafe, look and weapon selection. Wire mask0x04 means
USE and must never be copied into native mask0x04 (forward). Timeout is6 simulation
ticks, with a150ms wall-time backstop. It clears input, not native velocity;
KEX inertia/collision can continue after Release. Disconnect preserves the one
native P2 for neutral simulation and same-participant reconnect, without Spawn.
Native retirement occurs through the accepted StateExit/Direct Quit path.

The MAP_CHANGE/baseline name `M12A_INPUT_ONLY` labels this headless diagnostic
control domain. It does not claim that a remote KEX level loaded or that an empty
baseline describes the host's real actors. Real map names/transitions and world
replication are deferred. Do not interpret this control client as a game client.

Stop joins the worker before native unbind, records terminal cancellation/loss,
unregisters roles and clears queues only after both owners stopped. Session,
Player storage, code and MinHook trampolines remain retained until process exit.
There is no DLL unload, manual refcount repair or forced Puppet destructor.

Current task ends at M1.2a. No M1.2b work is authorized by this README.
