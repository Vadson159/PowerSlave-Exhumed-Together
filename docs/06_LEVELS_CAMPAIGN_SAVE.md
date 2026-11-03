# Уровни, кампания, сохранения

VERIFIED: [LoadMap 0x1401c7b70](evidence/ghidra_2022/G_FUNC_1401c7b70.json) привязан к строке `kexSlaveLevelLocal::LoadMap`, читает геометрию/сектора/акторы и вызывает read helpers. [ReadActors 0x1401cc490](evidence/ghidra_2022/G_FUNC_1401cc490.json) проходит map actor records stride `0x14` и вызывает factory from map record. Для типа player проверяется/нормализуется sector, затем действует обычный singleton Spawn из [03](03_PLAYER_LIFECYCLE.md).

VERIFIED: game instance state dispatcher использует разные requested/current states. [Enter state 0x1401b6a00](evidence/ghidra_2022/G_FUNC_1401b6a00.json) case 4 вызывает level interface LoadMap и переводит состояние в case 3; case 3 устанавливает player/HUD/script/music state. [Leave state 0x1401b6670](evidence/ghidra_2022/G_FUNC_1401b6670.json) освобождает player handle, скриптовые contexts, level и pools. [Level unload 0x1401c90e0](evidence/ghidra_2022/G_FUNC_1401c90e0.json) освобождает массивы геометрии/акторов и связанные ресурсы. На переходе mapEpoch сети должен смениться; ни один старый entity ID/handle не применяется к новой карте.

Кампания задаётся `defs/mapInfo.txt`, scripted map events и глобальным Player progress; [E010](evidence/E010_KPF_SELECTED_TEXT.json). `TOMB/TOMBEND` используют общие quest/artifacts и локальные buttons/lock/holster. Это требует разделения общего сюжетного прогресса и индивидуального управления катсценой. Полное изменение всех scripts не входит в первый прототип.

Сохранения:

| Адрес | Проверенное наблюдение | Что пока неизвестно |
|---|---|---|
| `0x1401af7d0` | Console `savegame <slot>` вызывает `0x1401b3260` и печатает Game saved по результату | Фактический durable commit без runtime не проверен |
| `0x1401b3260` | Формирует byte stream из game/player/progress fields, имя `saves/save_{:03}.sav`, вызывает platform user-data interface | Полная schema/version/durability; это не доказанный snapshot всех живых actor/mover/script states |
| `0x1401b1de0` | Создаёт запросы чтения slots и ставит platform request, хранит shared request state | Completion/lifetime нужно проверять отдельно, не считать вызов синхронным |
| `0x1401af8f0` / `0x1401b3f20` | `loadgame` вызывает read path, platform update, затем parse/apply slot; apply проверяет readiness/slot и восстанавливает player/state | Не выводить доступность всех slots/режимов из console wrapper |
| `0x1401b5b80` / `0x1401b5ce0` | Pack/restore subset player/campaign fields, health и weapon-related arrays | Полная типизация полей не установлена |
| `0x1401b5df0` / `0x1401e04e0` | Loaded position/angle/sector, player init, bitsets/world restoration callbacks | Поведение второй сущности, mid-level join и rollback UNKNOWN |

Все dossiers: [save](evidence/ghidra_2022/G_FUNC_1401b3260.json), [read request](evidence/ghidra_2022/G_FUNC_1401b1de0.json), [apply](evidence/ghidra_2022/G_FUNC_1401b3f20.json), [pack](evidence/ghidra_2022/G_FUNC_1401b5b80.json), [restore](evidence/ghidra_2022/G_FUNC_1401b5ce0.json), [loaded world](evidence/ghidra_2022/G_FUNC_1401b5df0.json).

INFERRED минимальная будущая политика: host хранит оригинальный campaign save и отдельное versioned coop state с независимыми игроками, общим progress, ruleset и achievement participation ledger. Оригинальный `.sav` сейчас не менять. Save synchronization требует связать sidecar с конкретным save generation/content hash, не допускать смешанных версий. Переподключение восстанавливает собственное player state; не копирует controller memory или чужой Steam progress.

Первый сетевой прототип может допускать подключения на level barrier; настоящий mid-level join должен ждать доказательства полноты world baseline. Участие в достижениях регистрируется с фактического входа в событие/интервал кампании: присоединение не выдаёт прошлые награды. Финальное прохождение/сложность/условие без анкхов требуют накопительного командного условия и индивидуального покрытия участия; детали в [handoff](10_OPUS_HANDOFF.md).

UNKNOWN: все законные transition paths, полный формат save, восстановление текущих AI/movers/script coroutine states, compatibility с campaign resume и achievement eligibility. Эти вопросы ограничивают будущие milestones, но не препятствуют началу локального M0.
