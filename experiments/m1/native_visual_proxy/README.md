# DOC57 — one stock passive Actor

Exact-build isolated REA/Frida experiment. Reuses the accepted M0/M11 suspended
launcher, write-open isolation and owned-window helpers. No new injector or DLL.
No network, Player construction, input replacement, camera hook or game-field
writes. All native calls run synchronously on the attested simulation thread.

**Recorded result: BLOCKED.** The archived probe falsely required a nested Frida
factory-listener callback before recording its returned pointer. The factory did
create one native Actor, but the probe stopped, visibility was not demonstrated,
and no native Remove was attempted. These sources are the failed experiment,
not an accepted reusable visual proxy. Read RESULT_SUMMARY and the main document.
The two Python/CModule fixtures reproduce listener suppression inside onLeave;
`traps: 'all'` also fails to observe the nested listener in that fixture.

Read docs/57_NATIVE_VISUAL_PROXY_STEP0.md and the evidence verdict before reuse.
Every native run requires a fresh N57_* run/profile; do not reuse historical PIDs.

File preparation: `workflow.py audit`, `workflow.py tests`, `workflow.py manifest`,
`workflow.py integrity`. No command in workflow.py launches a game.

REA spawn Python `runner.py launch N57_STEP0_01`; resume that launcher; bind the
new launch identity with `runner.py bind N57_STEP0_01`. Attach REA to the exact
reported game PID. Load generated_isolation.js and scout_bound.js while the
game primary thread is suspended, then reuse `runner.py action ... resume`.
Use stock UI to select Tomb of Ramses. Export full REA records and extract the
accepted message envelope; `runner.py gate ...` must pass before loading the
experimental script. Unload scout before probe_bound.js so prefixes are restored.

`runner.py command ... 1` generates the one-shot mailbox request; load it through
REA only after the probe has reported its owned mailbox. Repeating it is
idempotent. Capture owned-window before/live/after frames. Perform only bounded
ordinary W/S input. After at least 120 actual base Actor Tick callbacks,
`runner.py command ... 2` requests one native Remove. Wait for pool return and
the same sweep's node payload/in-use observation. All non-owning pointers are
discarded at retirement. Native pooled storage is not a destructor/free claim.
After post-retire P1 checks, unload the experimental script and verify its native
hook prefixes are restored. Finally request ordinary WM_CLOSE, retain OS handles through signal/exit0 and
close every REA session. No forced termination may support a PASS.
