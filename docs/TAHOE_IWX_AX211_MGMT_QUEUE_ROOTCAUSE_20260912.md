# AX211 (IWX) JOIN root cause — MGMT queue config rejected (RUNTIME-diagnosed)

Срез: 2026-09-12. Продолжение классификации (docs TAHOE_IWX_AX211_JOIN_BLOCKER_
CLASSIFIED). Блокер `itlwm: : could not enable MGMT Tx queue 1 (error 5)`
диагностирован РАНТАЙМ на реальном AX211 (destructive swap + dtrace на живом
драйвере, затем чистый restore IWN). Это НЕ hardware-limit — это driver-fixable.

## Метод

AX211 (0000:00:14.3) вкачан в гостя (bind_ax211_vfio.sh + adapted launcher),
6235 оставлен на vfio (idle). Гость поднялся на ЧИСТОМ WIP (951D4653), en1 без IP
(JOIN падает). dtrace на iwx_allocate_tx_queue/iwx_lookup_cmd_ver/iwx_free_resp
(структуры описаны в скрипте, читают firmware-ответ). После — чистый restore:
AX211 → host iwlwifi, host wifi reconnect, relaunch IWN; гость снова connected
(en1 172.16.66.212, ping gw 0% loss). Лаба в verified-состоянии.

## Захваченные данные (одна попытка JOIN)

```
ADDSTA_cmdver=12
ALLOC ret=-5 scd_ver=99 sta=0 tid=15 size=1024 fq=1 | pktnull=0 gid=0x1 cmdfail=0 qnum=1 flags=0x1 wptr=0
ENABLE_MGMT ret=5
```

## Декод (точный корень)

- `ret=-5` = -EIO. Падающая ветка = **EIO-B**: `if (le16toh(response->flags) != 0)`
  в `iwx_allocate_tx_queue` (ItlIwx.cpp ~7505).
- `scd_ver=99` = `IWX_FW_CMD_VER_UNKNOWN`: firmware cmd-version TLV AX211 **НЕ
  содержит** SCD_QUEUE_CONFIG_CMD (DATA_PATH_GROUP 0x5 / cmd 0x17). Контраст:
  ADD_STA (LONG_GROUP/0x18) = **12** (есть). → в allocate `version != 3` →
  **LEGACY** ветка (`hcmd.id = IWX_SCD_QUEUE_CFG`, cmd_v0 с sta_id/tid/flags=
  ENABLE_QUEUE/cb_size/byte_cnt_addr/tfdq_addr).
- Firmware **ОТВЕТИЛА** (pktnull=0), НЕ CMD_FAILED (gid&0x40=0), **эхо qnum=1**
  (== fq=1, station-id тоже echo) — значит это **НЕ** dynamic-queue mismatch
  (EIO-C), fixed queue number принят. НО **flags=0x1** ("set on failure" по
  комментарию iwx_tx_queue_cfg_rsp) → gen2 AX211 firmware **ОТВЕРГАЕТ legacy
  queue-config команду**.
- AX211 (0x51F0/0x51F1) → `iwl_so_long_latency_trans_cfg` →
  `sc_device_family = IWX_DEVICE_FAMILY_AX210` (gen2/AX210).

## Природа бага

`iwx_allocate_tx_queue` выбирает modern vs legacy ТОЛЬКО по `version == 3`,
игнорируя поколение устройства. Для AX210-семейства с version=UNKNOWN это даёт
LEGACY SCD_QUEUE_CFG, которую прошивка AX210 отвергает (flags=0x1). Аналогично
REMOVE-путь (ItlIwx.cpp ~7008) трактует version=UNKNOWN как «legacy TVQM без
REMOVE». Т.е. вся модель «UNKNOWN → legacy» неверна для AX210.

## Направление фикса — ЭТАЛОННО ПОДТВЕРЖДЕНО (upstream iwlwifi)

Fetched torvalds/linux: `mvm/ops.c` ~1426 —
`trans->conf.queue_alloc_cmd_ver = iwl_fw_lookup_cmd_ver(fw,
WIDE_ID(DATA_PATH_GROUP, SCD_QUEUE_CONFIG_CMD), 0)` — **DEFAULT 0** когда прошивка
не публикует версию. → iwlwifi для прошивки AX211 (без TLV) ТОЖЕ берёт ver 0 →
**OLD** SCD_QUEUE_CFG, как и itlwm. Значит выбор команды (legacy) — НЕ дивергенция.

Настоящая дивергенция = FIXED vs DYNAMIC аллокация (`pcie/gen1_2/tx-gen2.c`
~1021 `iwl_txq_dyn_alloc`): iwlwifi ВСЕГДА сперва зовёт `iwl_txq_dyn_alloc_dma`
= выделяет СВЕЖУЮ dynamic-очередь (правильный gen2 DMA/bc-table), шлёт команду
(old ИЛИ new) БЕЗ номера очереди, и **firmware НАЗНАЧАЕТ** очередь
(`iwl_pcie_txq_alloc_response`). itlwm `iwx_enable_mgmt_queue` привязывает MGMT
к ПРЕДСУЩЕСТВУЮЩЕМУ FIXED-кольцу `sc->txq[first_data_qid]` и требует, чтобы
прошивка приняла этот fixed-конфиг → gen2/AX210 firmware отвергает (flags=0x1).
Это ровно gap «Unlike iwlwifi, we do not support dynamic queue ID assignment».
Замечание: dtrace echo qnum=1 — прошивка НАЗНАЧИЛА очередь 1 (у old-команды нет
поля queue), совпало с fq=1 случайно; провал именно по flags (конфиг fixed-кольца
не принят), не по номеру.

**ФИКС:** маршрутизировать gen2/AX210 MGMT-очередь через СУЩЕСТВУЮЩИЙ dynamic-путь
itlwm (`iwx_tvqm_alloc_txq` / `iwx_allocate_tx_queue` fixedQueue=-1 — уже делает
свежий DMA + firmware-assigned queue), сохранить назначенный qid и использовать
его в MGMT-TX (вместо fixed first_data_qid). Ограниченный, но реальный порт по
MGMT-TX пути. Верификация = ещё один AX211 swap + JOIN.

## Статус

Корень найден, driver-fixable, НЕ hardware-limit. Лаба восстановлена (IWN,
connected). Следующий цикл: fetch upstream iwlwifi reference (gen2 MGMT queue) →
подтвердить гейт → реализовать → build → swap-verify.
