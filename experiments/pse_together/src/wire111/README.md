# P4 identity-only wire v1.11

User approval: 2026-11-04, P4 remediation chat. This separately compiled fork
leaves `tools/enet-prototype` and its wire v1.10 byte-identical. `BASELINE.json`
records the source hashes before the fork; only `pse_core_p4` uses these files.
Other targets keep `pse_core` and v1.10. Standalone/world mock use of this fork is
outside P4: only the native host/client and identity contract are exercised.

Header version is 0x0111. Welcome and MapChange append native PSM version (u8),
countA (u64 LE), countB (u64 LE) after mapName. MapReady appends mapName (u16 LE
length, 1..32 printable bytes, no quote or backslash) and that structural triple
after slot/epoch. Names never silently truncate; truncation, trailing payload,
zero counts and mixed header versions are refused. Welcome payload version must
also match. Connection generation is assigned from the authenticated peer.

The simulation compares the current peer's name and all three structural values
before promotion/baseline/input/snapshot eligibility. A mismatch revokes that
peer until reconnect; a foreign generation cannot revoke the current one.
The native client checks the same fields in Welcome and MapChange.

There is no downgrade, content hash, difficulty, manifest, sectorIndex or input
acknowledgement extension. Those P6 changes still need separate decisions.
