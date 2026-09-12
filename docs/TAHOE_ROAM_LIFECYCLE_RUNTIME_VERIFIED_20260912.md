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

## ДОКАЗАНО: GUI off/on (power toggle) recovery контракт (highest-priority surface)

CoreWLAN `setPower:NO`→`setPower:YES` (эквивалент GUI-тумблера) на WIP:
восстановление SAE+DHCP `172.16.66.219` за 18с, ping OK. Все WCL FSM проходят
свой контракт ЧИСТО, без зависаний:
- OFF: NET_MANAGER LINK_UP→LEAVE_NETWORK→DEAUTH→LINK_DOWN; ROAM_MANAGER→LINK_DOWN;
  SSM DRIVER_UNAVAILABLE.
- ON: SSM DRIVER_AVAILABLE→CAN_SEND; SYSTEM_POWER_ON/RESUME (SCAN/JOIN/NDD);
  JOIN_MANAGER JOIN_REQ→IN_PROGRESS→ASSOC_DONE→CONNECT_COMPLETE→IDLE;
  NET_MANAGER LINK_DOWN→WAITING_FOR_CONNECT_COMPLETE→WAITING_FOR_IP;
  ROAM_MANAGER LINK_DOWN→CONNECT_COMPLETE→LINK_UP.

Транспорт лаборатории (read-only): mgmt SSH идёт по guest en2 (10.0.6.15,
default route 10.0.6.2), НЕЗАВИСИМ от en1 (тестируемый Wi-Fi) → GUI Wi-Fi
off/on безопасен для управления. Инструменты на guest: /tmp/aiamscan (scan),
/tmp/aiampow (power on/off/status) — CoreWLAN, компилируются `clang -fobjc-arc
-framework CoreWLAN -framework Foundation`.

Остаток GUI-дивергенций (по handoff §7): пакетные потери в ОКНЕ reconnect
(saved-SAE reselect, off/on) — это НЕ сбой контракта FSM (контракт чист), а
потери в переходном окне; локализация RF/AP/driver требует endpoint-capture.

## ДОКАЗАНО: saved-SAE reselect recovery контракт

CoreWLAN `disassociate` → авто-rejoin (saved SAE): восстановление с новым DHCP
(172.16.66.212) за ~12с, **потери 0.8% (1/120 ping)** — почти бесшовно. FSM чист:
NET_MANAGER LINK_UP→LEAVE_NETWORK→DEAUTH→LINK_DOWN; ROAM_MANAGER→LINK_DOWN;
JOIN_MANAGER JOIN_REQ→IN_PROGRESS→(TRY_NEXT_CANDIDATE)→ASSOC_DONE→CONNECT_COMPLETE
→IDLE; NET_MANAGER→WAITING_FOR_CONNECT_COMPLETE→WAITING_FOR_IP; ROAM_MANAGER→LINK_UP.
Handoff §7 reselect-потери здесь не воспроизводятся как сбой контракта. Тул
/tmp/aiamdis (CoreWLAN disassociate).

## ДОКАЗАНО: S3 sleep/wake + Wi-Fi recovery (handoff §7 open item — для Wi-Fi закрыт)

QEMU 10.2.50 корректно исполняет S3 гостя. Механика (проверено):
`sudo pmset sleepnow` → гость входит в S3 → QEMU `VM status: paused (suspended)`,
RIP заморожен. Пробуждение: QEMU HMP `system_wakeup` ПОСЛЕ полного suspend (+
`sendkey spc` как nudge; первый wakeup на 12с мог не сработать — гость ещё
входил в S3). После wake — ТОТ ЖЕ boot session UUID `7D5B03FE` = настоящий
resume, НЕ reboot.

Post-S3 на WIP: en1 active, DHCP `172.16.66.212`, ping OK (латентность
восстанавливается). pmset log: `0x100=MAGICWAKE ... en1 owner=
IOSkywalkNetworkBSDClient` → **WoWLAN magic-wake itlwm работает**. Wi-Fi
восстанавливается чисто. (Handoff §7 «GUI застревал в WindowServer/IOFB» —
это графика, НЕ Wi-Fi; Wi-Fi-контракт S3 удовлетворён.)

Монитор QEMU (root): `/home/dima/Projects/itlwm/aiam-iwn-profile-reset.bhi2aO/
qemu-scd-control-monitor.sock` (HMP; `system_wakeup`, `info status`, `sendkey`).

## setWCL_ARP_MODE: путь к идентичности теперь ясен (S3 verifiable), но = firmware-фича

itlwm имеет WoWLAN command-инфраструктуру (IWx/IWM_WOWLAN_CONFIGURATION 0xe1,
patterns, KEK/KCK) и magic-wake, НО НЕ имеет proto/ARP-offload (`PROT_OFFLOAD`
отсутствует). Эталон = firmware ARP-offload (`configureARPOffload` → `arpoe`
iovar): firmware отвечает на ARP во сне, хост не просыпается. itlwm без offload
просыпается на ARP (magic-wake) — хуже по питанию во сне. Идентичность =
реализовать Intel proto-offload cmd (iwl_proto_offload: IPv4 для ARP-resp, IPv6
для NS-resp) в D3-конфиге + wire setWCL_ARP_MODE. НЕ костыль (blind-success рвёт
сон). Верификация = внешний same-L2 host ARP'ит гостя во сне (offload vs wake).
Крупная firmware-фича низкого пользовательского приоритета (фоновое питание сна).
