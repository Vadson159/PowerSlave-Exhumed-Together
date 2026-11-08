# P1 runtime binding

`runtime_p1.py` binds one fresh two-process run to the unified `b60/` host/client
DLLs. It reuses the accepted M0 suspended launcher, isolation guard, DOC51 loader,
DOC53 retained-handle watcher and DOC58B ordinary OS-input fixture. Import and
syntax checks do not launch a game. No new injector or JS control adapter exists.

Run Python with `-X utf8 -B`. Launches, window actions and inputs use the existing
REA MCP Python-spawn lane on its Default desktop. In `start_frida_session.argv`,
omit argv[0]. Keep the launcher session alive until the owned game exits.

Before launch the parent must provide
the file selected by `runtime_p1.GATE_PATH` with `status: PASS` and a
nonempty `frozen` list of `{path, sha256}` entries including both unified DLLs.
Every launch/bind checks all frozen hashes and both copied EXE hashes. The pre-gate
must also cover the new observation ABI. The script does not grant approval or
substitute for runtime evidence.

The current `RUNS` are `P1_HOST_03` and `P1_CLIENT_03`, both completed. Before
another live run choose fresh names in `RUNS`, a new pre-native output and
`GATE_PATH`; never reuse completed names or profiles. Examples below use the
historical `_01` names to show the command shape; substitute the fresh pair.
Bind generates files under `docs/evidence/m1/PSE_TOGETHER_P1/<run>/` and fresh
profiles under `experiments/pse_together/runtime/profile/<run>/`.

## Order

1. Through REA spawn `runtime_p1.py launch host P1_HOST_01`. Once `launch.json`
   exists, run `bind host P1_HOST_01`. Attach the suspended owned KEX process,
   load **only** `generated_isolation.js` plus `load_native.js`, then resume the
   game through the accepted `action host P1_HOST_01 resume` helper. The generated
   `observer_bound.js` contains historical gameplay hooks and **must not load**.
2. Keep the retained-handle watcher running through REA:
   `watch host P1_HOST_01`. Use `input focus host` immediately before the accepted
   `action host P1_HOST_01 ui_start`; inspect its captures. The map screen needs
   a separately posted RETURN with actual foreground. Save REA status/export JSON
   into the run, then `gate host P1_HOST_01` to obtain `ISOLATION_GATE.json` and
   `permit_native.js`. Load the permit only after that PASS.
3. Before client join, prepare an attested host position with one short bounded
   W input and the read-only host-position witness. Run03 used one W0.03 from
   `[-7420,-6775,1152]` to `[-7420.13671875,-6809.43798828125,1152]`, sector259.
   Inspect actual position before any further input. The DOC58B one-way ledge
   still applies; native P2 placement work belongs to P4.
4. Repeat launch/bind/watch/isolation/gate for `client P1_CLIENT_01`. Do not load
   any `adapter58b.js`, controller JS or `NativeCallback` registration. The native
   DLL owns the proxy seam. Client `permit_native.js` starts its accepted worker;
   `permit_proxy_spawn.js` separately publishes the native spawn-permit atomic.
5. Before spawn, `observer client P1_CLIENT_01 snapshot BEFORE` generates a
   read-only P1 snapshot observer. Load while the client is foreground; wait for
   `p1_state_snapshot` with status OK. Export its evidence, then unload this
   observer before loading a second observer on the same scene site.
6. Permit spawn; confirm native `proxy_spawned` and native MapReady in the DLL log.
   `observer client P1_CLIENT_01 render LIVE` generates the renderer observer.
   Load it after the snapshot observer has unloaded and restored scene bytes.
   Ordinary host strafe/mouse inputs with foreground handoff exercise snapshots;
   the client applies/observes only while foreground. Capture the owned client
   window when its proxy is in the actual frustum.
7. `observer_p1.js` calls `PSE_P1_ReadObservation(out,128)` on the attested
   Sim/render TID. `PSE_P1_IsPositionInterpolant(candidate)` first filters lerp
   callbacks using private numeric identity comparison with no candidate
   dereference. Sprite callbacks first compare the frame resource token cached
   at scene entry. The full DTO is requested only for candidate proxy work.
   The owned pair comes from the completed native `proxy_spawned` journal row;
   render binding requires exactly one Spawn and no later retirement event.
   The observer never dereferences the owned Actor. With a fresh Live DTO on
   the same Sim TID it validates the owned node and completely walks sector259
   with reciprocal/sentinel/4096 bounds, reading other Actor frame/render
   fields to exclude same-frame same-origin candidates. Any missing witness
   gives UNKNOWN, another match gives AMBIGUOUS; only complete unique identity
   gives OURS. The permanent observer hooks scene, sprite and lerp, no vertex.
   It can remain loaded through disconnect: non-Live/invalid DTOs stop all proxy
   sampling. `ERROR_INVALID_STATE` (5023) returns a filled invalid DTO; its phase
   is recorded and counted as normal invalidation, with no observer error.
   Lerp evidence records own output changes during one synchronous
   native lerp; sprite evidence compares frame resource token, exact output
   float bits, angles, sector, flags and scale, with the bounded uniqueness
   census. Every two seconds a summary records counts, including ambiguity and
   unknown identity. After invalidation the census is never called.
8. Close host via owned-window WM_CLOSE. Leave the client foreground and observe
   native disconnect → one Remove → pending → PoolReturn → node retirement.
   The renderer observer continues producing P1 field snapshots/summary but no
   proxy samples after native identity invalidates. Export evidence. Unload it
   before loading any final `snapshot AFTER_RETIREMENT` observer on scene.
9. Load `request_detach.js` only once native proxies are quiescent; confirm seam
   status 3 (`status.js`). Unload remaining observers/isolation only at the
   accepted shutdown stage. Close the client with WM_CLOSE, collect retained and
   duplicate handle exit evidence, launcher `exit.json` and protected-after
   snapshots. Close all REA sessions. The parent then runs original-game/save
   integrity and exact restoration checks.

`runtime_p1.py input` accepts the same bounded inputs as DOC58B:
`focus host|client`, `key W|S|A|D <seconds<=0.4> [handoff]`, `mouse <abs(dx)<=400>`,
and `hold <seconds<=30>`. They address validated owned HWNDs; synthetic activation
messages are rejected. The accepted foreground fixture joins queues only when
needed and detaches them in `finally`. If that accepted request still fails,
`focus_p1.py` raises the owned window without activation and issues one actual
left click at a caption point. It verifies caption style, target PID and the
top-level window at that point, repeats PID/root checks after moving the cursor,
and requires native `WM_NCHITTEST` to return `HTCAPTION` before mouse-down.
It restores the cursor after the attempt.
`physical W|S|A|D <seconds<=0.4>` is a separately recorded OS keyboard fallback;
run03 used the accepted posted-key path. `capture_client_view_p1.py` performs
bounded ordinary client mouse-look with actual foreground checks before each
event and then uses the accepted owned-window screenshot helper. It never
writes game memory; renderer/lerp records remain the acceptance evidence.
This fallback was added for P1 after the desktop search process retained focus;
it changes the stand input path, not either frozen native DLL or game memory.

All phase/action details and claims remain runtime-dependent. A generated script,
syntax PASS or loader exit 0 is not a successful P1 runtime acceptance.

Current results, failures and the REVIEW boundary: [DOC-61](../../../docs/61_P1_NATIVE_PROXY_DLL.md).
