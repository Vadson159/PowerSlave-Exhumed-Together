# P7 wire v1.12

An isolated fork of the accepted identity-only v1.11 sources. `BASELINE.json`
records the original 32 source digests. Neither v1.10 nor v1.11 is modified.
No mixed-version negotiation or downgrade exists. The existing native map
name/structure handshake is retained; world replication is not enabled here.

The existing 65-byte `PlayerStateSnapshot` record is followed by 56 explicit
little-endian bytes. Each complete player record is **121 bytes**. With the
11-byte message header and 13-byte snapshot prefix, one player is **145 bytes**,
two players are **266 bytes**, and eight players are **992 bytes**.

| Record offset | Scalar |
|---|---|
| 0..64 | Existing slot/entity/transform/health/weapon/life/buttons/death fields |
| 65 | `ammoByWeapon`: eight uint16 values, 16 bytes |
| 81 | `keys`: uint16 |
| 83 | `artifacts`: uint32 |
| 87 | `abilities`: uint32 |
| 91 | `questItems`: uint16 |
| 93 | `ankhCount`: uint16 |
| 95 | `ankhMask`: uint16 |
| 97 | `teamDolls`: uint32 |
| 101 | `pendingWeapon`: int32, -1 or owned slot 0..7 |
| 105 | `stateTimeNs`: uint64, post-world-step monotonic host clock |
| 113 | `resetToken`: nonzero uint32, opaque |
| 117 | `discontinuitySequence`: nonzero uint32 |

`ammo` remains a checked mirror of `ammoByWeapon[activeWeapon]`. Health must fit
native int16; alive requires positive health, dead requires nonpositive health
and a nonzero death event sequence. DYING/RESPAWNING wire states are not admitted
by this stock P7 contract. Key bits are limited to 0x0f, artifacts to 0x3f,
abilities to 3 and quest items to 0xff. There are five unique ankhs: count <= 5,
mask <= 0x1f and count equals mask population. This rejects duplicate-count
corruption. Native maximum-health and per-weapon ammo capacities remain native
apply gates, not inferred codec limits.

Each record carries its timestamp and reset/discontinuity values, and bridge
conversion preserves every scalar. Network workers never read native objects.
The host worker rejects invalid native records before send; the client checks
the exact version/channel, EOF, epoch, packet sequence and record identities.
Presentation/history/time-sync/reset-ACK semantics are not supplied by a codec.

Build integration is in `../../cmake/p7_wire112.cmake`; the real ENet two-worker
contract is `tests/native/p7_wire112_contract.cpp`. Those checks establish
transport behavior only, not KEX/HUD/native-apply runtime acceptance.
