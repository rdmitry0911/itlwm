# Roam/scan arbitration — reference contract mapped to the WIP lifecycle

Срез: 2026-09-12, продолжение WIP `83a2f617` / консолидация `50fdd147`.
Источник: read-only decompile `5995e24caa` на `10.7.6.112`, пакет
`/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.x4SovM`.
Это **карта эталона**, не production-исправление и не runtime-утверждение.

> **ИСПРАВЛЕНО (свёрено с предыдущим агентом + первичное чтение
> `setForceRoamMsg`/`roamStart`).** Первая редакция этого файла ошибочно
> утверждала, что событие `0x89` взводит high-priority-reassoc окно 3000мс и
> потому одной публикации `0x89/0x8b/0x50` достаточно для подавления scan-over-roam.
> Это НЕВЕРНО. Точная механика — в разделе «Механика» ниже. Публикации `0x89`
> самой по себе НЕдостаточно для обычного (не-low-RSSI, не-force) roam.

## Итог (что именно исправляет расхождение) — исправленный

Публичный CoreWLAN scan доходит до драйверного `setWCL_SCAN_REQ`, потому что
верхний gate `WCLScanManager::isScanAllowedByOtherActivity` вернул «разрешено»:
для обычного departure-roam (нормальный RSSI, без force-roam) НИ high-priority
окно, НИ low-RSSI-gate не срабатывают. На Broadcom скан тоже был бы разрешён в
этом случае — но firmware/драйвер не рушит из-за него свой roam. На Intel же
`setWCL_SCAN_REQ` **жёстко отменяет** принятый reassoc-owner (ECANCELED),
теряя target-переход.

Значит расхождение — **в драйверной обработке скана поверх живого roam**, а не в
одной лишь публикации событий. Основное исправление = арбитраж в драйвере: не
рушить принятый roam при public scan (coalesce/defer/дать roam завершить target),
как это делает эталон. Публикация `0x89/0x8b/0x50` — необходимая, но НЕ достаточная
часть; она включает верхний gate только для force-roam (окно 3000мс) и для
low-RSSI-roam (FSM ROAM_SCAN + rssi < −69, кроме LOW_LATENCY-сканов).

Остаток — **runtime-квалификация + арбитраж**: собрать, поставить в disposable
guest, воспроизвести scan-over-roam (два внешних LabAP BSS: source `…:51:ca`/ch9,
target `9a:fb:5d:97:a9:02`/ch13), и реализовать драйверный арбитраж так, чтобы
принятый roam переживал public scan честно.

## Механика (первичное чтение, подтверждено)

Приватные ivars `p = WCLRoamManager + 0x20`:

| Механизм | Что меняет |
|---|---|
| `setForceRoam(force,highPrio)` / `setForceRoamMsg` (2 байта {highPrio,force}) | `p+0x3c`=force, `p+0x3d`=highPrio, `p+0x40`=timestamp; при force=0 всё чистит |
| `0x89 → roamStart` | FSM→ROAM_SCAN; зануляет `p+0x58..0xb8`; `p+0x78`=rssi; `p+0x60`=timestamp (НЕ `+0x40`); protection timer **10000мс** |
| `0x50 → roamDone` | завершает FSM; снимает protection timer; чистит `p+0x3c/0x3d/0x40` |

`getRoamState` (24B roamManagerInfo) читает: `[0x00]`=`p+0x40` (start для окна 3000мс,
ставится ТОЛЬКО force-roam), `[0x0d]`=high-priority (`p+0x3c && p+0x3d`),
`[0x0c]`=low-rssi-roaming (`rssi<−69 && FSM-roaming`), `[0x0e]`=`p+0x3c`.
Т.е. high-priority-reassoc-окно управляется force-roam, а FSM-состоянием (от `0x89`)
управляется только low-rssi-ветка gate.

## `WCLScanManager::sendRequest` (ffffff80020fb4f6)

1. Аллоцирует 0x1550-байтный scan request.
2. Читает op mode у драйвера (query 0xf). Если `& 0x1800000000` → REJECT 0x10
   («Rejecting scan request because of interface's operational mode»).
3. `iVar = isScanAllowedByOtherActivity(this, req, &flags)`.
   - `iVar != 0` → лог «scan is blocked by other system activity %d», **возврат
     наружу; драйверный setter НЕ вызывается**; roam не тронут.
   - `iVar == 0` → берёт scan request для драйвера и вызывает драйвер
     (`FUN_…0fbfd6` → setWCL_SCAN_REQ на Intel) → планирует (query 0x1b9).
   Никакой автоматической очереди busy-запросов в consumer нет.

## `isScanAllowedByOtherActivity` (ffffff80020fb842)

Собирает состояние через bulletin-board query `FUN_…20e8530` (тип в
`0xaaaaaaaa00XX0003`), затем строгий порядок отказа:

| Источник (query) | Поле | Условие отказа | flag | лог |
|---|---|---|---|---|
| netManagerInfo 0x0f (5B) → uStack_70 | bit 0x100 | IP resolution in progress | 8 | «IP Resolution in progress» |
| roamManagerInfo 0x07 (0x18) → uStack_88.. | см. ниже | | | |
| join 0x09 (8B) → uStack_90 | bit 1 | join manager busy | 2 | «join manager is busy» |
| ndd 0x1c (8B) → uStack_a0 | bit 1 | ndd manager busy | 4 | «ndd manager is busy» |
| GAS 0x0a (8B) → uStack_98 | bit 1 | GAS manager busy | 1 | «GAS manager is busy» |

Порядок проверок (первое сработавшее возвращает !=0):
1. GAS busy (uStack_98&1) → 0x10, flag 1.
2. ndd busy (uStack_a0&1) → -0x1f7dfbbe, flag 4.
3. join busy (uStack_90&1) → -0x1f7ddbff, flag 2.
4. IP resolution (uStack_70&0x100) → flag 8 (ставит таймстемпы +0x28/+0x30).
5. **high-priority reassoc**: `uStack_80 & 0x10000000000` (bit 40 = roamInfo байт
   0x0d bit0). Если `FUN_…0fc93a(uStack_88) < 3000` (мс с reassoc-start) → REFUSE,
   flag 0x10, лог «high priority reassoc in progress … Reassoc start time [%llu]».
   После 3000мс → allowed. **← это ключевой gate для scan-over-roam.**
6. **low-RSSI roaming**: `uStack_80 & 0x100000000` (bit 32 = байт 0x0c bit0) И
   scan НЕ low-latency (`req[+0x1310] & 0x20 == 0`) → REFUSE, flag 0x20,
   «Low Rssi Roaming in progress … except if LOW_LATENCY scan».
7. **link-loss suppression**: `uStack_70 & 0x10000` → REFUSE, flag 0x40.
8. associated-waiting-IP (flag 8), BT-call busy (flag 0x80),
   low-signal-could-roam (rssi ≤ порог → param_3[1]|=1).

## `WCLRoamManager::getRoamState` (ffffff8002105182) — 24-байтный roamManagerInfo

Заполняет буфер (`puVar5`) из внутренних полей менеджера (`lVar3 = this+0x20`):

| off | тип | значение | источник |
|---|---|---|---|
| 0x00 | u64 | roam/reassoc **start time** (мс) | `*(this+0x20+0x40)` |
| 0x08 | i32 | текущий RSSI (пример 0xffffffb5=-75) | `0xffffffb5` литерал в декомпиле |
| 0x0c | bool | low-rssi-roaming | `rssi < -69 && roaming_active(+0x10.bit)` |
| 0x0d | bool | **high-priority-reassoc active** | `(this+0x3c &1) && (this+0x3d &1)` |
| 0x0e | byte | roaming/reassoc in progress | `this+0x3c & 1` |
| 0x0f | byte | доп. состояние | `FUN_…222663a(this+8)` |
| 0x10 | bool | FSM state == 2 | `(*(this+0x10+0x10) & 0xfe)==2` |
| 0x11 | bool | link/other | `state==0 && netmgr[+0x16bc]==0` |

**ИСПРАВЛЕНИЕ строки-источника выше:** `this+0x3c/0x3d/0x40` взводит ТОЛЬКО
force-roam (`setForceRoamMsg`/`setForceRoam`); `0x89`→`roamStart` их НЕ трогает
(пишет `this+0x60` и FSM-состояние). Поэтому «start time» `[0x00]` окна 3000мс
относится к force-roam, а от `0x89` зависит лишь low-rssi-ветка `[0x0c]`
(FSM-roaming && rssi<−69). Колонка «источник» в строке 0x0d/0x0e корректна по
ПОЛЯМ, но их писатель — force-roam, не `0x89`.

## Что это значит для Intel-порта — исправленный

1. Тот же Apple WCLScanManager/WCLRoamManager работают НАД AirportItlwm; события
   драйвера кормят их FSM. НО: обычный departure-roam (нормальный RSSI, без
   force-roam) НЕ поднимает ни high-priority-окно, ни low-rssi-ветку → gate
   разрешает скан → на Intel `setWCL_SCAN_REQ` рушит roam. Публикации `0x89`
   недостаточно для этого случая.
2. Значит нужен **драйверный арбитраж**: при public scan поверх принятого roam
   не отменять reassoc-owner жёстко (WIP remaining #2). Возможные направления —
   coalesce (roam-скан обслуживает и public-запрос), defer, либо дать roam
   завершить target перед сканом. Точная эталонная реакция на «скан поверх
   обычного roam» (что делает Broadcom/firmware) — открытый вопрос для чтения:
   `WCLScanManager::scanRequestHandler`/`handleScanRequest`/`abortScan` и
   как firmware совмещает roam-scan с public escan.
3. `setWCL_SCAN_REQ` НЕ менять спекулятивно на reject: в consumer нет очереди/ретрая,
   reject даст молчаливо пустой GUI-скан. Сначала измерить runtime, потом решать.
4. Остаток implementation-долга WIP (независимо): заполнить 168-байтный `0x50`
   carrier (flags/authType/AKM/PHY/chanspec/channelsScanned — по реальным
   наблюдениям, не выдумывать firmware-stats). Точная карта полей — в
   `printRoamStatus` (ниже).

## Поля 168-байтного `apple80211_message_roam_status` (из printRoamStatus)

Индексы по `uint32 *param_2` (0x50-carrier), первичное чтение printRoamStatus:

| idx/off | поле | idx/off | поле |
|---|---|---|---|
| [0]/0x00 | status | [0x10]/0x40 | FROM authType |
| [1]/0x04 | reason | [0x11]/0x44 | TO authType |
| [2]/0x08 | start time (u64) | [0x12]/0x48 | FROM AKMs |
| [4]/0x10 | end time (u64) | [0x13]/0x4c | TO AKMs |
| [6]/0x18 | flags | [0x14]/0x50 | FROM phyMode |
| [7]/0x1c | profile | [0x15]/0x54 | TO phyMode |
| [8]/0x20 | FROM rssi | 0x38..0x3a | FROM oui(3) |
| [9]/0x24 | TO rssi | 0x3b..0x3d | TO oui(3) |
| [10]/0x28 | FROM channel | 0x58..0x5d | FROM bssid(6) |
| [11]/0x2c | TO channel | 0x5e..0x63 | TO bssid(6) |
| [12]/0x30 | FROM chan flags | 0x6a (u16) | channelsScannedCount |
| [13]/0x34 | TO chan flags | | |

WIP уже корректно пишет status/reason/start/end/rssi/channel/oui(0x38,0x3b)/
bssid(0x58,0x5e). НЕ заполнены (нули): flags, profile, chan-flags, authType,
AKMs, phyMode, channelsScanned.

## Файлы эталона (10.7.6.112, x4SovM/contract)

- `…WCLScanManager11sendRequestEP14WCLScanRequest.c`
- `…WCLScanManager28isScanAllowedByOtherActivityEP14WCLScanRequestRj.c`
- `…WCLRoamManager12getRoamStateER20bulletinBoardMessage.c`
- сопутствующие: `handleScanRequest`, `scanRequestHandler`, `abortScan`,
  `handleReassocEvent`, `handleRoamDoneEvent`, `startRoamScan`, `sendReassocToDriver`.
Manifest SHA256 `ce2cac9db82eed8be5e9138ba8de9e7cca14a4c206a2aa0cb8f1654538760d6d`.

## ДОБАВЛЕНО (reference-first, по указанию: только идентичность, никаких костылей)

Прочитан драйверный контакт-surface эталона
`AppleBCMWLANScanAdapter::startScan` (ffffff80016aca5a):

- startScan НЕ арбитрирует roam-vs-scan. Он отклоняет только при
  action-frame-in-progress (`*(core+0x128)+0x4478 & 1` → 0xe00002d5
  «Action frame in progress. Rejecting escan request!»), иначе выполняет escan
  (`startEventScan`, когда mode==2). Никаких проверок roam/reassoc в драйвере.
- Весь roam-vs-scan gate живёт ВЫШЕ драйвера — в
  `WCLScanManager::isScanAllowedByOtherActivity`.

**Следствие для идентичности (важно):** то, что itlwm `setWCL_SCAN_REQ`
самостоятельно отменяет принятый roam (`ieee80211_cancel_wcl_reassoc_bgscan`),
— это ДИВЕРГЕНЦИЯ контакт-surface: эталонный драйвер так не делает. Идея
«defer scan behind roam» в драйвере — ТОЖЕ не подтверждена эталоном (startScan
не откладывает и не арбитрирует). Поэтому НИ отмену, НИ отложенную очередь в
драйвере реализовывать нельзя как «политику».

**Настоящая цель = идентичность контакт-surface**: itlwm должен предъявлять
верхнему WCL-слою те же селекторы/события/состояние, что и AppleBCMWLAN, чтобы
арбитраж делал ТОТ ЖЕ верхний слой (isScanAllowedByOtherActivity/getRoamState),
а `setWCL_SCAN_REQ` вёл себя как startScan (выполнял скан, без своей отмены).

**Открытый исследовательский вопрос (следующий этап, proof-first):**
хостит ли itlwm реальные WCLScanManager/WCLRoamManager (как AppleBCMWLAN), или
переизобретает их? От этого зависит, где именно достигать идентичности:
- если WCL-менеджеры общие/выше — фикс в том, чтобы itlwm правильно кормил их
  roam-состоянием (getRoamState/bulletin), и убрать драйверную самоотмену;
- физическое ограничение Intel (один scan-engine) — это ВНУТРЕННЯЯ реализация,
  но она обязана давать идентичный контакт-surface (какие события видит
  WCLRoamManager, когда скан пересекается с roam на эталоне — читать далее).

Ресурс (по указанию пользователя, на 10.7.6.112, НЕ 10.7.6.11 — недоступен):
`/home/dima/Projects/ghidra_decompiler_optimization` (пофикшенная Ghidra),
`/home/dima/Projects/ghidra_input/BootKC_guest_25C56.kc` (точный guest-build KC),
`itlwm_guest_source_snapshot_cr479_*` (снапшот исходников), исчерпывающие
скрипты в `/home/dima/Projects/ghidra_additional` (AIAMWiFiExhaustiveDecompile.java).
