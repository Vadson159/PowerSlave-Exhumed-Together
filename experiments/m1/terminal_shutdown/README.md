# DOC52 external terminal diagnostics

Overall PARTIAL, old root cause UNKNOWN. One diagnostic two-KEX scenario exited
normally with unchanged DOC51 DLLs. No production fix and no verification rerun.
Read [DOC52](../../../docs/52_HOST_TERMINAL_SHUTDOWN_INVESTIGATION.md).

`runner.py` is bound to completed run names and refuses another launch over their
evidence. Do not invoke it as an automatic reproduction loop. No next-stage work
is authorized by this README. Historical DOC40–51 files remain immutable.

`observer.py` retains process/duplicate handles from before resume through exit,
validates PID/path/creation time and compares kernel object identity. Wait0 is
required independently of exit code. No automatic game termination exists.

`snapshot.cpp` is an external PSS thread-context/WCT/StackWalk64 diagnostic. It
uses local symbols only, does not write target memory or explicitly suspend
threads. PSS can affect scheduling. Stack words are candidates, not frames;
nearest-export labels need interpretation. Capture may fail during termination.
Its dump branch was not exercised in the accepted scenario. The wrapper bounds
the helper at25s, ending only the helper on timeout.

`terminal_probe.js` uses native fixed-slot listeners and no JS terminal callbacks.
The external250ms memory poll is intended for a prolonged hang; it missed fast
normal exit (RPM299 after reclaim). Do not claim terminal API entry from zero
slots or compile-only fixture. A later investigation needs durable terminal
checkpoints before running another game scenario.

`audit.py` hashes historical files, game copies/original/protected profiles,
extracts DOC51 shutdown and compares DOC48/51 source hashes. `review.py` checks
the completed scenario, writes CREATE_NEW reports, and does not attach/run games.
It refuses to overwrite existing reports. Frozen executed harness and manifests
are under `docs/evidence/m1/HOST_TERMINAL_52/`.

Build diagnostics only (does not launch the game):

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' -S experiments/m1/terminal_shutdown -B experiments/m1/terminal_shutdown/build -G 'Visual Studio 17 2022' -A x64 -DCMAKE_SYSTEM_VERSION=10.0.26100.0
& 'C:/Program Files/CMake/bin/cmake.exe' --build experiments/m1/terminal_shutdown/build --config Release
```

Do not overwrite frozen artifacts or completed fixture reports. Existing tests
were run against disposable Python processes: active259/wait258, exit37/wait0,
wrong path/creation rejection, successful PSS/WCT capture/free. These do not prove
the cause of DOC51's hang.
