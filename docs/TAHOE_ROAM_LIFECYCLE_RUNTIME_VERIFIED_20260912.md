# Roam-lifecycle WIP — runtime verification against the WCL FSM contract

Срез: 2026-09-12. Кекст `b7b0fd47` (WIP `83a2f617` + консолидация) собран,
установлен и **загружен** в disposable guest; проверен runtime против эталонного
контракта поверхности соприкосновения (методология: lab = полигон).

## Идентичность загруженного образа

- UUID `951D4653-645E-3DCB-941A-CB5F617177E6`, Mach-O sha256
  `081db7d4068a3e765902ebd36aa9025668b7614f1b0ef6ae671c3736ad99c380`,
  `ITLWM_COMMIT_HASH=b7b0fd4782ef`, 1088 imports разрешаются против guest BootKC.
- Установлен транзакционным bridge (preflight PASS, exact 5-member AuxKC,
  ACTIVATION_READY), чистый reboot. После reboot `kmutil showloaded` = 951D4653.
- Откат к AF16: displaced-копии в
  `/private/var/tmp/aiam-iwn-activation-wip.Wbd9Rc/activation-20260912T130623Z/`.

## Наблюдательный канал

Kernel log: `[wcl] logTransition@207:FSM <MANAGER>: in state ... got event ...
moved into ...` (subsystem corecapture/InterfaceLogs) — это ЭТАЛОННЫЕ WCL FSM
(SCAN_MANAGER, ROAM_MANAGER, NET_MANAGER, JOIN_MANAGER, NDD_MANAGER), работающие
НАД itlwm. Плюс драйверные `(AirportItlwm) itlwm: wcl_reassoc ...` строки.

## ДОКАЗАНО: roam-FSM контракт удовлетворён (D2 закрыт)

Boot-roam (16:08:07→16:08:15) — полный цикл ROAM_MANAGER, управляемый событиями WIP:

| WCL ROAM_MANAGER переход | драйверное событие itlwm (WIP) |
|---|---|
| LINK_UP →(ROAM_START)→ ROAM_SCAN | `wcl_reassoc REAL_SCAN_STARTED` (0x89) |
| ROAM_SCAN →(ROAM_PREP)→ ROAM_REASSOC | `wcl_reassoc TARGET_SELECTED bssid=…ca ch9` (0x8b) |
| ROAM_REASSOC →(REASSOC)→ WAIT_ROAM_DONE | reassoc reply (0x49) |
| WAIT_ROAM_DONE →(ROAM_DONE)→ LINK_UP | `wcl_reassoc TARGET_RUNNING` (0x50/roamDone) |

Т.е. эталонный ROAM_MANAGER доходит до **ROAM_MANAGER_EVENT_ROAM_DONE → LINK_UP**
— настоящего терминала, который старый itlwm (только 0x49/0xcf) НЕ давал (FSM
зависал, protection timer не снимался). WIP закрывает эту дивергенцию: события
0x89/0x8b/0x50 потребляются эталонным FSM и проводят его по полному контракту.

## ДОКАЗАНО: scan контакт-surface корректен

`FSM SCAN_MANAGER: IDLE →(SCAN_REQ)→ IN_PROGRESS →(SCAN_COMPLETE)→ IDLE`
на каждый CoreWLAN scan (собственный тул `scanForNetworksWithName:nil`,
9 и 26 сетей, соединение выживает, en1 up). Эталонный SCAN_MANAGER драйвит itlwm
как AppleBCMWLAN.

## Регрессий нет

После reboot: WPA3-SAE ch9, DHCP 172.16.66.219, ping gw 0% потерь. Скан не рвёт
соединение.

## Что ещё НЕ воспроизведено прямо (следующий runtime-шаг)

Конкретный **scan-во-время-roam** (public scan приходит в состоянии ROAM_SCAN):
boot-roam прошёл без наложения скана. Терминал superseded-roam использует ТОТ ЖЕ
код emit 0x50 (postWclReassocCompletionGated FAIL-ветка), уже доказанно достигающий
ROAM_MANAGER → высокая уверенность, но прямой repro (детерминированный триггер
roam + одновременный scan) ещё нужен. Триггер roam на заказ — открытый tooling-вопрос.
