# DOC-56 — Native Acceptance Addendum (attempt 2)

2026-11-03 UTC. **Итог не изменился: DOC-56 PARTIAL. Native D1 BLOCKED, native D2 UNKNOWN.** Исходный [DOC-56](56_NATIVE_MAP_IDENTITY_READY_GATE.md) сохранён как исторический результат без правок.

## Preflight (Этап 1) — FAIL до запуска игры

[NATIVE_PREFLIGHT_2.json](evidence/m1/NATIVE_MAP_READY_56/NATIVE_PREFLIGHT_2.json):

- В этой сессии нет REA MCP: серверы сессии — только Claude Docs и scheduled-tasks. `rea` зарегистрирован в `~/.claude.json` (`mcpServers.rea`) и в Codex `config.toml`, но этой сессией не загружен.
- REA CLI обновлён до 6.8.0 (после попытки 1, условия изменились), повторный `rea list-frida-devices`: `capability_unavailable` — нет `rea-agents/node_modules/frida/build/frida_binding.node`; в `build/` только `BSDmakefile`, `Makefile`, `src`. MCP-сервер использует тот же `rea.mjs` и тот же Frida-провайдер, поэтому даже загруженный MCP не дал бы attach.
- Необходимые возможности 2–4 (Frida attach к PID, загрузка DLL через принятый `load_native.js`, isolation write guard) недоступны; 1/5/6 без них не имеют смысла.
- AGENTS.md: «Не исправлять REA MCP». REA/Frida не устанавливались и не пересобирались; новый injector не создавался; ни один процесс игры не запускался; KEX память не читалась.

Следовательно, этапы 2–4 (runtime проверка `GI+0x1E0/+0x1F8`, level witnesses, state fields, имя Tomb of Ramses, D1, D2) **не выполнены**. Реальные значения native map identity: **UNKNOWN**. PID/TID, journals, OS exit codes DOC-56: отсутствуют.

## Особая проверка: 30 stable ticks и `localThisTick == 1` (static source, не runtime)

В Client DLL (`native_second_instance/src/client.cpp`, DOC-56 build) порядок в одном `hookTick`: original StateTick → `hookUpdate` увеличивает `localThisTick` → `Session::consume()`: сначала `observeMap()` (derive + `ClientGate::observe`, stability +1 при каждом валидном tick независимо от `localThisTick`), затем отправка MapReady при `gameplay() && localThisTick==1 && shouldSendReady()`. `shouldSendReady` = `IdentityVerified && verifiedThisTick` того же вызова. `IdentityVerified` сохраняется между ticks, поэтому tick с `localThisTick!=1` лишь откладывает отправку до следующего обычного tick. Условия не противоречат друг другу; Host recover timeout 600 Host ticks ≫ 30. Автономно достижимость отправки через тот же `ClientGate` и реальный Worker уже доказана `map_ready_contract` (positive). Native достижимость — UNKNOWN. Дефекта не найдено, исправлений нет.

## Состояние артефактов

Все хеши [BUILD_MANIFEST](evidence/m1/NATIVE_MAP_READY_56/BUILD_MANIFEST.json) совпадают (Host DLL `2b66f075…ecdf7`, Client DLL `92bd90b9…5e09d`); относительно baseline изменены только 4 заявленных source файла; original/copies/full original snapshot/protected profiles совпадают. Production и тесты не менялись → регрессии не перезапускались (результаты DOC-56 остаются действительными).

## v1.10

Host получает только client-attested epoch echo; независимой Host-side проверки карты клиента нет. Защита от несовпадения целиком держится на честной client-side проверке. v1.11 не реализуется.

## Следующий минимальный шаг

Запустить DOC-55 runner/bind/gate с DOC-56 артефактами в сессии, где REA MCP загружен **и** его Frida provider работает (например, исправная установка, на которой выполнялся DOC-55), либо после явного разрешения пользователя на восстановление Frida bindings в rea-agents вопреки правилу AGENTS.md. До этого DOC-56 остаётся PARTIAL. Остановка; DOC-57 не начинался.
