# Roam/scan arbitration — reference contract mapped to the WIP lifecycle

Срез: 2026-09-12, продолжение WIP `83a2f617` / консолидация `50fdd147`.
Источник: read-only decompile `5995e24caa` на `10.7.6.112`, пакет
`/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.x4SovM`.
Это **карта эталона**, не production-исправление и не runtime-утверждение.

## Итог (что именно исправляет расхождение)

Публичный CoreWLAN scan отменяет принятый IWN roam НЕ потому, что `setWCL_SCAN_REQ`
«агрессивен», а потому, что верхний слой Apple (`WCLScanManager::sendRequest`)
доходит до драйверного setter'а — то есть его gate `isScanAllowedByOtherActivity`
вернул «разрешено». На Broadcom тот же gate ОТКАЗЫВАЕТ в скане на время активного
roam. Значит **основное исправление — не в setter'е, а в публикации roam-lifecycle
событий**, которые кормят FSM `WCLRoamManager`, так что `getRoamState` рапортует
активный high-priority-reassoc, и `sendRequest` отклоняет скан ДО драйвера.

Это ровно то, что строит WIP (события `0x89`/`0x8b`/`0x50`). Остаток —
**runtime-квалификация**: собрать, поставить в disposable guest, воспроизвести
scan-over-roam и убедиться, что скан подавляется (а не отменяет roam).

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

Т.е. `this+0x3c` (roam active) и `this+0x40` (start time) взводятся при входе в
ROAM_SCAN (consume `0x89`), сбрасываются при roamDone (`0x50`). Именно они дают
high-priority-reassoc окно 3000мс в gate выше.

## Что это значит для Intel-порта

1. Тот же Apple WCLScanManager/WCLRoamManager работают НАД AirportItlwm. Значит
   публикация `0x89`/`0x50` из общего net80211 owner (WIP) — это и есть подача
   roam-state вверх; отдельный «getRoamState-ответчик» в драйвере, вероятно, не
   нужен, но это **обязан подтвердить runtime** (какое событие взводит `+0x3c`).
2. `setWCL_SCAN_REQ` не следует спекулятивно менять на reject: если верхний gate
   подавляет скан, setter в окне не достигается; если НЕ подавляет, reject даст
   молчаливо пустой GUI-скан (в consumer нет очереди/ретрая). Правильность зависит
   от runtime-поведения верхнего gate — сначала измерить, потом решать.
3. Остаток implementation-долга WIP: заполнить 168-байтный `0x50` carrier
   (flags/AKM/PHY/chanspec — по реальным наблюдениям, не выдумывать firmware-stats),
   затем собрать и воспроизвести scan-over-roam в disposable guest.

## Файлы эталона (10.7.6.112, x4SovM/contract)

- `…WCLScanManager11sendRequestEP14WCLScanRequest.c`
- `…WCLScanManager28isScanAllowedByOtherActivityEP14WCLScanRequestRj.c`
- `…WCLRoamManager12getRoamStateER20bulletinBoardMessage.c`
- сопутствующие: `handleScanRequest`, `scanRequestHandler`, `abortScan`,
  `handleReassocEvent`, `handleRoamDoneEvent`, `startRoamScan`, `sendReassocToDriver`.
Manifest SHA256 `ce2cac9db82eed8be5e9138ba8de9e7cca14a4c206a2aa0cb8f1654538760d6d`.
