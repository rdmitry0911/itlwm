# IWX (AX211) runtime on WIP — HAL init/scan OK, MGMT-TX-queue enable FAILS

Срез: 2026-09-12. Пользователь разблокировал IWX: host AX211 (0000:00:14.3,
8086:51F1, Raptor Lake CNVi, IOMMU group 7 isolated) прокинут в гость, 6235
(0000:25:00.0) возвращён на хост. Управление гостем — независимый проводной en2.

## Процедура swap (проверено, есть готовые скрипты)
Build/ax211-passthrough-wip77/{bind_ax211_vfio.sh, restore_ax211_host.sh}.
1. Чистый shutdown гостя (management SSH) → дождаться выхода QEMU (overlay flush).
2. 6235→host iwlwifi (unbind vfio, driver_override=iwlwifi, bind).
3. AX211 STA disconnect+managed no+link down → bind_ax211_vfio.sh (rollback-guarded).
4. Relaunch QEMU: заменить ТОЛЬКО `-device vfio-pci,host=0000:25:00.0,id=iwn`
   на `...,host=0000:00:14.3,id=ax211`; тот же overlay tahoe-auth-beacon.qcow2 +
   OVMF_VARS.fd, без snapshot=on; новые monitor/serial/pid в scratch; hostfwd 3338,
   VNC 5905 сохранить. Reverse: обратный порядок + verbatim IWN launcher.
Генератор launcher'ов из живого argv: scratch/iwx-swap/ (launch-ax211.sh,
launch-iwn-verbatim.sh; OSK сохраняется приватно, не печатается).

## Результат на WIP (kext 951D4653 поддерживает 51F1: IOPCIPrimaryMatch + IWL_PCI_DEVICE(0x51F1))
ДОКАЗАНО работает: iwx HAL инстанцируется (`ItlIwx=1`, `ItlIwm=0`), PNVM firmware
загружается (`PNVM init complete 0x20001`), `IWX APSTA lower-ready ... supported=1`,
scan/RX работает (видит сеть RSSI −60 dBm).
НЕ работает: JOIN падает — `itlwm: : could not enable MGMT Tx queue 1 (error 5)`
на каждой попытке → JOIN_MANAGER JOIN_REQ_FAILED/JOIN_ABORT, en1 inactive.

## Атрибуция (ВАЖНО, исправлено)
`VFIO dma-buf not supported in kernel` — БЕНИГНОЕ предупреждение: оно появляется
и для 6235, который РАБОТАЕТ. Значит НЕ dma-buf. `MGMT Tx queue error 5` —
РЕАЛЬНАЯ iwx-специфичная проблема (init/scan ок, mgmt-TX-queue enable падает).
Это НЕ common-код roam-lifecycle (тот verified на IWN). Кандидаты: iwx_enable_txq/
SCD MGMT-queue setup для AX211 API-68; регресс между AF16 (работал на AX211
2026-08-05) и WIP, ЛИБО pre-existing на этом AX211/условиях. Требует iwx-TX-queue
диагностики (следующий слой): swap назад к AX211 + трейс iwx_enable_txq/SCD.

## IWN восстановлен
После reverse-swap: WIP на 6235, en1 172.16.66.212 connected, ping ok. Канонический
verified IWN стейт восстановлен.
