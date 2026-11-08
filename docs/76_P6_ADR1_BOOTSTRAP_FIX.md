# DOC-76 — П6: исправление A1 и применение решений пользователя

2026-11-04. Исполнитель — Codex. Только П6 / S2.1, S3.1; режим В. **REVIEW**, редакция 3 [DOC-62](62_P6_ADR_AUTHORITY_AND_CLIENT_WORLD.md), на повторную независимую проверку. [DOC-75, Astra](75_P6_INDEPENDENT_REVIEW_ASTRA.md) независимо принял ADR-2 и вернул ADR-1 по A1/P1; его прежний вердикт/доказательства не перезаписывались.

Пользователь поручил выбрать и применить оптимальные варианты, затем явно исправить дефект ADR-1. [Решения](evidence/m1/P6_ADR_REV3/DECISIONS.md): **D2=A, D3=W1, D3b=узкое условное Remove после class gate П9, D10=поля П6 в отдельной будущей v1.12**. AGENTS уточнён только в этих принятых границах. v1.10/v1.11, исходники игры/мода и DLL не менялись; runtime/native gates не снимались.

## Исправление A1

Убрано безусловное «нет пары history → новый reset». Теперь T2/T5/T5a задают переход **WAIT_ANCHOR → WARMUP_HISTORY → TRACKING**:

| Ситуация | Правило редакции 3 |
|---|---|
| Установка anchor | Прочитать фактически установленное состояние; seed с реальным локальным t_install и C=0. GenerationStart неизменен; не записывать seed в прошлое host stateTime |
| Sample до начала предсказания либо uncertainty пересекает начало | Отбросить BEFORE_PREDICTION_GENERATION в любой фазе. Не сбрасывать seed/token/deadline |
| Ещё нет второй записи или правой границы interval tau±u | Один pending DTO; новые samples его не заменяют. Проверять после следующего post-step до новых samples, не более одного сравнения за шаг |
| Пригодная непрерывная пара/покрытие | Выполнить существующие T3/T4; успешное сравнение, включая e=0, переводит в TRACKING |
| Watermark продвинут, пока pending ждал | HighestSeen и lastCompared разделены; уже принятый pending сравнивается один раз, позднейшие отброшенные seq не делают его фиктивным дубликатом |
| History вытеснена после начала TRACKING | Настоящий reset T5. Это отличается от начального накопления history |
| Warmup/pending не завершился в конечный срок | Закрыть это соединение с причиной; дедлайн не продлевается потоком пакетов. Не создавать новый token автоматически по timeout |
| Новый разрыв / старое поколение | Действительный новый разрыв → T5; чужой token/старый разрыв отбрасываются до изменения watermark |

До ACK хост держит neutral и не публикует обычные snapshots нового token. Одного этого ограничения недостаточно для A1, поэтому seed и WARMUP работают также для первого snapshot **после ACK**. Вход не восстанавливает отброшенное локальное движение; replay не добавлен.

## Проверки

[BOOTSTRAP_EXAMPLES.json](evidence/m1/P6_ADR_REV3/BOOTSTRAP_EXAMPLES.json), [script](research/p6_history_bootstrap_examples.py), [raw output](evidence/m1/P6_ADR_REV3/CHECKS.log): **43/43**, **INFERRED** scalar specification fixtures, не KEX implementation или native/runtime-приёмка.

Проверены обе временные трассы независимого DOC-75: pre-ACK sample не вызывает reset, дальнейший пригодный sample достигает TRACKING; post-ACK первый sample получает пару с реальным seed. Есть составные сценарии для uncertainty у границы поколения, pending без вечного замещения, двух watermark, поздней/повторной доставки, duplicate timestamp, истинного вытеснения history, таймаутов и нового разрыва. Отдельно проверено, что warmup/pending timeout закрывает соединение без нового resetToken.

При повторной независимой проверке найдены два пробела первоначального fixture: он разрешал два сравнения за один local step и проверял ожидаемую правую границу раньше уже известного внутреннего разрыва history. Нормативный T5a уже запрещал оба поведения и не менялся. Fixture получил step budget, приоритет known gap и четыре проверки (итог 43/43). Прежние [39 результатов](evidence/m1/P6_ADR_REV3/EXAMPLES_39_BEFORE_STEP_BUDGET.json) и [исходник](evidence/m1/P6_ADR_REV3/FIXTURE_39_BEFORE_STEP_BUDGET.py) сохранены. Это исправления synthetic-проверки, не обнаруженные игровые дефекты.

[REUSE_CHECKS.json](evidence/m1/P6_ADR_REV3/REUSE_CHECKS.json): **141/141** SHA/profile/saved-summary сверок, три exact EXE. Принятая статика порядка фабрики **25/25** переиспользована, не повторена; старые 53 примера редакции 2 сохранены как история. Снимок [DOC62_REV2_BEFORE](evidence/m1/P6_ADR_REV3/DOC62_REV2_BEFORE.md) имеет SHA проверенного независимой моделью DOC-62 `58426f3d39e86f7f171cd83394cbed7ad60a67cd285043966e8932bf2513930f`.

Нативные контракты движения/подавления, фактический hook post-tick, размеры history и допустимые задержки/u/timeouts остаются П7; suppression/lifetime/provenance выбранных классов — П9. Численные fixtures проверяют определения ADR, не обещают плавность или работоспособность KEX. Для прежних функций переиспользуются exact-SHA [координаты VA/RVA/file offset](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/FUNCTION_COORDINATES.json) и [наблюдаемые связи/ABI limits](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/STATIC_EXCERPTS.json); новых адресов/вызовов нет.

Игра, Ghidra, REA/Frida, Steam и сеть не запускались; принятые исследования/runs и native tests не повторялись. Изменены только контракт П6, его документация/evidence, fixture и разрешённые пользователем правила/решения. Оригинал и защищённые профили не менялись; своих игровых процессов/REA-сессий не создавалось. Итоговая независимая проверка оформляется отдельно, без самоприёмки автора.

Воспроизведение:

```text
python -X utf8 -I docs/research/p6_history_bootstrap_examples.py
```

Ссылки обновлённого пакета проверяются с отдельным output `docs/evidence/m1/P6_ADR_REV3/LINK_CHECK.json`; прежние evidence не перезаписываются.
