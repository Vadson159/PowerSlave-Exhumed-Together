# DOC-46: generation-scoped control routing

Transport-only remediation of DOC-45 P1-1. No KEX run, game DLL build/load,
world replication, admission policy or input-timer change. Read DOC-46 for
the measured before/after results and the exact acceptance boundary.

`workflow.py baseline` created the unique Evidence directory
`docs/evidence/m1/M12B_GENERATION_46`, froze the original sources and copied the
audit reproducer without changing its bytes. **Do not repeat baseline over an
existing run.** Before executables and source hashes remain available there.

Build and test the current code without touching either accepted game DLL:

```powershell
C:/Python314/python.exe -B experiments/m1/generation_routing/workflow.py build after routing_contract r1_live native_compile_check
C:/Python314/python.exe -B experiments/m1/generation_routing/workflow.py run after routing_contract r1_live
C:/Python314/python.exe -B experiments/m1/generation_routing/workflow.py build after pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance
C:/Python314/python.exe -B experiments/m1/generation_routing/workflow.py run after pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance
```

The harness creates a fresh folder for every command/run, retains full stdout,
exit status and a copy/hash of each executable. `native_compile_check` compiles
the unchanged native consumer as an object library, never as a game DLL.
`finalize.py` creates the current source/diff/integrity/results manifests by
reading files. It does not launch any process or replace old Evidence.

Original R1's verdict is evaluated after `Session::stop`, so its exit10 remains
even when the before-stop log says gen2 is connected/authenticated/ready. This
is retained as a known harness error, **not a product gate**. `r1_live` is the
same scenario with the predicate moved before cleanup; it fails10 on the frozen
before implementation and passes0 after the routing fix. The copied original
also contains the audit's R2 function; this task never executes it.

`routing_contract` covers five scenarios: real-ENet unicast/broadcast delivery
and stale/current KICK; authenticated MAP_READY retained through a full inbound
queue and FIFO across reconnect; stale client MAP_READY on a restarted worker;
Session ready/outbox generation capture under rejection; and unchanged wire
serialization. Its Session fixture takes the NETWORK role only after joining
the worker, uses one producer/consumer, and never calls KEX.

Unicast DTO generation0 is invalid; broadcast ignores generation. Producers
must capture the intended generation before enqueue. They may not fill it from
the current session when retrying an older message. Host MAP_READY generation
comes from the authenticated peer, because Wire v1.10 has no such field.

The accepted M1.2a DLL artifacts remain historical and do not contain this
source patch. A future authorized native stage must build a consistent combined
DLL before loading it. DOC-46 grants PASS only to this control-routing change;
it does not grant a new native-runtime or full M1.2b acceptance.
