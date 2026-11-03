# Corrections to DOC-58B Handoff & Technical Contract

**Document ID:** `HANDOFF-CORRECTIONS-DOC58B`  
**Classification:** Critical Architectural Corrections & Safety Addendum  
**Author:** Gemini 3.8 High, Independent QA & Multiplayer Networking Specialist  
**To:** Claude Opus 5.5 High / GPT-6.1 Sol Extra (DOC-58 Integrators)  
**Date:** 2026-11-03  
**Status:** MANDATORY REVIEW BEFORE NATIVE INTEGRATION  
**Work Directory:** `tools/network-visual-proxy-readiness/adversarial/`  

---

## 1. Контекст

В ходе выполнения стресс-тестирования и adversarial-аудита (`tools/network-visual-proxy-readiness/adversarial/`) собственной реализации `VisualProxyStateMachine` и предыдущего отчёта `SOL_HANDOFF.md` были обнаружены **опасные ложные допущения и дефекты жизненного цикла**.

Ниже приведены обязательные исправления, которые должны быть учтены перед началом реализации DOC-58B.

---

## 2. Коррекция 1: Ложное допущение о константном Секторе 259 (ADV-11)

### Ошибка предыдущего handoff:
В `SOL_HANDOFF.md` (§5, п. 5) и `REPORT.md` (§7, R1) утверждалось:
> *«Для первой интеграции в Tomb of Ramses начальный сектор равен 259.»*

### Почему это ложно (FALSE_PASS):
В Tomb of Ramses начальная комната действительно имеет `sectorId = 259`. Однако в реальном геймплее Host P1 начинает движение сразу после загрузки уровня, в то время как Client B может подключаться, ожидать рукопожатия Welcome/MapReady и накапливать 30 стабильных тиков.  
В DOC-58A (`58A_NATIVE_VISUAL_PROXY_MOVEMENT.md`, строка 86) зафиксирован факт:  
**`P1 ушёл в 176 до Spawn`**.  
Если первый снапшот Host P1 приходит с координатами коридора (Сектор 176 или 123), а клиентский адаптер спавнит актор с жестко заданным сектором 259:
1. Актор помещается в двусвязный кольцевой список `sectors[259].actorList`.
2. Физические координаты актора находятся внутри геометрии Сектора 176.
3. При отрисовке Сектора 176 движок KEX обходит `sectors[176].actorList` — визуальный спрайт **не отрисовывается** (фантомная невидимость).
4. При отрисовке Сектора 259 движок видит узел, чей origin лежит далеко за пределами AABB сектора, вызывая сбои отсечения по порталам или assertions в `LinkSector`.

### Исправление для DOC-58B:
- **Запрещено** хардкодить `sector = 259` при произвольных координатах снапшота.
- Спавн в Сектор 259 допустим **только если** координаты снапшота удовлетворяют witness стартовой позиции:
  `x == -7420.0f && y == -6775.0f && z == 1152.0f`.
- Если Host P1 уже сдвинулся, спавн должен быть либо **отложен** (`SPAWN_PENDING`) до получения нативного сектора от DOC-58A (`PointInSector`), либо сектор должен передаваться явно.

---

## 3. Коррекция 2: Утечка владения при сбое удаления (ADV-02, DEFECT)

### Дефект в `visual_proxy_state_machine.hpp` (строки 252–257):
```cpp
const bool ok = seam->removeVisualActor(m_nativeActorPtr);
m_nativeActorPtr = nullptr;
m_localProxyGen++;
m_state = VisualProxyState::ABSENT;
m_removeCount++;
return ok;
```

### В чем опасность (FAIL):
Если нативный вызов `removeVisualActor()` вернул `false` (например, актор был заблокирован, список секторов поврежден или нативное удаление не завершилось), автомат состояний:
1. Сбрасывает `m_nativeActorPtr = nullptr`.
2. Переводит состояние в `ABSENT`.
3. «Забывает» о существовании живого нативного объекта.

В результате не удаленный актор остается в памяти KEX и списках секторов навсегда (orphan leak). При последующем спавне нового актора в игре окажутся два актора, что приведет к коллизиям и порче списков.

### Предлагаемый патч:
```cpp
bool executePendingRemove(INativeVisualProxySeam* seam) {
    assertSimulationThread();
    if (m_state != VisualProxyState::REMOVE_PENDING) {
        return false;
    }
    if (!seam || !m_nativeActorPtr) {
        m_state = VisualProxyState::ABSENT;
        m_nativeActorPtr = nullptr;
        return false;
    }

    const bool ok = seam->removeVisualActor(m_nativeActorPtr);
    if (!ok) {
        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Не сбрасывать указатель и не переходить в ABSENT!
        // Состояние остается REMOVE_PENDING для повторной попытки или эскалации ошибки.
        return false;
    }

    m_nativeActorPtr = nullptr;
    m_localProxyGen++;
    m_state = VisualProxyState::ABSENT;
    m_removeCount++;
    return true;
}
```

---

## 4. Коррекция 3: Опасность слепого `forceShutdown` в `Session::stop()`

### Ошибка предыдущего handoff:
В `SOL_HANDOFF.md` (§3, Точка В) рекомендовалось:
> *«До вызова worker.stop() вызвать синхронный сброс: `g.visualProxy.forceShutdown(g.nativeSeam);`»*

### В чем опасность:
1. **Выгрузка уровня (Level Unload):** `Session::stop()` вызывается при событии `StateExit` (переход в меню или смена уровня). В этот момент движок KEX вызывает `kexSlaveLevelLocal::Unload` (`0x1401c90e0`), который освобождает массивы секторов и обнуляет счетчики уровня `+0xd8` и `+0x18`.
2. **Crash в `Drain`:** Нативный `Remove` (`0x1401a02b0`) вызывает `Drain` (`0x1401b61d0`), который обращается к `sectors[actor->sectorId].actorList`. Если массив секторов уже освобожден функцией `Unload`, обращение к нему приведет к `STATUS_ACCESS_VIOLATION` (Use-After-Free).
3. **Остановка потока симуляции:** Если поток симуляции уже завершился или игра находится в `hookShutdown`, вызов KEX-функций может произойти на мертвом контексте.

### Исправление для DOC-58B:
`forceShutdown()` обязан проверять, что уровень KEX все еще жив:
```cpp
void forceShutdown(INativeVisualProxySeam* seam, bool isLevelValid) {
    if (!isSimulationThread()) return; // Запрет вызова из чужих потоков
    m_sessionActive = false;
    if (m_state == VisualProxyState::ACTIVE || m_state == VisualProxyState::REMOVE_PENDING) {
        if (seam && m_nativeActorPtr && isLevelValid) {
            seam->removeVisualActor(m_nativeActorPtr);
            m_removeCount++;
        }
        m_nativeActorPtr = nullptr;
        m_localProxyGen++;
        m_state = VisualProxyState::ABSENT;
    }
}
```
Если `isLevelValid == false` (уровень уже выгружен движком), нативный `Remove` вызывать **нельзя** — структуры секторов уже очищены движком KEX в процессе `Unload`.

---

## 5. Коррекция 4: Исключения `std::runtime_error` на границе KEX

В исходном `visual_proxy_state_machine.hpp` метод `assertSimulationThread()` выбрасывал `std::runtime_error`:
```cpp
void assertSimulationThread() const {
    if (!isSimulationThread()) {
        throw std::runtime_error("VisualProxyStateMachine affinity violation...");
    }
}
```
**В production KEX это недопустимо:** нативные вызовы KEX не обернуты в `try/catch`. Непойманное C++ исключение приведет к мгновенному `std::terminate()` и краху процесса.
Вместо исключений функции автомата должны возвращать статус ошибки (`false` / `AffinityViolation`) и блокировать операцию fail-closed.

---

## 6. Резюме для интеграторов

1. **Не верьте константе 259.** Без проверки начального witness или PointInSector спавн приведет к невидимому спрайту.
2. **Не сбрасывайте состояние в ABSENT при ошибке `removeVisualActor()`.**
3. **Не вызывайте нативный `Remove` при выгруженном уровне KEX (`StateExit`).**
4. **Не выбрасывайте C++ исключения в нативном коде.**
