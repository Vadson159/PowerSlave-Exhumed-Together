# Exhumed 2022: script API, callback map и наблюдаемые поля

> Дополнение после API-only среза: G_FUNC_1401dbc60 подтвердил Puppet owner +0x1b8 и singleton Player+d8 rebinding; G_FUNC_1401db790 — allocation 0x1e0; G_FUNC_1401f1180 — GetButtons uint32+0; G_FUNC_140058e50 — CastToActor identity callback. Прежние UNKNOWN в таблицах ниже относятся к моменту registration/inspect анализа. Актуальные выводы — [03_PLAYER_LIFECYCLE](../03_PLAYER_LIFECYCLE.md) и [10_OPUS_HANDOFF](../10_OPUS_HANDOFF.md). Безопасный remote lifecycle/ABI по-прежнему UNKNOWN.




Анализ выполнен по сохранённым Evidence **direct Ghidra headless 12.2** и текстам установленного KPF. Это не результат работоспособного REA Ghidra provider. Игра не запускалась, callbacks не вызывались, патчинг/инъекция не выполнялись.

Бинарник: `anubis_Shipping_Steam_x64.exe`, SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Preferred image base `0x140000000`. Все VA ниже — адреса статического preferred image. RVA=VA-base; file offset вычислен по raw секции из [PE_main2022.json](../evidence/PE_main2022.json). PE ASLR DYNAMIC_BASE flag в этом Evidence false; нельзя выводить runtime base из предположения о загрузчике.

[Машиночитаемая карта E014](../evidence/E014_SCRIPT_REGISTRATION_MAP.json) содержит точные строки, исходные Ghidra JSON и их SHA-256, последние присваивания callback temporaries, bounded pseudocode, machine-address loads, aliases, instructions и выбранные KPF excerpts. [extract_script_registration.py](extract_script_registration.py) читает только сохранённые JSON. При появлении новых G_INSPECT/G_FUNC может пересобрать derived aggregate; сырые Evidence не меняет.

## Что доказано и как получена карта

**VERIFIED:** четыре сохранённых routine содержат 217 передач callback pointer вместе со строкой script declaration, 74 enum-value регистрации и семь object-type size/flags arguments:

| Routine, имя Ghidra | VA / RVA / file offset | Наблюдаемое содержимое |
|---|---|---|
| FUN_1401f1880 | `0x1401f1880 / 0x1f1880 / 0x1f0c80` | 79 callbacks: globals/player/AI extensions, enums и регистрация actor/puppet/AI routines |
| FUN_1401f5040 | `0x1401f5040 / 0x1f5040 / 0x1f4440` | 46 методов kexSlaveActor + scratch properties |
| FUN_1401f6a70 | `0x1401f6a70 / 0x1f6a70 / 0x1f5e70` | 46 методов kexSlavePuppet + scratch properties |
| FUN_1401f84a0 | `0x1401f84a0 / 0x1f84a0 / 0x1f78a0` | 46 методов kexSlaveAI + scratch properties |

Источники: [G_FUNC_1401f1880](../evidence/ghidra_2022/G_FUNC_1401f1880.json), [G_FUNC_1401f5040](../evidence/ghidra_2022/G_FUNC_1401f5040.json), [G_FUNC_1401f6a70](../evidence/ghidra_2022/G_FUNC_1401f6a70.json), [G_FUNC_1401f84a0](../evidence/ghidra_2022/G_FUNC_1401f84a0.json).

**Метод:** извлекается именно **последнее присваивание той local-переменной**, адрес которой передаётся в registration call: local_a8 или local_38. Другие temporaries, например local_78, не заменяют её. В E014 сохранена история присваиваний и точная строка RHS; FUN и LAB не объединяются. Статические data xrefs/address loads в assembly подкрепляют адрес callback.

**VERIFIED** здесь = наблюдаемые строки, присваивания, инструкции, константы и связи в данном SHA. **INFERRED** = семантическое толкование как AngelScript registration/getter и роль поля. **UNKNOWN** = выполнение/успех регистрации, runtime object validity, ownership, полноценный native ABI и безопасность прямого вызова.

Exact script declaration вроде `kexSlavePuppet@ GetActor()` не является проверенной C++ сигнатурой. Для native callbacks не восстановлены полные аргументы, this-adjustment, возвращаемые handles, refcount contract или исключения. Числовой call-convention argument 3 сохранён как наблюдение; доказанного ABI контракта из одного числа нет.

Object-type registration аргументы: slaveRotation_s=0xC, object factory/automap=8, kexSlavePlayer=0x198, kexSlaveActor=0x1B8, kexSlavePuppet=0x1E0, kexSlaveAI=0x1F8; flags в этих вызовах 0x40001. Это **аргументы registration**, не доказанные sizeof, возможность new/construct или число существующих объектов. Class sizes нельзя превращать в готовые структуры.

## Player: поля и обязательный node/handle слой

Регистрация `kexSlavePlayer` и методов наблюдается в FUN_1401f1880, псевдостроки 539–649. Поля ниже имеют **instruction corroboration**, а не рассчитаны по старому исходнику.

| Script declaration | Callback label | Наблюдаемые инструкции | Вывод и граница |
|---|---|---|---|
| `EnumPlayerArtifacts& GetArtifacts()` | LAB_1401ae080 | LEA RAX,[RCX+0x90]; RET | VERIFIED возвращаемый адрес RCX+0x90; artifacts role по registration |
| `EnumPlayerAbilities& GetAbilities()` | LAB_1401ae090 | LEA RAX,[RCX+0x94]; RET | VERIFIED RCX+0x94 |
| `int16& GetKeys()` и const overload | LAB_1401ae0c0 | LEA RAX,[RCX+0x98]; RET | VERIFIED RCX+0x98, оба overload alias |
| `int16& GetQuestItems()` и const overload | LAB_1401ae0a0 | LEA RAX,[RCX+0x9a]; RET | VERIFIED RCX+0x9a |
| `uint32& GetTeamDolls()` и const overload | LAB_1401ae0b0 | LEA RAX,[RCX+0xa4]; RET | VERIFIED RCX+0xa4 |
| `void SetShakeTime(const int)` | LAB_140198140 | MOV dword ptr [RCX+0xb0],EDX; RET | VERIFIED 32-bit store EDX, storage+0xb0; instruction есть в overlapping G_INSPECT198120 |
| `kexSlavePuppet@ GetActor()` | LAB_140194b30 | MOV RCX,[RCX+0xd8]; TEST RCX; JNZ 140197ee0; null -> EAX=0 | VERIFIED загружается pointer+0xd8, затем отдельный helper |
| `EnumPlayerButtons GetButtons() const` | FUN_1401f1180 | callback mapping, тело пока не выведено этим анализом | UNKNOWN cmd offset/representation |
| `int16 GetHealth() const` | FUN_1401dfa20 | callback mapping | UNKNOWN storage/path: не смешивать с Actor.Health |
| `void SetLockTime(const int)` | LAB_1401f1190 | callback mapping | UNKNOWN offset без отдельного instruction Evidence |
| `bool IsWeaponOwned(const EnumPlayerWeapons) const` | FUN_1401ae020 | callback mapping | UNKNOWN inventory layout |
| `void HoldsterWeapon()` | FUN_1401e0bb0 | callback mapping | INFERRED операция weapon state; side effects требуют dossier |

Evidence: [G_INSPECT_1401ae080](../evidence/ghidra_2022/G_INSPECT_1401ae080.json), [1401ae090](../evidence/ghidra_2022/G_INSPECT_1401ae090.json), [1401ae0a0](../evidence/ghidra_2022/G_INSPECT_1401ae0a0.json), [1401ae0b0](../evidence/ghidra_2022/G_INSPECT_1401ae0b0.json), [1401ae0c0](../evidence/ghidra_2022/G_INSPECT_1401ae0c0.json), [140194b30](../evidence/ghidra_2022/G_INSPECT_140194b30.json), [140198120](../evidence/ghidra_2022/G_INSPECT_140198120.json).

### GetActor не возвращает напрямую pointer из +0xd8

**VERIFIED:** [FUN_140197ee0](../evidence/ghidra_2022/G_FUNC_140197ee0.json), VA `0x140197ee0` / RVA `0x197ee0` / file `0x1972e0`:

~~~text
MOV RBX,qword ptr [RCX+0x28]
null? return 0
CALL qword ptr [object_vtable]
metadata chain:
  CMP dword ptr [metadata+0x8], dword ptr [0x140a46128]
  parent = qword ptr [metadata+0x20]
on failure: assertion "pRet != nullptr", SlaveObjectNode.h, source line 0x56
on success: return RBX
~~~

**INFERRED:** Player+0xd8 хранит node/handle, из которого helper получает object pointer по node+0x28 и проверяет тип по metadata. Ассоциация с puppet подкреплена exact script return type. Нельзя обходить этот слой и считать `*(Player+0xd8)` непосредственно puppet. Node lifetime/refcount/pooling и запись puppet-owner **UNKNOWN**.

**UNKNOWN:** поле обратной связи puppet->player/controller этим registration-анализом не установлено. Actor.GetSourceOwner — отдельный API и не доказан как player-owner pointer.

### Глобальный Player()

**VERIFIED:** `kexSlavePlayer@ Player()` связан с LAB_1401f1260. [G_INSPECT_1401f1260](../evidence/ghidra_2022/G_INSPECT_1401f1260.json):

~~~text
MOV RAX,qword ptr [0x140af33a0]
LEA RCX,[0x140af33a0]
JMP qword ptr [RAX+0xa8]
~~~

Это статический объект/dispatch at 0x140af33a0 и vtable slot+0xa8; сам wrapper не устанавливает конкретный player-member offset. **INFERRED:** script API выбирает один текущий player через engine service. Глобальный accessor не доказывает принципиальной невозможности создать другой controller/actor.

## Общие Actor/Puppet/AI getters и разделение ссылок

**VERIFIED:** три registration routine повторяют 46 declaration->callback связей, включая многие одинаковые callback addresses. Это общий script surface; не доказанное полное C++ наследование.

| Declaration family, exact смысл из строки | Callback | VERIFIED displacement/instruction |
|---|---|---|
| GetOrigin mutable/const | LAB_140194aa0 | LEA RAX,[RCX+0x18] |
| GetRotation | LAB_140198100 | LEA RAX,[RCX+0x30] |
| GetSector | LAB_140198110 | LEA RAX,[RCX+0x3c] |
| GetVelocity mutable/const | LAB_140198120 | LEA RAX,[RCX+0xe0] |
| GetFloorHeight mutable/const | LAB_140198130 | LEA RAX,[RCX+0x11c] |
| GetFlags mutable/const | LAB_140194ad0 | LEA RAX,[RCX+0x16c] |
| GetHealth mutable/const | LAB_140194ac0 | LEA RAX,[RCX+0x17a] |

Все перечисленные LEA stubs завершаются RET. Они подтверждают адрес storage относительно **наблюдаемого RCX**; ширина/тип следует из script declaration и ещё не составляет полного native layout. Rotation_s свойства x/y/z зарегистрированы с offsets 0/4/8; наблюдаемая роль x/pitch и z/yaw также видна в KPF expressions.

Evidence: [G_INSPECT_140194aa0](../evidence/ghidra_2022/G_INSPECT_140194aa0.json), [140194ac0](../evidence/ghidra_2022/G_INSPECT_140194ac0.json), [140198100](../evidence/ghidra_2022/G_INSPECT_140198100.json), [140198110](../evidence/ghidra_2022/G_INSPECT_140198110.json), [140198120](../evidence/ghidra_2022/G_INSPECT_140198120.json). E014 связывает overlapping windows с callback addresses и не называет LAB распознанной функцией.

**VERIFIED registration callbacks, поля пока UNKNOWN:**

| Script API | Callback |
|---|---|
| SetTarget / GetTarget | LAB_1401d35d0 / LAB_1401d3660 |
| SetSourceOwner / GetSourceOwner | LAB_1401a17c0 / LAB_1401a1860 |
| SetTaggedActor / GetTaggedActor | LAB_1401a1880 / LAB_1401a1920 |
| LinkSector(const int) | FUN_1401d3d70 |
| InflictDamage(actor, EnumDamageTypes, int) | FUN_1401a1370 |
| FireActor(kStr,kVec3) / FireActor(kStr,x,y,z) | FUN_1401a1500 / FUN_1401a1530 |
| CastToActor() | FUN_140058e50 |
| Remove() | FUN_140172f10 |

**INFERRED:** target, sourceOwner и taggedActor имеют разные роли и должны получить разные network IDs. Это особенно отличается от 2015 пути, где projectile.Target использовался как source. Exact lifetimes/native layouts неизвестны; нельзя объединять их ради краткости.

**Alias warning:** Remove() для всех трёх script types и Automap.IsAlreadyMapped() используют FUN_140172f10; CastToActor() для всех — FUN_140058e50. Это наблюдаемые alias pointers, не основание объявлять Remove действительным destroy API или CastToActor безопасным native cast. Нужны тела этих stubs. Const/mutable getters sharing one address также не доказывают mutation rights/lifetime в C++.

**VERIFIED:** scratchParam0..9 зарегистрированы через object-property calls в actor/puppet/AI routines; raw offset arguments сохранены. **UNKNOWN:** фактическое машинное использование каждой ячейки и безопасность replication; это не свободные поля для мода — scripts уже используют их.

## Enums: значения текущей сборки

Все 74 enum-value пары сохранены в E014. Избранные **VERIFIED registration literals**:

| Enum | Наблюдаемые значения |
|---|---|
| EnumActorFlags | AF_SOLID=0x2, AF_SHOOTABLE=0x20, AF_MOVEABLE=0x80, AF_TOUCHABLE=0x100, AF_INWATER=0x400, AF_HIDDEN=0x2000, AF_DISABLEFRICTION=0x10000000 |
| EnumDamageTypes | ADT_GENERIC=0, EXPLOSION=1, FIRE=2, ACID=3 |
| EnumPlayerButtons | ATTACK=1, JUMP=2, FORWARD=4, BACKWARD=8, STRAFELEFT=0x40, STRAFERIGHT=0x80, WEAPONRIGHT=0x100, WEAPONLEFT=0x200, USE=0x400, MAPZOOMIN=0x800, MAPZOOMOUT=0x1000 |
| EnumPlayerWeapons | MACHETE=0, PISTOL=1, M60=2, BOMBS=3, FLAMETHROWER=4, COBRASTAFF=5, RINGOFRA=6, BRACELET=7 |
| EnumPlayerAbilities | PAB_NONE=0, DOLPHIN=1, VULTURE=2 |
| EnumPlayerArtifacts | PA_NONE=0, SANDALS=1, MASK=2, SHAWL=4, ANKLETS=8, SCEPTER=0x10, FEATHER=0x20, ALL=0x3f |
| EnumPlayerFlags | PF_NONE=0, JUMPING=1, **FALLING=2**, NOCLIP=4, FLY=8, ABOVESURFACE=0x10, JUMPWASHELD=0x20, NEEDTOGASP=0x40, FLOATING=0x80, ELECTROCUTE=0x100, DEAD=0x200, GOD=0x400, STUNNED=0x800 |
| EnumDifficulty | NORMAL=0, HARD=1, PHARAOH=2 |

**VERIFIED difference:** 2015 source называл bit 2 `PF_USERJUMPED`, текущая регистрация — `PF_FALLING`. Имена и значения других flags нельзя автоматически переносить по сходству классов. Неперечисленные кнопки BC_LEFT/RIGHT отсутствуют в прочитанной enum registration, что не доказывает их отсутствия во всём движке.

## KPF: реальные single-player зависимости и actor-aware части

Источник: [E010_KPF_SELECTED_TEXT.json](../evidence/E010_KPF_SELECTED_TEXT.json), KPF SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`. Exact member SHA/строки/excerpts в E014. Это **VERIFIED текст поставленных данных**, но runtime reachability/compilation и вызовы не проверены.

| Member:строки | Статическое наблюдение | INFERRED значение |
|---|---|---|
| defs/actors/misc.txt:3–16 | Obj_Player -> classname kexSlavePuppet; radius90, height160, stepHeight66, health200, friction0.9125 | Реальный asset anchor для M0 spawn; не доказательство безопасного повторного создания |
| scripts/actorTypes.txt:18–21 | AT_PLAYER=0 | Числовой script type anchor; map/factory matching ещё проверять |
| scripts/main.txt:18–21 | включает p_misc/p_pspr/p_enemy | Эти scripts поставлены как main-module dependencies |
| p_misc.txt:74–105 | A_RandomItem смотрит Player().GetHealth и RNG | Drop decisions должны быть host-authoritative; нужен выбор policy «чьё health» |
| p_misc.txt:297–317 | projectile SourceOwner назначается Player().GetActor().CastToActor | Явная single-player ownership зависимость в data/scripts |
| p_misc.txt:372–419 | vase outcome зависит от Player artifacts/health | Нельзя оставить всем клиентам независимую локальную генерацию |
| p_pspr.txt:174–199 | CobraSlither читает actor.GetSourceOwner, owner.GetType | Уже actor-aware owner propagation |
| p_enemy.txt:54–95 | spider leap/push читает actor.GetTarget и задаёт velocity/scratch | AI target и mutable per-actor state влияют на gameplay |
| map/KILARENA_script.txt:18–26 | Player().GetArtifacts()=PA_ALL | Изменение глобального player/team progression |
| map/SELBUROW и SETARENA:20–35 | Player artifacts и GetActorFromMapActorIdx | Боссы/арены зависят от единого progression decision |
| map/SUNKEN_script.txt:21–40 | Player.GetKeys &1 | Общие ключи либо перенаправление local accessor |
| map/TOMBEND_script.txt:75–88,258–270 | Player.GetQuestItems vs0xFF | Условия прогресса/концовки — authority host |
| map/TOMB/TOMBEND | HoldsterWeapon/SetLockTime/SetShakeTime/GetButtons | Диалог/катсцена блокирует/читает одного player; правила co-op потребуют отдельного решения |

**VERIFIED:** current Obj_Player defs отличаются от 2015 hardcoded Spawn (radius95.25/step48/friction0.9375), поэтому старые movement constants не перенесены как текущие.

**Граница анализа:** в p_misc есть блок A_SetupFlame под `#if 0`. Его sourceOwner-подобные строки не использованы как доказательство выполняемого поведения. Равным образом scripts/map/old/ не являются доказательством активной версии level script.

## Значение для M0 и кандидаты дальнейшего анализа

**INFERRED:** script API содержит factory SpawnActor overloads и explicit actor arguments для damage, transform, sector, source/target. Это реальные интеграционные anchors. Но в изученной поверхности нет подтверждённого CreatePlayerController, SetPuppetOwner или N-player command routing, поэтому script-only полноценный co-op не доказан.

Приоритеты:

1. **UNKNOWN:** тело factory SpawnActor и spawn kexSlavePuppet; повторный spawn не должен перезаписать original controller.
2. **UNKNOWN:** player allocator/init, puppet owner field и формирование node at player+0xd8; helper197ee0 уже показывает обязательный indirection.
3. **UNKNOWN:** cmd GetButtons, input consumption, actor tick ordering; найти не только read-only accessors.
4. **UNKNOWN:** точный destroy/remove contract — alias Remove пока подозрителен.
5. **UNKNOWN:** node/sourceOwner/target reference lifecycle и clone/spawn dependencies.
6. **INFERRED design:** authoritative host должен выполнять gameplay scripts/RNG/AI/world transitions единожды; client presentation не должна повторять damage/spawn/frame actions.
7. **UNKNOWN:** достаточность prefab Obj_Player для remote visual avatar; defs не содержат доказанного подходящего player sprite/animation.
8. **UNKNOWN:** полный script surface за пределами четырёх registration routines, runtime-зарегистрированные additions и script state serialization.

Референс2015 используется только для сравнения/поисковых гипотез: [REF2015_PLAYER_LOOP.md](REF2015_PLAYER_LOOP.md). Его структуры и ABI сюда не переносились.

## Полная declaration-to-callback таблица

Ниже автоматически сгруппированы одинаковые script declaration+callback по scope. «LAB» сохраняет статус Ghidra label; native function recognition не заявляется. Для каждой строки source snippets/hash/xrefs и неизвестные условия см. E014. Namespace с address-only selector обозначается «?»: его строка не выдумывается.


| Scope | Script declaration literal | Callback label / preferred VA | RVA | File offset |
|---|---|---|---|---|
| ? | void delay(const float) | FUN_1401f1700 / 0x1401f1700 | 0x1f1700 | 0x1f0b00 |
| kexSlaveRandomizer | uint8 Bit() | FUN_1401e91b0 / 0x1401e91b0 | 0x1e91b0 | 0x1e85b0 |
| kexSlaveRandomizer | uint8 UInt8() | FUN_1401e9260 / 0x1401e9260 | 0x1e9260 | 0x1e8660 |
| kexSlaveRandomizer | uint16 UInt16() | FUN_1401e9320 / 0x1401e9320 | 0x1e9320 | 0x1e8720 |
| kexSlaveRandomizer | uint32 UInt32() | FUN_1401e9350 / 0x1401e9350 | 0x1e9350 | 0x1e8750 |
| kexSlaveRandomizer | int8 Int8() | LAB_1401e9310 / 0x1401e9310 | 0x1e9310 | 0x1e8710 |
| kexSlaveRandomizer | int16 Int16() | FUN_1401e9320 / 0x1401e9320 | 0x1e9320 | 0x1e8720 |
| kexSlaveRandomizer | int32 Int32() | FUN_1401e9350 / 0x1401e9350 | 0x1e9350 | 0x1e8750 |
| kexSlaveRandomizer | uint8 Dice() | FUN_1401e93a0 / 0x1401e93a0 | 0x1e93a0 | 0x1e87a0 |
| kexSlaveRandomizer | int8 RInt8() | FUN_1401e9470 / 0x1401e9470 | 0x1e9470 | 0x1e8870 |
| kexSlaveRandomizer | int16 RInt16() | FUN_1401e9480 / 0x1401e9480 | 0x1e9480 | 0x1e8880 |
| kexSlaveRandomizer | int32 RInt32() | FUN_1401e94b0 / 0x1401e94b0 | 0x1e94b0 | 0x1e88b0 |
| kexSlaveRandomizer | uint32 Max(const uint32) | FUN_1401e9510 / 0x1401e9510 | 0x1e9510 | 0x1e8910 |
| kexSlaveRandomizer | uint32 Size(const uint32) | FUN_1401e9580 / 0x1401e9580 | 0x1e9580 | 0x1e8980 |
| kexSlaveRandomizer | float Float() | FUN_1401e9670 / 0x1401e9670 | 0x1e9670 | 0x1e8a70 |
| kexSlaveRandomizer | float FloatVal(const float) | FUN_1401e96e0 / 0x1401e96e0 | 0x1e96e0 | 0x1e8ae0 |
| kexSlaveRandomizer | float RFloat() | FUN_1401e9700 / 0x1401e9700 | 0x1e9700 | 0x1e8b00 |
| kexSlaveRandomizer | float RFloatVal(const float) | FUN_1401e9720 / 0x1401e9720 | 0x1e9720 | 0x1e8b20 |
| kexSlaveRandomizer | float RFloatMinMax(const float, const float) | FUN_1401e9750 / 0x1401e9750 | 0x1e9750 | 0x1e8b50 |
| kexSlaveRandomizer | float Size(const float) | FUN_1401e9580 / 0x1401e9580 | 0x1e9580 | 0x1e8980 |
| kexSlaveEvents | void FireRemoteEventFromTag(const int) | FUN_1401be0b0 / 0x1401be0b0 | 0x1be0b0 | 0x1bd4b0 |
| kexSlaveEvents | void FireActorEventFromTag(const int) | FUN_1401bed90 / 0x1401bed90 | 0x1bed90 | 0x1be190 |
| kexSlaveEvents | void MoveScriptedSector(const int, const float, const float, const bool) | FUN_1401c0160 / 0x1401c0160 | 0x1c0160 | 0x1bf560 |
| kexSlaveEvents | void PlaySound(const kStr &in) | LAB_1401f1430 / 0x1401f1430 | 0x1f1430 | 0x1f0830 |
| kexSlaveEvents | void StopAllSounds(void) | FUN_1401f1450 / 0x1401f1450 | 0x1f1450 | 0x1f0850 |
| kexSlaveEvents | void PlayMusic(const kStr &in, const bool) | LAB_1401f14c0 / 0x1401f14c0 | 0x1f14c0 | 0x1f08c0 |
| kexSlaveEvents | void PlayMusicNoLoop(const kStr &in) | LAB_1401f14a0 / 0x1401f14a0 | 0x1f14a0 | 0x1f08a0 |
| kexSlaveEvents | void StopMusic(void) | LAB_1401f14e0 / 0x1401f14e0 | 0x1f14e0 | 0x1f08e0 |
| kexSlaveEvents | int PlayTactile(const kStr &in, int, int) | LAB_1401f14f0 / 0x1401f14f0 | 0x1f14f0 | 0x1f08f0 |
| kexSlaveEvents | bool IsTactilePlaying(int) | FUN_1402060b0 / 0x1402060b0 | 0x2060b0 | 0x2054b0 |
| kexSlaveEvents | bool IsNamedTactilePlaying(int, const kStr &in) | LAB_1401f1500 / 0x1401f1500 | 0x1f1500 | 0x1f0900 |
| kexSlaveEvents | void StopTactile(int) | FUN_140206180 / 0x140206180 | 0x206180 | 0x205580 |
| kexSlaveEvents | void StopAllTactile(void) | FUN_1402061e0 / 0x1402061e0 | 0x2061e0 | 0x2055e0 |
| kexSlaveEvents | void ChangeMap(const kStr &in) | LAB_1401f1510 / 0x1401f1510 | 0x1f1510 | 0x1f0910 |
| kexSlaveEvents | void EndGame(const bool) | FUN_1401f1570 / 0x1401f1570 | 0x1f1570 | 0x1f0970 |
| kexSlaveEvents | kexSlaveActor@ GetActorFromMapActorIdx(const int) | FUN_1401f1770 / 0x1401f1770 | 0x1f1770 | 0x1f0b70 |
| kexSlaveEvents | void OpenDoor(const int) | LAB_1401f15e0 / 0x1401f15e0 | 0x1f15e0 | 0x1f09e0 |
| kexSlaveEvents | void CloseDoor(const int) | LAB_1401f1610 / 0x1401f1610 | 0x1f1610 | 0x1f0a10 |
| kexSlaveEvents | void CloseDoorSlow(const int) | LAB_1401f1640 / 0x1401f1640 | 0x1f1640 | 0x1f0a40 |
| kexSlaveEvents | void LiftLowerWaitRaise(const int) | LAB_1401f1670 / 0x1401f1670 | 0x1f1670 | 0x1f0a70 |
| kexSlaveEvents | void LiftRaiseWaitLower(const int) | LAB_1401f16a0 / 0x1401f16a0 | 0x1f16a0 | 0x1f0aa0 |
| kexSlaveEvents | void FloorLower(const int) | LAB_1401f16d0 / 0x1401f16d0 | 0x1f16d0 | 0x1f0ad0 |
| ? | kexSlaveObjectFactory@ ObjectFactory() | LAB_1401f1240 / 0x1401f1240 | 0x1f1240 | 0x1f0640 |
| kexSlaveObjectFactory | kexSlaveActor@ SpawnActor(const int, const kVec3&in, const float, const int) | LAB_1401f9f18 / 0x1401f9f18 | 0x1f9f18 | 0x1f9318 |
| kexSlaveObjectFactory | kexSlaveActor@ SpawnActor(const kStr&in, const kVec3&in, const float, const int) | LAB_14019edd0 / 0x14019edd0 | 0x19edd0 | 0x19e1d0 |
| kexSlaveObjectFactory | kexSlaveActor@ SpawnActor(const int, const float, const float, const float, const float, const int) | FUN_14019ede0 / 0x14019ede0 | 0x19ede0 | 0x19e1e0 |
| kexSlaveObjectFactory | kexSlaveActor@ SpawnActor(const kStr&in, const float, const float, const float, const float, const int) | FUN_1401f11f0 / 0x1401f11f0 | 0x1f11f0 | 0x1f05f0 |
| ? | kexSlaveAutomap@ Automap() | LAB_1401f1250 / 0x1401f1250 | 0x1f1250 | 0x1f0650 |
| kexSlaveAutomap | void Toggle(const bool) | LAB_1401f9f10 / 0x1401f9f10 | 0x1f9f10 | 0x1f9310 |
| kexSlaveAutomap | void ToggleMapAll(const bool, const bool) | LAB_1401f9f08 / 0x1401f9f08 | 0x1f9f08 | 0x1f9308 |
| kexSlaveAutomap | bool IsEnabled() const | LAB_1401f9f20 / 0x1401f9f20 | 0x1f9f20 | 0x1f9320 |
| kexSlaveAutomap | bool IsAlreadyMapped() const | FUN_140172f10 / 0x140172f10 | 0x172f10 | 0x172310 |
| ? | int GetDifficulty() | LAB_1401f1530 / 0x1401f1530 | 0x1f1530 | 0x1f0930 |
| ? | uint GetLanguageID() | FUN_1401f1550 / 0x1401f1550 | 0x1f1550 | 0x1f0950 |
| ? | kexSlavePlayer@ Player() | LAB_1401f1260 / 0x1401f1260 | 0x1f1260 | 0x1f0660 |
| kexSlavePlayer | EnumPlayerArtifacts& GetArtifacts() | LAB_1401ae080 / 0x1401ae080 | 0x1ae080 | 0x1ad480 |
| kexSlavePlayer | EnumPlayerAbilities& GetAbilities() | LAB_1401ae090 / 0x1401ae090 | 0x1ae090 | 0x1ad490 |
| kexSlavePlayer | EnumPlayerButtons GetButtons() const | FUN_1401f1180 / 0x1401f1180 | 0x1f1180 | 0x1f0580 |
| kexSlavePlayer | int16 GetHealth() const | FUN_1401dfa20 / 0x1401dfa20 | 0x1dfa20 | 0x1dee20 |
| kexSlavePlayer | const int16& GetKeys() const | LAB_1401ae0c0 / 0x1401ae0c0 | 0x1ae0c0 | 0x1ad4c0 |
| kexSlavePlayer | int16& GetKeys() | LAB_1401ae0c0 / 0x1401ae0c0 | 0x1ae0c0 | 0x1ad4c0 |
| kexSlavePlayer | const int16& GetQuestItems() const | LAB_1401ae0a0 / 0x1401ae0a0 | 0x1ae0a0 | 0x1ad4a0 |
| kexSlavePlayer | int16& GetQuestItems() | LAB_1401ae0a0 / 0x1401ae0a0 | 0x1ae0a0 | 0x1ad4a0 |
| kexSlavePlayer | const uint32& GetTeamDolls() const | LAB_1401ae0b0 / 0x1401ae0b0 | 0x1ae0b0 | 0x1ad4b0 |
| kexSlavePlayer | uint32& GetTeamDolls() | LAB_1401ae0b0 / 0x1401ae0b0 | 0x1ae0b0 | 0x1ad4b0 |
| kexSlavePlayer | kexSlavePuppet@ GetActor() | LAB_140194b30 / 0x140194b30 | 0x194b30 | 0x193f30 |
| kexSlavePlayer | void HoldsterWeapon() | FUN_1401e0bb0 / 0x1401e0bb0 | 0x1e0bb0 | 0x1dffb0 |
| kexSlavePlayer | void SetLockTime(const int) | LAB_1401f1190 / 0x1401f1190 | 0x1f1190 | 0x1f0590 |
| kexSlavePlayer | void SetShakeTime(const int) | LAB_140198140 / 0x140198140 | 0x198140 | 0x197540 |
| kexSlavePlayer | bool IsWeaponOwned(const EnumPlayerWeapons) const | FUN_1401ae020 / 0x1401ae020 | 0x1ae020 | 0x1ad420 |
| kexSlavePuppet | void Stun() | FUN_1401dc020 / 0x1401dc020 | 0x1dc020 | 0x1db420 |
| kexSlaveAI | float& GetMoveSpeed() | LAB_1401f11a0 / 0x1401f11a0 | 0x1f11a0 | 0x1f05a0 |
| kexSlaveAI | float& GetMeleeExtraDistance() | LAB_1401f11b0 / 0x1401f11b0 | 0x1f11b0 | 0x1f05b0 |
| kexSlaveAI | float& GetSightDistance() | LAB_1401f11c0 / 0x1401f11c0 | 0x1f11c0 | 0x1f05c0 |
| kexSlaveAI | float& GetTurnSpeed() | LAB_1401f11d0 / 0x1401f11d0 | 0x1f11d0 | 0x1f05d0 |
| kexSlaveAI | int& GetPainChance() | LAB_1401f11e0 / 0x1401f11e0 | 0x1f11e0 | 0x1f05e0 |
| kexSlaveLevelScript | void CallDelayed(const kStr &in, kexSlaveActor@, const float) | LAB_1401f1280 / 0x1401f1280 | 0x1f1280 | 0x1f0680 |
| kexSlaveLevelScript | void CallDelayed(const int, kexSlaveActor@, const float) | LAB_1401f1290 / 0x1401f1290 | 0x1f1290 | 0x1f0690 |
| kexSlaveLevelScript | void Halt(const int) | FUN_1401f12a0 / 0x1401f12a0 | 0x1f12a0 | 0x1f06a0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void Remove() | FUN_140172f10 / 0x140172f10 | 0x172f10 | 0x172310 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | slaveRotation_s& GetRotation() | LAB_140198100 / 0x140198100 | 0x198100 | 0x197500 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kVec3& GetOrigin() | LAB_140194aa0 / 0x140194aa0 | 0x194aa0 | 0x193ea0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const kVec3& GetOrigin() const | LAB_140194aa0 / 0x140194aa0 | 0x194aa0 | 0x193ea0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int& GetSector() | LAB_140198110 / 0x140198110 | 0x198110 | 0x197510 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float GetRadius() const | FUN_1401a16d0 / 0x1401a16d0 | 0x1a16d0 | 0x1a0ad0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float GetHeight() const | FUN_1401a1710 / 0x1401a1710 | 0x1a1710 | 0x1a0b10 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float GetStepHeight() const | LAB_1401a1750 / 0x1401a1750 | 0x1a1750 | 0x1a0b50 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int GetType() const | LAB_1401a17a0 / 0x1401a17a0 | 0x1a17a0 | 0x1a0ba0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int16& GetHealth() | LAB_140194ac0 / 0x140194ac0 | 0x194ac0 | 0x193ec0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const int16& GetHealth() const | LAB_140194ac0 / 0x140194ac0 | 0x194ac0 | 0x193ec0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float& GetScale() | LAB_1401a3e90 / 0x1401a3e90 | 0x1a3e90 | 0x1a3290 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const float& GetScale() const | LAB_1401a3e90 / 0x1401a3e90 | 0x1a3e90 | 0x1a3290 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int& GetFlags() | LAB_140194ad0 / 0x140194ad0 | 0x194ad0 | 0x193ed0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const int& GetFlags() const | LAB_140194ad0 / 0x140194ad0 | 0x194ad0 | 0x193ed0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kVec3& GetColor() | LAB_1401e97a0 / 0x1401e97a0 | 0x1e97a0 | 0x1e8ba0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const kVec3& GetColor() const | LAB_1401e97a0 / 0x1401e97a0 | 0x1e97a0 | 0x1e8ba0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | uint8& GetTransparency() | LAB_1401e97b0 / 0x1401e97b0 | 0x1e97b0 | 0x1e8bb0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const uint8& GetTransparency() const | LAB_1401e97b0 / 0x1401e97b0 | 0x1e97b0 | 0x1e8bb0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kVec3& GetVelocity() | LAB_140198120 / 0x140198120 | 0x198120 | 0x197520 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const kVec3& GetVelocity() const | LAB_140198120 / 0x140198120 | 0x198120 | 0x197520 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float& GetFloorHeight() | LAB_140198130 / 0x140198130 | 0x198130 | 0x197530 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const float& GetFloorHeight() const | LAB_140198130 / 0x140198130 | 0x198130 | 0x197530 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float& GetCeilingHeight() | LAB_1401a3ea0 / 0x1401a3ea0 | 0x1a3ea0 | 0x1a32a0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const float& GetCeilingHeight() const | LAB_1401a3ea0 / 0x1401a3ea0 | 0x1a3ea0 | 0x1a32a0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor@ FireActor(const kStr&in, const kVec3&in) const | FUN_1401a1500 / 0x1401a1500 | 0x1a1500 | 0x1a0900 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor@ FireActor(const kStr&in, const float, const float, const float) const | FUN_1401a1530 / 0x1401a1530 | 0x1a1530 | 0x1a0930 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void SetFrameID(const int16) | LAB_1401f1170 / 0x1401f1170 | 0x1f1170 | 0x1f0570 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int16 GetFrameID(void) const | LAB_1401e97c0 / 0x1401e97c0 | 0x1e97c0 | 0x1e8bc0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void PlaySound(const kStr &in) | LAB_140194ab0 / 0x140194ab0 | 0x194ab0 | 0x193eb0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void SetTarget(kexSlaveActor@) | LAB_1401d35d0 / 0x1401d35d0 | 0x1d35d0 | 0x1d29d0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor @GetTarget(void) | LAB_1401d3660 / 0x1401d3660 | 0x1d3660 | 0x1d2a60 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void SetSourceOwner(kexSlaveActor@) | LAB_1401a17c0 / 0x1401a17c0 | 0x1a17c0 | 0x1a0bc0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor @GetSourceOwner(void) | LAB_1401a1860 / 0x1401a1860 | 0x1a1860 | 0x1a0c60 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void SetTaggedActor(kexSlaveActor@) | LAB_1401a1880 / 0x1401a1880 | 0x1a1880 | 0x1a0c80 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor @GetTaggedActor(void) | LAB_1401a1920 / 0x1401a1920 | 0x1a1920 | 0x1a0d20 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void LinkSector(const int) | FUN_1401d3d70 / 0x1401d3d70 | 0x1d3d70 | 0x1d3170 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | int GetGameTicks() const | LAB_14019edc0 / 0x14019edc0 | 0x19edc0 | 0x19e1c0 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | kexSlaveActor@ CastToActor() | FUN_140058e50 / 0x140058e50 | 0x58e50 | 0x58250 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void ChangeAnim(const kStr &in) | LAB_1401a1360 / 0x1401a1360 | 0x1a1360 | 0x1a0760 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | const float GetCollidedWallAngle() const | LAB_1401f1160 / 0x1401f1160 | 0x1f1160 | 0x1f0560 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | bool CanSeeLocation(const kVec3&in, const float) const | FUN_1401a0b50 / 0x1401a0b50 | 0x1a0b50 | 0x19ff50 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void ClearIgniteFlames() | FUN_1401a0f20 / 0x1401a0f20 | 0x1a0f20 | 0x1a0320 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void MarkCheckpoint() const | FUN_1401a1090 / 0x1401a1090 | 0x1a1090 | 0x1a0490 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | void InflictDamage(kexSlaveActor@, const EnumDamageTypes, const int) | FUN_1401a1370 / 0x1401a1370 | 0x1a1370 | 0x1a0770 |
| kexSlaveActor, kexSlavePuppet, kexSlaveAI | float GetCollidableRadius(const float) const | FUN_140194ae0 / 0x140194ae0 | 0x194ae0 | 0x193ee0 |
