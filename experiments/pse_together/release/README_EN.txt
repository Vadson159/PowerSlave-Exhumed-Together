POWERSLAVE EXHUMED TOGETHER — PUBLIC TEST 2026-11-12

Unofficial campaign co-op for PowerSlave Exhumed (2022, Steam), Windows x64.
Supports 2–8 players over LAN / Radmin VPN. Early test build: bugs, crashes
and desynchronization remain possible. Eight-player internet play is unverified.

INSTALL
1. Each player needs their own Steam game and Steam running.
2. Extract the ZIP to a short writable path, e.g. D:\PSE_Together.
3. Run install.bat. It finds your Steam game and creates a separate game copy.
   If asked, paste the game folder from Steam > Manage > Browse local files.
   The installer checks the supported executable SHA-256 before installation:
   b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d
4. Host: host.bat, New Game, enter a level.
5. Friends: join.bat, F7, select the host's open lobby, Enter. Start New Game;
   the mod follows the host's level. If discovery fails, enter the host's
   LAN/Radmin IPv4 manually, port 28745. Everyone needs the SAME mod build.

NETWORK
Join the same LAN or Radmin network. Allow the copied game through the firewall
on that network: UDP 28745 (game), UDP 28746 (discovery). Radmin may use the
public network profile. No matchmaking or automatic NAT traversal.

CONTROLS
F7: lobby. Backslash: player list. T: chat. E: respawn after death.
Any player's level exit moves the group onward.

SAVES / STEAM
Mod profiles, settings and logs live in profile next to game. The original
Steam game and normal saves/settings remain separate. Steam overlay and shared
achievement forwarding are enabled: REAL Steam achievements may unlock.
To disable mod achievement forwarding, run set PSE_ACH_DRYRUN=1 in Command
Prompt before starting host.bat or join.bat from the same window.

UPDATE / UNINSTALL
Close the game, extract the full new package and run install.bat again.
Everyone must update together. To uninstall, close the game and delete this
folder, keeping any mod saves you want first.

Missing SDL2_pse_original.dll? Rerun install.bat with the Steam game available.
Compare VERSION.txt on both PCs. Bug reports: version, map, player count,
steps and relevant profile/host/logs + profile/client/logs. Review logs first.

Source, downloads, screenshots and bug reports:
https://github.com/Vadson159/PowerSlave-Exhumed-Together

By Vadson159 / Vadson. Unofficial fan project. Commercial game not included.
See THIRD_PARTY_NOTICES.txt and the credits inside the sprite KPF.
