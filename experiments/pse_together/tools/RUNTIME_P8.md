# П8: воспроизведение и ограничения

Статус: REVIEW / PARTIAL, режим В — [DOC-68](../../../docs/68_P8_PLAYER_SPRITES_AND_ANIMATIONS.md).
Игра и instrumentation только REA MCP + принятые isolation / DOC-51 loader.
Новый injector не добавлялся. Все текущие `P8_*_01…04` завершены: **не повторять**.

## Offline

```powershell
python -X utf8 -B experiments/pse_together/tools/build_expedition_sprites.py
python -X utf8 -B experiments/pse_together/tools/run_p8_offline.py --out docs/evidence/m1/PSE_TOGETHER_P8/OFFLINE_NEW_ID
python -X utf8 -B experiments/pse_together/tools/pre_native_p8.py --offline docs/evidence/m1/PSE_TOGETHER_P8/OFFLINE_NEW_ID/RESULT.json --out docs/evidence/m1/PSE_TOGETHER_P8/PRE_NATIVE_GATE_NEW_ID.json
python -X utf8 -B experiments/pse_together/tools/review_p8.py
```

Последняя команда только перечитывает завершённый run04. Она не запускает игру.
Builder не меняет PNG. Он генерирует rect/origin/scale metadata и KPF с native auto-mount.
John baseline builder `build_player_sprites.py` сохранён, но финальный runtime использует новый пакет.

## Новый runtime — только при изменении условий

1. Выбрать новые RUNS и GATE_PATH в `runtime_p8.py`, новый gate/snapshot. Сверить EXE SHA.
2. Через REA spawn Python `runtime_p8.py launch host NEW_HOST`, resume Python; дождаться launch.json; `bind host NEW_HOST`. Attach REA к указанному owned suspended EXE; load **только** generated_isolation.js и load_native.js. `observer_bound.js` не загружать. Resume игрового primary thread через принятый action resume. Запустить watch через отдельный REA Python spawn; сохранять retained handle до exit.
3. То же для клиента. Открыть TOMB через ui_start, осмотреть screenshot; RETURN на экране карты. Focus непосредственно перед вводом. Если caption покрыт другим окном, разнести только два проверенных owned окна обычным OS MoveWindow, без desktop/session переключений.
4. Run04 отвёл камеру клиента физическим S0.08 **до permit_native / сетевого подключения**. Это не гарантирует disjoint host placement (П4). Host — только W0.03 для принятой bounded позиции. При каждом шаге смотреть реальные координаты и native gates, не считать длительность клавиши расстоянием.
5. Сохранить complete `frida_session_status` в STATUS_GATE.json каждого run; `gate` должен дать current-run ISOLATION_GATE PASS. Только затем load permit_native.js. На клиенте получить p8_resource_gate и MapReady, snapshot BEFORE observer, выгрузить его, отдельно load permit_proxy_spawn.js.
6. `observer client NEW_CLIENT render LIVE` генерирует принятый P1 observer. В run04 к bound source добавлен read-only Scene callback, журналирующий `readObservation().frame_token` при изменении; точный source сохранён в evidence. Он не читает owned Actor через JS.
7. `preview_p8.js` связывается PID/target/DLL path + journal path из NATIVE_ARTIFACT.json. Только atomic PSE_P8_Preview: два персонажа × четыре позы, затем slot8 = возврат к DTO. Это **диагностический preview**, не доказательство authoritative combat. Screenshot capture — обычный validated ui capture; никаких game state stores из JS.
8. Regression-trigger run04: host physical S0.08 с handoff, после завершения preview. Это намеренно может вынести host target за accepted area; требуются refusals >0 и отсутствие animation fault, actor всё ещё live. Не использовать этот шаг для visual acceptance grounded spawn.
9. request_shutdown → подтверждённый deferred_retired → unload renderer observer → request_detach → 6 exact16 restores. WM_CLOSE клиента; убедиться в retained handle signaled/exit0 и protected after. Focus/WM_CLOSE хоста, те же доказательства. Export complete REA evidence и закрыть все свои сессии.
10. Integrity оригинальных77 и защищённых сохранений. Удалить только собственные additive KPF из workspace test copies после сверки SHA; пакет assets оставить. Не менять оригинальную установку.

В run01 absolute `-file` сломал загрузку: KEX приставляет basepath. Runs02/03 показали также auto-mount mods/*.kpf; run04 использовал auto-mount один раз. Сохранённые артефакты run04 и source snapshot — точка воспроизведения; текущий b68 не является frozen evidence.
