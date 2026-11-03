# DOC-50: P2-5 sequence hardening

Only transport/Session autonomous tests; never launches KEX. Sources after DOC-49,
pristine vendored ENet, MSVC Release x64 C++20 /MT, SDK 10.0.26100.0.

From workspace root:

```powershell
python experiments/m1/input_sequence/workflow.py build after
python experiments/m1/input_sequence/workflow.py run after
python experiments/m1/input_sequence/workflow.py integrity
```

`baseline` is a one-time operation, already performed before any product edits.
It must not overwrite the existing evidence directory. Commands keep stdout,
argv, exit, executable SHA256 in unique run folders. Build snapshots are unique.
`run before` invokes the preserved before binaries with an expected-defect
predicate over actual state, not an expected PASS string. Do not rebuild the
before targets against today's product sources. For an independent before
rebuild configure `docs/evidence/m1/M12B_SEQUENCE_50/before/experiments/m1/input_sequence`
as the CMake source into a new build directory inside this lab. That frozen tree
contains the exact pre-edit Worker/Session and inspection sources; its include
tree has no `input_sequence.h`.

`sequence_contract` compiles actual unchanged implementation files in a test TU
with private access enabled (including method definitions for MSVC decoration).
The single-thread fixture uses real ENet authentication, original Worker handlers,
callbacks, mailbox and SPSC/Session. It runs both roles on its sole owner thread
with affinity checks enabled, permitting race-free exact mailbox inspection.
The deterministic packet matrix uses reliable packets on the INPUT channel so
duplicates/stale commands reach the application filter. This is a test delivery
choice, not a production channel change. High water marks near UINT32_MAX are
explicitly seeded into owned C++ test objects; no game memory is present.

`sequence_live` links normal libraries, starts the real background Worker and
uses ordinary unreliable INPUT through ENet. A fixed 60 Hz application generator
omits selected payloads, delays others 2–4 slots, duplicates/reorders payloads,
and reconnects with the actual secret. These are application payload impairments,
not measurements of physical UDP datagram loss or a LAN/VPN calibration. It
checks delivered sequence advancement in every second, actual Session acceptance,
no unexpected disconnect and no queue pressure escalation. All raw Session
acceptances are retained. Existing test assertions are unchanged.

The 1901-command mailbox test models a long simulation pause without waiting or
filling the bridge: every command passes the Worker before the merged state is
flushed once. Session must not apply the per-wire-packet bound to that merged DTO.

Native consumer is compiled as `native_compile_check` object only (/W4 /WX).
No game DLL is built, replaced, loaded or runtime-accepted here.
