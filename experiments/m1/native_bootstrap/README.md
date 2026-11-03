# M1.1 native bootstrap

Exact-build Windows x64 C++20 experiment; no ENet or Thread Bridge dependency.
Entry addresses and owned Player defaults come from accepted M0 evidence, recorded
by `generate_profile.py`. This directory contains adapter code, not KEX source.

Build with installed VS2022 BuildTools / SDK10.0.26100.0:

```powershell
C:/Python314/python.exe -B experiments/m1/native_bootstrap/build.py
C:/Python314/python.exe -B experiments/m1/native_bootstrap/runner.py preflight
```

For a new authorized diagnostic run, use the REA provider to spawn Python with
`runner.py launch <NEW_RUN>`, then resume the provider parent. The verified copy
is `experiments/m0/game`, with a fresh CWD/profile under this directory. Bind with
`runner.py bind <NEW_RUN>`. Attach to that launch's PID, load only
`generated_isolation.js` and `load_native.js`, then run the owned `resume` helper.
`observer_bound.js` is generated solely to reuse M0's OS-isolation binding; it is
never loaded. Keep the isolation script alive until the process has terminated.

`M11Prepare` validates target path, exact EXE SHA, DLL path and log path, pins the
DLL, and queues a same-process APC on the suspended primary thread. `DllMain`
only records its HMODULE. The APC installs exact-prefix-checked MinHook detours;
the real native tick must independently attest the same simulation TID before
any Player call. No native KEX call originates on the loader thread.

Save a current full Frida status after real gameplay begins. `runner.py gate
<NEW_RUN>` requires the native P1-update witness and a complete bounded isolation
ledger, then creates `permit_native.js`. Loading that file writes only an atomic
mailbox. The DLL consumes it on its own tick, waits60 actual P1 updates, and runs
one P2 sequence:45 backward,15 neutral,45 forward,15 neutral updates. P1 Update
continues through its original function; only P2 command/turning seams synthesize
input. Native actor traversal performs Puppet physics. Scheduling stops after120
P2 updates. Local input, identity, health, inventory and weapon state are compared
immediately before/after each P2 Update.

Use the native menu for the selected exit scenario. Before StateExit world
teardown, or before native shutdown if still bound, the DLL disables/restores its
control hooks and performs at most one native Reset. StateExit retains the
original teardown and observes owned Actor destructor return without reading the
destroyed Actor. Direct Quit retains all callback code/storage/trampolines until
process exit; it never forces actor teardown. This DLL intentionally remains
pinned even after StateExit. There is no hot-unload export.

Raw output, frozen DLL/PDB/source, startup failures, the isolation gate and final
review live in `docs/evidence/m1/`. Read `docs/41_M1_NATIVE_BOOTSTRAP.md` for actual
acceptance, rather than inferring success from a build or exit0.

Limits: one exact EXE, one additional Player, one controlled sequence, diagnostic
Frida OS isolation/loader, no production input queue, network, save/transition
policy, respawn, achievements, or arbitrary unload. The native thirteen-button
polling loop still runs for P2; P2 skips the consuming command/turning builders.
Bounded P1 input preservation is checked at runtime; broad hardware-input and
asynchronous exception/unwind behavior are outside this experiment.

MinHook v1.3.4 is vendored unchanged from its official tag. Its BSD license is
retained in `third_party/minhook/LICENSE.txt`; archive/source hashes are in the
implementation Evidence dependency manifest.
