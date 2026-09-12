# WCL selector contact-surface audit — crutch check vs reference

Срез: 2026-09-12. Проверка всех 40 `get/setWCL_*` handler'ов itlwm
(`AirportItlwmSkywalkInterface.cpp`) против эталона
`AppleBCMWLANCoreMac_decompiled.c` (полная декомпиляция, 10.7.6.112,
`AppleBCMWLANInfraProtocol::*`). Цель: найти костыли (blind-success), т.к.
«Костыли запрещены».

## Итог: костылей в WCL-поверхности НЕТ

Скрипт-эвристика пометила 2 «blind-success» кандидата; оба разрешены:

- **`setWCL_LIMITED_AGGREGATION`** — `return data==NULL ? BadArg : Success`.
  Эталон `AppleBCMWLANInfraProtocol::setWCL_LIMITED_AGGREGATION` (0x1542eca) —
  ТО ЖЕ САМОЕ: `uVar1=0xe00002bc; if(param_2!=0) uVar1=0; return uVar1`. Эталон
  сам — no-op acknowledger. **ИДЕНТИЧНО**, не костыль.
- **`setWCL_SET_SCAN_HOME_AWAY_TIME`** — на деле применяет
  (`airportItlwmSetScanHomeAwayTime`, валидация ≤1000мс). Эвристика ошиблась.

Остальные не-реализованные селекторы возвращают **`kIOReturnUnsupported`**
(НЕ blind-success), с комментарием «Intel has no equivalent backend»:
`BCN_MUTE_CONFIG` (эталон применяет `configureBeaconMitigationParams`),
`WNM_OFFLOAD`, `ARP_MODE`, `REAL_TIME_MODE`, `ROAM_USER_CACHE`, `SOI_CONFIG`,
`ASSOCIATED_SLEEP`, `ULOFDMA_STATE`. Комментарий SOI явно: «do not acknowledge
an unconsumed request» — т.е. сознательный отказ от костыля.

## Природа остаточной не-идентичности

Для этих селекторов эталон возвращает success (применив через Broadcom-специфичный
adapter/iovar), а itlwm — `Unsupported`. Это НЕ костыль, а честное «нет backend».
Достичь идентичности = реально реализовать эффект (beacon mitigation, WNM/ARP
offload, real-time/sleep режимы) средствами Intel, если это возможно без
firmware-транспорта, которого у Intel нет. Это низкоприоритетно (фоновые
оптимизации, не «наиболее востребованное пользователями»: connect/scan/roam/GUI
полностью реализованы и runtime-подтверждены).

## Реализованные и runtime-подтверждённые (не Unsupported)

`setWCL_ASSOCIATE/REASSOC/SCAN_REQ/SCAN_ABORT/LEAVE_NETWORK/JOIN_ABORT/
LINK_STATE_UPDATE/LINK_UP_DONE/ROAM_PROFILE_CONFIG/SET_ROAM_LOCK/QOS_PARAMS/
UPDATE_FAST_LANE/ACTION_FRAME/CONFIG_BG*/SET_SCAN_HOME_AWAY_TIME/TRIGGER_CC`,
`getWCL_BSS_INFO/EXTENDED_BSS_INFO/CHANNELS_INFO/BGSCAN_CACHE_RESULT/
TRAFFIC_COUNTERS/LOW_LATENCY_INFO/GET_TX_BLANKING_STATUS`.
