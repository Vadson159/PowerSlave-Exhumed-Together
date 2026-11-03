# DOC-49 P2-2 cadence remediation

Read `docs/49_M12B_INPUT_CADENCE_REMEDIATION.md` first. Overall PARTIAL:
the actual headless scheduler is fixed; production default remains6 ticks OR150ms
because widening had no benefit in the corrected-client matrix. LAN/VPN untested.

One authorized native run completed: `M12B_CADENCE_NATIVE_01`. Do not repeat it
automatically, reuse historical PID bindings or expand to another game DLL.

Production changes are limited to `network_input/src/native_client.cpp`,
`network_input.cpp` and `network_input.hpp`. Existing bootstrap/Worker/Bridge,
admission/routing, ENet policy and wire1.10 are unchanged.

Reproduction components:

- `workflow.py`: before/source snapshots, fresh build, unchanged regression suites,
  R1/R2, routing/admission, timeout contracts and original/save integrity.
- `generate_qa.py`, `qa_enet/`, `telemetry.cpp`: isolated ENet accounting copies.
  Production DLL/client do not link these wrappers. `PSE_QA_FLUSH` affects only
  QA executables, never the production transport.
- `matrix.py`: real before/after client processes, raw UDP relay, seeds and stages.
  Counter units distinguish commands/datagrams; the parser's forward offset is32.
- `cadence_host.cpp`: actual Session/Worker/Bridge plus three same-stream candidate
  predicates. `timeout_contract.cpp` tests actual Sessions for all three policies.
- `analyze.py`, `functional_review.py parserfix_`: offline analysis. Initial failed
  or incomplete runs remain in Evidence; scripts do not turn them into PASS.
- `native_runner.py`, `bind_native_probe.py`, `native_client_control.py`,
  `os_snapshot.ps1`: accepted isolated launcher and unchanged DOC48 passive probe,
  fresh PID/profile/DLL binding, bounded Hold/Release/Silence and OS teardown.
- `native_review.py`, `finalize.py`: file-only native consistency and manifests.

Offline review of completed Evidence, without a game/client/test launch:

```powershell
C:/Python314/python.exe -B experiments/m1/input_cadence/analyze.py
C:/Python314/python.exe -B experiments/m1/input_cadence/functional_review.py parserfix_
C:/Python314/python.exe -B experiments/m1/input_cadence/native_review.py
```

Autonomous test modes were `smoke`, `matrix`, `functional`, `flush`, `stock`.
Existing completed network runs are reused; errors are retained under fresh
names after an actual condition change. No historical tests/assertions changed.
R2 requires argument `r2`. A new process without its former reconnect secret is
expected to be rejected by the reserved host ledger, preserving DOC47 policy.

All current work is complete. Further native runs or P2-2 channel calibration
need a new task; full M1.2b remains outside scope.
