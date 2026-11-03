# DOC-48 Window Close preflight

One authorized native run completed: `M12B_WINDOW_CLOSE_01`, bounded PASS.
Read `docs/48_M12B_WINDOW_CLOSE_SHUTDOWN.md` and its raw Evidence first.
No production source changes. P2-2 remains NOT FIXED.

`CMakeLists.txt` builds current network/native sources into a separate tree;
the accepted DOC-44 DLLs are never overwritten. R1/R2 sources are reused unchanged.
`r2_fixed.exe` must be invoked with argument `r2`; without it the historical
audit source selects its old R1 branch and post-stop verdict.

Files:

- `workflow.py`: baseline, build/source manifest, autonomous suites, file integrity.
- `runner.py`: accepted M0/M1 launcher/guard/loader, fresh profile/build selection.
- `bind_probe.py`, `close_probe.js`: exact-prefix passive WM_CLOSE/SDL/KEX route,
  socket I/O/close and real Worker wait/exit evidence. Load after native bootstrap
  hooks, before network Permit. No replacements, KEX calls or game-memory writes.
- `client.py`: starts the unchanged freshly built headless client, records exit.
- `os_snapshot.ps1`: independent process/thread/UDP snapshots.
- `static_review.py`: exact PE coordinates and primary SDL source references.
- `review.py`: file-only audit, re-runnable without game, client or transport tests.

Offline reproduction of the completed result:

```powershell
C:/Python314/python.exe -B experiments/m1/window_close/review.py
```

The single-game allowance has been consumed. Do not automatically repeat launch,
reuse the old PID/bound probe, start P2-2 remediation or expand to full M1.2b.
Future native work needs its own task and a fresh run/profile/PID binding.
