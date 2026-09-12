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

## Направление фикса (требует эталонного grounding перед кодом)

Эталон Intel-транспорта = iwlwifi (на 10.7.6.112 отдельного дерева нет → нужен
upstream). Открытый вопрос ИМЕННО по эталону: как iwlwifi для gen2/AX210
аллоцирует MGMT-очередь, когда SCD_QUEUE_CONFIG_CMD ver = UNKNOWN —
(a) modern SCD_QUEUE_CONFIG_CMD (IWX_SCD_QUEUE_ADD, wide-ID) по признаку
device_family (не по version), или (b) TVQM dynamic (iwlwifi gen2 использует
динамические очереди), или (c) fixed с иными полями. dtrace показал echo qnum=1
(fixed принят прошивкой), что склоняет к (a) modern-format-по-device_family, а не
к смене на dynamic. Гипотеза фикса: гейт `version == 3 || sc_device_family >=
IWX_DEVICE_FAMILY_AX210` → modern. НО писать без подтверждения эталоном запрещено
(риск firmware-assert). Верификация фикса = ещё один AX211 swap + JOIN.

## Статус

Корень найден, driver-fixable, НЕ hardware-limit. Лаба восстановлена (IWN,
connected). Следующий цикл: fetch upstream iwlwifi reference (gen2 MGMT queue) →
подтвердить гейт → реализовать → build → swap-verify.
