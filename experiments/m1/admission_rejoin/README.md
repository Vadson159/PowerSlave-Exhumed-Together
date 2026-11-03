# DOC-47 admission/rejoin

Transport-only fix of DOC-45 P2-1. No KEX launch, native hook change or game DLL
build/load. Historical artifacts are read-only. The unique evidence directory is
`docs/evidence/m1/M12B_ADMISSION_47`; baseline must not be repeated over it.

`maxClients` limits simultaneous ENet transport peers. `maxClientSlots` bounds
the admissible reserved slot range [1..N] separately, default7. The current
Session sets both to1. Participant identities/keys remain reserved until a fresh
host session ledger is created; dynamic participant rotation is not implemented.

The current Session also opts into `rejectedPeerTimeoutMs=250`. REJECT is sent
using existing v1.10 and disconnect_later; the host retires an unauthenticated
rejected peer when that deadline expires even without ACK. Rejected connections
cannot submit another HELLO or input before cleanup. No slot/generation was
committed to such a peer, so expiration retires only its transport entry. Existing
authenticated generation-scoped actions from DOC-46 are untouched. A deadline
requires the host's poll loop to keep running; it is not a flood/fairness policy.

```powershell
C:/Python314/python.exe -B experiments/m1/admission_rejoin/workflow.py build after r2_original r2_fixed admission_contract r1_live routing_contract
C:/Python314/python.exe -B experiments/m1/admission_rejoin/workflow.py run after r2_original r2_fixed admission_contract r1_live routing_contract
C:/Python314/python.exe -B experiments/m1/admission_rejoin/workflow.py build after pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance
C:/Python314/python.exe -B experiments/m1/admission_rejoin/workflow.py run after pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance
```

Each run has its own CWD, full stdout, exit code and executable/hash. The workflow
passes `r2` to both R2 executables. `r2_original.cpp` is byte-identical to Opus's
reproducer; its dormant R1 function is not used. `r2_fixed.cpp` adds a positive
before-cleanup acceptance predicate: rejected beta is disconnected and alpha is
authenticated/ready in slot1/gen2. `admission_contract` tests eight reject/rejoin
cycles, invalid keys, registry non-mutation, input neutralization, opt-in/default
policy and a rejection that is not ACKed. DOC-46 tests are reused unchanged.

`finalize.py` packages source/diff/integrity/results by reading saved files, with
no gameplay execution. PASS is only for P2-1. The historical game DLL artifacts
do not contain this patch; a future authorized native stage needs a consistent
fresh combined build. No P2-2…P2-5 or full M1.2b work is authorized by this file.
