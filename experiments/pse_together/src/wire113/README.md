# Isolated v1.13 candidate

User decision: [DECISION_V113.md](../../../../docs/evidence/m1/P23_UI/DECISION_V113.md).
Protocol/UI contract: [../ui/README.md](../ui/README.md).

Headers were copied from v1.12 so quoted includes never resolve back into a
frozen protocol. `net_protocol.cpp`, `enet_host.cpp`, `enet_client.cpp` are
isolated source copies. The native `enet_worker.cpp` binding is also isolated;
remaining `.cpp` inputs are inherited unchanged from
`../wire112/src`. CMake uses this include directory exclusively for pse_core_p23.
Do not link pse_core_p23 together with other pse_core variants in one binary.

Only `pse_dll_host_p23` / `pse_dll_client_p23` select this target. Other accepted
native variants retain their protocols. Chat/roster use reliable channel0;
host-only UI pose0x63 uses unreliable sequenced channel1, with native epoch,
generation, order and receiver-local TTL. Remote QPC values are never compared
to client QPC. End-to-end network delay is not measured by this TTL.
Standalone sockets/codec tests are not KEX gameplay acceptance.
Frozen protocol/artifact comparison is recorded in
`docs/evidence/m1/P23_UI/FINAL_CHECKS02/SOURCE_CHECKS.json`.
