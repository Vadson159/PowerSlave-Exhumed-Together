# Native second-instance input PoC (DOC51)

Overall **PARTIAL**. Real KEX Client W/S → existing ENet → Host native P2 movement
was observed. Client exited normally. Host joined its Worker and reset native P2,
then its final process wait remained unsignaled despite exit-code query0; external
OS cleanup was required. Cause unknown. Do not repeat native runs automatically.

Read [DOC51](../../../docs/51_NATIVE_SECOND_INSTANCE_INPUT_POC.md) and its final
summary before further work. Main PID bindings are historical and must not be reused.
The main proof also needed explicit synthetic Host HWND activation while Client
remained actual foreground. `cl_engineNoSleep 1` alone did not keep Player Update
active. This is a diagnostic fixture, not a finished background-window policy.

`src/client.cpp` is a passive exact-build adapter, not KEX source. It uses the
existing Tick/Update/lifecycle seams, creates no external Player, and publishes
scalar samples on the actual KEX simulation thread. Opt-in CLIENT Worker cadence
creates60Hz commands from the latest fresh sample; stale/invalid means repeated
neutral. Existing host hooks, admission, timeout, wire1.10 and DOC50 are preserved.

File-only reproduction uses installed CMake/VS2022:

```powershell
C:/Python314/python.exe -B experiments/m1/native_second_instance/workflow.py build
C:/Python314/python.exe -B experiments/m1/native_second_instance/workflow.py tests
C:/Python314/python.exe -B experiments/m1/native_second_instance/workflow.py preflight
```

Commands retain new timestamped logs under DOC51 Evidence. `tests` writes a
CREATE_NEW summary and will refuse to overwrite the completed one; use a fresh
evidence directory in a separately scoped task. `freeze`, `copy`, `baseline`,
`review` and run output names also refuse to replace accepted artifacts/results.

`runner.py` reuses the unchanged M0 isolated launcher/path guard and native loader
with per-run build manifests. Only the existing REA Default-desktop provider was
used for launch/window helpers. Loader work stays outside loader lock; DLL is
pinned, no FreeLibrary/hot unload. `input_runner.py`/`ui_hold.py` are explicit
synthetic Windows tests, not the client input implementation. They call no KEX.

Actual artifacts: `artifacts/pse_m11_bootstrap.dll` (fresh Host),
`artifacts_MAIN/pse_native_client.dll` (main Client). Initial smoke Client and its
source snapshot remain separate. Frozen source manifests and final harness are
under `docs/evidence/m1/NATIVE_SECOND_INSTANCE_51/`; do not replace them with a new
build. Game copies and original/user profiles remain read-only outside new test
profiles. No world synchronization or real co-op acceptance is claimed.
