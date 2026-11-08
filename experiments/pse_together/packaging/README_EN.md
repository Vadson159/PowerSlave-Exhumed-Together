# PowerSlave Exhumed Together — P25 kit

Diagnostic build for Windows 10/11 x64. The standalone ENet program tests
LAN/Radmin connectivity and simulates players outside the game. Frozen P1/P24
native DLLs are developer reference artifacts. Portable in-game co-op has not
been accepted. No game assets, saves, Steam, REA or Python are distributed.

## Install and connect

1. Extract the entire ZIP to a separate writable folder, for example
   `C:\PSE-test`. Keep it separate from the game. No administrator is needed.
   Requires Windows PowerShell 5.1 and Windows system DLLs; the VC++ runtime is static.
2. Run `verify.bat` to check SHA-256 hashes and the config schema.
   `friends.json` is editable and deliberately excluded from the hash list.
3. Edit `friends.json` in Notepad. `bind` is the host computer's local IPv4;
   `host` is the host address used by the client. Leave 127.0.0.1 for loopback.
   Use private IPv4 addresses for LAN or 26.x.x.x for Radmin.
   `port`: matching UDP port 1024..65535, default 27015. `token`: matching session
   password. Give each friend a different `name` and `participant_id`.
   These three strings accept 1..32 ASCII letters, digits, underscores or hyphens.
   `max_clients`: 1..7, default 1, diagnostic prototype capacity only.
   `crash_dumps`: true enables external crash capture; false records text only.
   Unknown fields, wrong types and public addresses are rejected before launch.
4. Host runs `host.bat`, friend runs `client.bat`. If Windows Firewall asks,
   allow the application on the intended private network only. Scripts do not
   modify firewall rules. Radmin users must join the same Radmin network.
5. Expect `Authenticated!` on the client and P2 in the host's `status` output.
   `stat` shows RTT/packets; `move 1 0` and `shoot` affect the standalone model.
   Enter `quit` to exit cleanly. This archive has not yet been tested on two PCs.

## Logs and crashes

Each launch creates `logs/<role>_<UTC>_<ID>/`: `console.log` captures stdout/stderr,
`diagnostics.log` records PID, exception and exit codes, and `session.json`
records role, result and package version without the password.
An unhandled exception in the owned ENet child produces `crash.dmp`, a Windows
minidump with exception context. The external supervisor never attaches to KEX.
Dump failures are recorded as `crash_dump_failed`; a nonzero exit alone does not
prove a dump was written. Closing the window also ends the child. Use `quit`
to preserve the normal session receipt.

Run `collect.bat` after both processes exit. `results_*.zip` includes known text
logs only. Current user/kit paths and MAC addresses are redacted; IP addresses,
player names and arbitrary text may remain. Review before sharing. Config,
token and dumps are excluded by default. To opt into including dumps run
`powershell -NoProfile -ExecutionPolicy Bypass -File .\friends.ps1 collect -IncludeDumps`.
Dumps can contain private process data and are not anonymized. Send the archive
to the developer yourself.

Uninstall: close the processes, keep any results you need, delete the extracted
folder. No services, drivers or registry settings are installed.

## Native reference and EXE check

Required game EXE SHA-256:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
`native-reference/` contains the exact frozen SDL2 shim, broker, checker and
separate host/client payload DLLs. See `provenance.json` for hashes and sources.
`symbols.zip` is supplied separately for diagnostics, without game assets.
`native-reference/pe-check.exe <EXE-path>` reads the file, prints RU/EN
expected/actual SHA (UTF-16LE stdout), and returns 0 or a Win32 error code.
Do not copy these DLLs into the original Steam installation or launch KEX
with this kit. The accepted broker allows only two exact workspace paths;
REA supplies isolated profiles under the DOC-51/P24 procedure. Developers use
the existing workspace and `tools/p24.py` according to its README. This archive
does not grant native permits or alter game configs or hooks. Native KEX logs
and dumps remain part of the accepted lab evidence. Automatic KEX crash capture
and a clean friend game installation are not certified here.

If hashes/config validation fail, keep the error, run `verify.bat`, and extract
the original ZIP into a new folder. If authentication fails check address,
port, token, Radmin and firewall; do not disable the firewall entirely.
No port forwarding, NAT traversal or matchmaking is provided.
