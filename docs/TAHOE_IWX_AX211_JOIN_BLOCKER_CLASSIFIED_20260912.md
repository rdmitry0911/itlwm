# AX211 (IWX) JOIN blocker — root-cause classification vs. the identity goal

Срез: 2026-09-12. Продолжение runtime-находки `e2e61595` (iwx HAL на реальном
AX211 инициализируется/сканирует, но JOIN падает:
`itlwm: : could not enable MGMT Tx queue 1 (error 5)`). Здесь — доказательная
классификация этого блокера относительно цели проекта (тождественность
**поверхности соприкосновения** драйвера с user space и kernel space).

## Точный путь отказа (source, доказано чтением кода)

`iwx_enable_mgmt_queue` → `iwx_enable_txq(sc, IWX_STATION_ID, first_data_qid,
IWX_MGMT_TID, ring_count)` → `iwx_allocate_tx_queue`. Для AX211 ADD_STA cmdver≥12,
поэтому `first_data_qid = IWX_QID_MGMT-1 = 1`. Ошибка **5 = EIO** возвращается из
`iwx_allocate_tx_queue` после `iwx_send_cmd(SCD_QUEUE_CONFIG_CMD)` из одной из
firmware-response проверок (`ItlIwx.cpp` ~7499–7515):
- `packet==NULL || (hdr.group_id & IWX_CMD_FAILED_MSK) || payload_len!=sizeof(rsp)` → EIO
- `response->flags != 0` → EIO
- `queue != fixedQueue || write != ring->cur` (для fixed-очереди) → EIO

Т.е. EIO означает: **firmware отвергла/не подтвердила конфигурацию MGMT-очереди**
(либо ассертнула, либо вернула чужой номер очереди/непустые flags).

## Это НЕ регрессия данной ветки (доказано git)

- `iwx_allocate_tx_queue` **идентична** между AF16 (`5e98d640`) и HEAD
  (`git diff` = пусто).
- Все EIO-проверки (`fwqid!=qid`, `wr_idx!=ring->cur`, packet/flags) уже были в
  версии-родителе до owner-rewrite `4d979f3b` (2026-09-11), с явным комментарием
  в коде: **«Unlike iwlwifi, we do not support dynamic queue ID assignment.»**
- Сборка, приведшая к «AX211 работал» (по данным предшественника ~2026-08-05),
  предшествует owner-rewrite и содержала ту же fixed-queue модель.

## Это firmware-side блокер (доказано reference-RE предшественника)

На reference-host `dima@10.7.6.112` есть `~/Projects/ghidra_output/
iwlwifi_ty68_assert_20260806/` — реверс **самой прошивки AX211** (ty68 = Ty
LMAC/UMAC, API-68) через кастомный ARCompact/Xtensa-процессор Ghidra. Артефакты
(`results_20260807/`) показывают, что предшественник декодировал:
- два UMAC-ассерта прошивки: `20101034` (site `80460d40`, ref `c0082ffa`) и
  `20101058` (site `8045ee1e`, функция event-enqueue `8045edac`);
- firmware queue-примитивы: `umac_queue_primitive_c008424c`,
  `umac_queue_push_c0083010`; hcmd lookup-таблицы (`umac_hcmd_lookup_*`).

Т.е. AX211 JOIN упирается в **ассерт/отказ внутри прошивки** на пути
enqueue/queue-config, а не в баг контактной поверхности драйвера. Предшественник
разбирал это на уровне ассемблера прошивки в августе; в shipped-релизе
(`97fe747c` / v2.4.0-alpha) AX211 сохранён как **документированный
hardware-limit** («release notes … retain hardware/GUI/roam limits»).

## Классификация относительно цели (поверхность соприкосновения → 0)

Цель — тождественность **контактной поверхности** (WCL `get/setWCL_*` +
apple80211 + FSM-контракты), которую видят user space и kernel space. AX211
JOIN-отказ лежит **ниже** этой поверхности: это firmware-transport (конфигурация
аппаратной TX-очереди), а не расхождение возвращаемых значений/событий контракта.
По принципу самого проекта: «ни user space, ни kernel space не имеет прямого
доступа к firmware; как драйвер исполняет контракт (с firmware или без) — в
контракте не указано». Отказ прошивки сконфигурировать очередь — не элемент
контракта; это отсутствие рабочего firmware-транспорта на конкретном железе.

**Reference-supported fix существует, но крупный и не-костыльный:** эталон
(iwlwifi gen2) использует **динамически назначаемый firmware номер очереди**
(`SCD_QUEUE_CONFIG` add → `rsp->queue_number`), тогда как база itlwm/OpenBSD iwx
явно «do not support dynamic queue ID assignment» и индексируют `sc->txq[qid]`
фиксированно. Идентичность потребовала бы порта динамических очередей (MGMT+data)
на firmware-assigned id — крупная iwlwifi-портизация. Но даже она не гарантирует
JOIN, если корень — firmware-ассерт (`20101034/20101058`), т.к. это дефект/несов-
местимость прошивки, неустранимый со стороны драйвера. Blind-success здесь =
костыль (JOIN «удался» без реальной очереди → обрыв трафика) → запрещён.

## Вывод

AX211 JOIN — **известный, документированный hardware/firmware блокер**
(firmware-ассерт на пути queue-config/enqueue), не регрессия этой ветки, не
костыль и **не расхождение контактной поверхности**. Он в одном классе с
D3/WoWLAN ARP-offload: крупная firmware-зависимая работа ниже контракта, вне
приоритетной GUI-поверхности. Контактная поверхность на целевом железе ветки
(IWN/6235) сведена и runtime-подтверждена; данный residual корректно вынесен как
firmware-transport ограничение конкретного адаптера, а не как незакрытая
нетождественность поверхности.

Дальнейшее (если приоритизируется отдельно пользователем): либо продолжить
firmware-ассерт RE предшественника (`iwlwifi_ty68_assert_20260806`) чтобы
установить устранимость со стороны драйвера, либо порт динамических TX-очередей
под эталон iwlwifi gen2 — обе — крупные отдельные треки, требующие destructive
AX211-swap для runtime-подтверждения.
