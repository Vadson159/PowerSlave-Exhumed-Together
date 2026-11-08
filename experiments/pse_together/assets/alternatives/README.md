# Альтернативы John для П8

Новые персонажи в сеттинге египетских руин, по уточнению пользователя 2026-11-04:

- [expedition_ranger_v1.png](expedition_ranger_v1.png) — боец экспедиции с бронзовым скарабеем и рюкзаком; [промпт](expedition_ranger_prompt.txt).
- [ruins_scout_v1.png](ruins_scout_v1.png) — разведчица с головным платком, перевязью и сумкой для находок; [промпт](ruins_scout_prompt.txt).

Оба созданы встроенным ImageGen, не CLI; исходные PNG скопированы в проект без перезаписи пикселей. Фактический размер каждого — 1586×992. Это 40 ячеек/поз (8×5): 20 walk, 5 idle, 8 shoot, 7 collapse/death. Направления и временные кадры сопоставлены с DOC-13. На кадрах смерти нет разрыва тела/разлёта частей.

`tools/build_expedition_sprites.py` читает alpha для границ прямоугольников, генерирует origins/scale и сохраняет исходные PNG побайтово в `assets/coop_sprites_expedition.kpf`. Пакет/manifest и generated native profile воспроизводимы из этих файлов. Кредиты — внутри KPF. Это два новых силуэта; восемь уникальных вариантов не заявляются.

**REVIEW / PARTIAL, режим В.** В run04 KEX реально продвигал 1/4/2/7 кадров обоих наборов через diagnostic preview. Это не приёмка художественной непрерывности, всех углов, grounded spawn или боевых переходов. Полный отчёт — [DOC-68](../../../../docs/68_P8_PLAYER_SPRITES_AND_ANIMATIONS.md).
