# PowerSlave Exhumed Together — M1.2a Test Client

Standalone Windows x64 CLI test harness for M1.2a integration verification (ENet client <-> native host DLL / mock host).

---

## 1. Overview & Purpose

The **M1.2a Test Client** (`m12a_test_client.exe`) is an independent QA and diagnostic tool designed to verify network transport, handshake, input streaming, and reconnection contracts under **Wire Protocol v1.10** (`0x0110`, Magic: `0x50534558`).

It serves as the test companion for **GPT-6.1 Sol Max** when connecting ENet to the native host DLL in Milestone M1.2a, and allows QA to validate protocol adherence without modifying any production code or test fixtures.

### Key Capabilities

1. **Protocol v1.10 Handshake & Slot Allocation:**
   - Issues canonical `MSG_HELLO` with protocol version, participant ID, display name, and join token.
   - Parses authoritative `MSG_WELCOME` from host, binding assigned slot (P2..P8), session ID, map epoch, and cryptographic `reconnectKey`.
2. **Automated Gameplay Movement Streaming:**
   - Emulates 60Hz tick input: holds `moveForward = 1.0f` (W key) for designated duration (default 30 ticks ~ 500ms).
   - Transmits an **explicit release packet** (`moveForward = 0.0f`) to prevent sticky movement or phantom walking on host.
   - Transmits neutral input (`moveForward = 0.0f, moveRight = 0.0f, buttons = 0`) for stabilization.
   - Enforces strictly monotonic command sequences (`sequence` and `clientTick`).
3. **Session Purge & Reconnection Without Stale State Reuse:**
   - Disconnects session cleanly via `LEAVE_USER_QUIT`.
   - Explicitly destroys prior client network instances and invalidates local session state (sequences, ticks, generation reset).
   - Reconnects using the authorized cryptographic `reconnectKey`.
   - Validates that the host preserves player slot and increments `connectionGeneration` (`gen1 -> gen2`).
   - Guarantees that reconnected commands strictly use `gen2`, with zero leakage of stale `gen1` state.
4. **Anti-Hijack Gate Verification:**
   - Attempts connection with a forged `reconnectKey` to prove host rejects unauthorized slot takeovers with `REJECT_HIJACK_ATTEMPT` (code 8).
5. **Clear Separation of Verdicts:**
   - Returns **PASS / FAIL** for verifiable network contracts (NET-01 .. NET-15).
   - Marks engine-specific conditions as **DEFERRED** (KEX-01 .. KEX-04), making clear that packet delivery alone does not prove Puppet movement in KEX.

---

## 2. Directory Structure

```
tools/m12a-test-client/
├── CMakeLists.txt              # Independent CMake configuration (MSVC /MT x64)
├── build.bat                   # 1-click build script (MSVC Release x64)
├── run_mock_test.bat           # 1-click test execution script against mock host
├── README.md                   # This documentation
├── TEST_REPORT.md              # Retained report of executed self-test verification
├── TEST_REPORT_PSE_HOST.md     # Retained report of executed test against standalone pse_host.exe
├── test_summary.json           # Machine-readable JSON summary of self-test
├── test_summary_pse_host.json  # Machine-readable JSON summary of pse_host run
├── m12a_client.log             # High-resolution timestamped execution log
├── include/
│   ├── m12a_client.h           # Test client core class wrapping ENetClientNode
│   ├── m12a_logger.h           # Categorized timestamped logger engine
│   ├── m12a_scenarios.h        # Automated scenarios (hold-w, reconnect, anti-hijack, full)
│   └── m12a_report.h           # Report generator (PASS/FAIL/DEFERRED, markdown, JSON)
└── src/
    ├── main.cpp                # CLI entry point, argument parser, in-process mock host runner
    ├── m12a_client.cpp         # Network state machine, packet dispatch, session purge
    ├── m12a_logger.cpp         # Log implementation (stdout + m12a_client.log)
    ├── m12a_scenarios.cpp      # Scenario workflows and contract assertions
    └── m12a_report.cpp         # Formatted summary tables and export logic
```

---

## 3. Building

### Prerequisites
- Windows 10/11 x64
- Microsoft Visual Studio 2022 (MSVC v143 toolset, BuildTools or Community+)
- CMake 3.20 or newer

### Build Command

Run `build.bat` from `tools/m12a-test-client/`:

```powershell
cd tools/m12a-test-client
.\build.bat
```

Or manually with CMake:

```powershell
cd tools/m12a-test-client
cmake -B build -S .
cmake --build build --config Release --parallel
```

Output executable:
`tools/m12a-test-client/build/Release/m12a_test_client.exe`

---

## 4. Usage & Command Line Reference

### Quick Self-Test (No external host required)
Spawns an in-process mock host, executes all scenarios, and outputs reports:

```powershell
cd tools/m12a-test-client
.\run_mock_test.bat
# or:
.\build\Release\m12a_test_client.exe --self-test
```

### Testing Against Standalone Mock Host (`pse_host.exe`)

1. Start standalone mock host in Console 1:
   ```powershell
   cd tools/enet-prototype
   .\build\Release\pse_host.exe --port 27015
   ```

2. Run test client in Console 2:
   ```powershell
   cd tools/m12a-test-client
   .\build\Release\m12a_test_client.exe --host 127.0.0.1 --port 27015
   ```

### Testing Against Future Native Host DLL (For Sol Max in M1.2a)

When Sol Max runs the game with the native host adapter:

```powershell
cd tools/m12a-test-client
.\build\Release\m12a_test_client.exe --host 127.0.0.1 --port 27015 --scenario full --report-file M12A_NATIVE_REPORT.md
```

### CLI Arguments Reference

| Option | Default | Description |
|---|---|---|
| `--host <ip>` | `127.0.0.1` | Host IPv4 address |
| `--port <port>` | `27015` | Host UDP listening port |
| `--participant <id>` | `m12a_test_p2` | Client unique participant identifier |
| `--name <string>` | `M12a_Test_P2` | Client display name |
| `--token <string>` | `""` | Join token / session password |
| `--scenario <name>` | `full` | `hold-w`, `reconnect`, `anti-hijack`, or `full` |
| `--self-test` | `false` | Run with in-process mock host |
| `--self-test-port <port>` | `28715` | Port for in-process mock host |
| `--hold-ticks <n>` | `30` | Number of ticks to hold W (+1.0 forward) |
| `--neutral-ticks <n>` | `15` | Number of ticks of neutral input |
| `--tick-rate <hz>` | `60` | Input streaming frequency (Hz) |
| `--timeout-ms <ms>` | `5000` | Connection and handshake timeout (ms) |
| `--log-file <path>` | `m12a_client.log`| Path to save execution log |
| `--report-file <path>` | `TEST_REPORT.md` | Path to save markdown report |
| `--json-file <path>` | `""` | Optional path to save JSON summary |
| `--quiet`, `-q` | `false` | Suppress verbose packet output |
| `--help`, `-h` | — | Display help screen |

### Process Exit Codes
- `0`: All verifiable network protocol contracts **PASSED**.
- `1`: One or more verifiable network checks **FAILED**.
- `2`: Configuration or socket initialization error.

---

## 5. Verification Matrix (Network PASS/FAIL vs Deferred KEX)

| Category | Check ID | Description | Verifiable on Mock Host? | Requires Native KEX DLL? |
|---|---|---|---|---|
| **Transport** | `NET-01` | Socket initialization & UDP connection | **YES (PASS)** | Optional |
| **Protocol** | `NET-02` | Handshake: `MsgHello` -> `MsgWelcome` (v1.10) | **YES (PASS)** | Optional |
| **Protocol** | `NET-03` | Authoritative slot assignment & validation | **YES (PASS)** | Optional |
| **Protocol** | `NET-04` | Initial `connectionGeneration == 1` check | **YES (PASS)** | Optional |
| **Streaming** | `NET-05` | Monotonic command sequence tracking | **YES (PASS)** | Optional |
| **Gameplay** | `NET-06` | Hold W (`moveForward = 1.0f`) stream at 60Hz | **YES (PASS)** | Optional |
| **Gameplay** | `NET-07` | Explicit W release (`moveForward = 0.0f`) packet | **YES (PASS)** | Optional |
| **Gameplay** | `NET-08` | Neutral input stream stabilization | **YES (PASS)** | Optional |
| **Lifecycle** | `NET-09` | Graceful disconnect (`LEAVE_USER_QUIT`) | **YES (PASS)** | Optional |
| **Reconnect** | `NET-10` | Reconnect with cryptographic `reconnectKey` | **YES (PASS)** | Optional |
| **Reconnect** | `NET-11` | Slot persistence across reconnect | **YES (PASS)** | Optional |
| **Reconnect** | `NET-12` | Generational increment (`gen1 -> gen2`) | **YES (PASS)** | Optional |
| **Reconnect** | `NET-13` | No stale generation/state reuse in new session | **YES (PASS)** | Optional |
| **Security** | `NET-14` | Rejection of forged `reconnectKey` | **YES (PASS)** | Optional |
| **Stability** | `NET-15` | Zero unhandled exceptions, zero socket deadlocks | **YES (PASS)** | Optional |
| **Engine** | `KEX-01` | P2 Puppet Actor instantiation in KEX | **NO** | **YES (Sol M1.2a)** |
| **Engine** | `KEX-02` | P2 native physics displacement & collision | **NO** | **YES (Sol M1.2a)** |
| **Engine** | `KEX-03` | P1 state isolation & non-interference | **NO** | **YES (Sol M1.2a)** |
| **Engine** | `KEX-04` | Safe native engine StateExit / shutdown | **NO** | **YES (Sol M1.2a)** |

> [!IMPORTANT]
> **QA Notice regarding P2 Movement:**
> Successful transmission of `moveForward = 1.0f` proves only that the client correctly formatted and delivered the command over UDP. It does **not** prove that Player 2 moved inside the KEX engine. Real native player displacement and collision can only be verified in KEX memory once Sol Max completes M1.2a.
