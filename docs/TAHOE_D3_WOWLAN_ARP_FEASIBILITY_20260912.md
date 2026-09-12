# D3/WoWLAN ARP-offload — feasibility of functional equivalence on available HW

Срез: 2026-09-12. Продолжение: setWCL_ARP_MODE функциональная эквивалентность
требует РЕАЛЬНОГО ARP-offload (NIC отвечает на ARP во сне) = полный D3/WoWLAN
proto-offload flow (доказано: contract из AppleBCMWLAN vtable 0x1268 → firmware
ARP-offload по host IPv4; [[functional-equivalence-required]]). Здесь — анализ
осуществимости на реальном железе лаборатории.

## Приоритет: НИЗ (system-only, нечастый, нет GUI-ветви)

По framework пользователя (частота + GUI>terminal>system) ARP/D3 — низший слой.
Часто-используемая GUI/terminal поверхность уже приведена к функциональной
тождественности и runtime-подтверждена (docs TAHOE_GUI_STATUS_FUNCTIONAL_EQUIV).

## Осуществимость по железу

- **6235 (hal_iwn, текущий гость, fw iwn-6030):** в `hal_iwn` есть ТОЛЬКО TLV-флаг
  ОПРЕДЕЛЕНИЯ D3/offload (`IWN_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS`,
  `..._D3_CONTINUITY_API`, `..._NEW_NSOFFL_*`) — но **НЕТ реализации** D3_CONFIG/
  PROT_OFFLOAD/WOWLAN command flow (grep `d3_config|prot_offload|wowlan|proto_offload`
  в hal_iwn/*.cpp = пусто). Т.е. драйвер для iwn D3/WoWLAN не строит вовсе. Плюс
  нужно подтвердить, что конкретная 6030-прошивка реально анонсирует D3-capability
  и содержит отдельный D3-ucode image (WOWLAN_INST/DATA TLV) — не подтверждено.
- **AX211 (hal_iwx):** cmd IDs есть (D3_CONFIG 0xd3, PROT_OFFLOAD 0xd4, group 0xb,
  WOWLAN_CONFIGURATION 0xe1), но **структур iwl_proto_offload_cmd нет**, D3 flow нет,
  и AX211 вообще не ассоциируется (firmware MGMT-queue блокер, docs
  TAHOE_IWX_AX211_JOIN_BLOCKER_CLASSIFIED) → ARP-offload недостижим пока JOIN закрыт.

## Почему это не костыль-закрываемо

ARP-offload во сне ФИЗИЧЕСКИ требует firmware (CPU спит; только NIC-firmware может
ответить на ARP). Без firmware-поддержки это невозможно в принципе — не выбор
драйвера. Blind-success (ack без offload) = костыль (обрыв достижимости во сне) →
запрещён. itlwm сейчас: sleep = full-stop (disableAdapter), на wake — reconnect;
пользовательский исход (после пробуждения подключено) ДОСТИГАЕТСЯ (S3 verified),
отличие от эталона = достижимость во сне + скорость wake.

## Что нужно, чтобы двигать дальше (осознанно, не вслепую)

1. Reference: точная версия `struct iwl_proto_offload_cmd` под целевую firmware
   API (upstream iwlwifi fw/api/offload.h — на 10.7.6.112 отдельного iwlwifi-дерева
   нет; писать структуры «на память» = риск firmware-assert как с mgmt-queue →
   запрещено правилом «докажи по эталону»).
2. Runtime feasibility: подтвердить, что 6235-прошивка анонсирует D3-capability +
   имеет D3-ucode image; иначе на 6235 это невозможно (как на AX211 из-за JOIN).
3. Реализация: proto_offload structs + builder + D3 entry/exit + правка sleep-path
   — КРУПНЫЙ порт, ВЫСОКИЙ риск регресса verified S3, многосессионный, требует
   runtime-проверки (внешний same-L2 host ARP’ит спящего гостя).

## Вывод

Наиболее востребованная пользователями поверхность (частые GUI/terminal функции)
приведена к функциональной тождественности. Остаток нетождественности —
исключительно НИЗШИЙ слой: крупные firmware-фичи (D3/WoWLAN ARP-offload) и
конкретно-аппаратный блокер (AX211 JOIN), оба hardware/firmware-ограничены, крупные
и многосессионные. Двигать D3 дальше осмысленно только с (1) upstream iwlwifi
reference и (2) подтверждённой D3-capability прошивки — иначе это слепой код /
костыль / риск регресса, что запрещено.
