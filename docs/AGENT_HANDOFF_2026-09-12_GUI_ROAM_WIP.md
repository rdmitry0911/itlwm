# Передача дел — GUI / повторные подключения / roam WIP

Срез: **12 сентября 2026, 11:31 UTC**. Пользователь попросил очередной
коммит/пуш, затем передачу другому агенту. Внедрение остановлено на явном
WIP-чекпойнте; никаких новых установок или runtime-утверждений не делать
на основании одних зелёных source-тестов.

Этот документ имеет приоритет над историческими «next» в старом
`AGENT_HANDOFF_2026-07-21_PMF_RUNTIME.md`, плане и scratch-записях.
Полная функциональная цель НЕ завершена; её объём не сужался.

## 1. Исходники, пуш, рабочий релиз

- Канонический checkout: `/home/dima/Projects/itlwm-iwn-sae-bridge-runtime`.
- Ветка: `tahoe-iwn-sae-bridge-runtime`.
- Единственный remote `origin`: `https://github.com/rdmitry0911/itlwm.git`.
- **WIP-код: `83a2f617dbef385587fb3ac69338fda2ac488338`**, сообщение
  `wip: checkpoint accepted-roam lifecycle before GUI runtime qualification`.
  Пуш завершён; `git ls-remote --heads` независимо подтвердил этот SHA.
  Следующий коммит содержит только настоящую передачу и обновление указателей.
- До WIP: `7ed00715291ee37511f1da10df68d0e0cd6977e9`, результаты same-L2 GUI.
- Единственная оставленная посторонняя untracked-папка: `Build/`.
  Не добавлять в коммит и не удалять. Все шесть изменённых production/test
  файлов WIP принадлежат передающему агенту и уже закоммичены.

**Рабочий установленный и последний проверенный опубликованный кекст —
НЕ WIP**, а `5e98d6406245d53b49a0592b5675f6ba739fb61d`:

- UUID `AF169180-05E8-33CF-960A-166E5DB901BB`;
- Mach-O SHA256 `ab06be973d3ae72aa6544b28e834285d2a6e1e02e8b8633c8fb78f14d2b1bf2b`;
- source manifest `5dbe5aae76fa07ade6a22dd9c4c23e7df39b53850cc745e11f9d75d1c0280b3e`;
- 1088 imports разрешались при проверке этой сборки;
- release: `https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha`;
- asset `AirportItlwm-Tahoe-v2.4.0-alpha.kext.zip`, 15695095 bytes,
  SHA256 `b0473b4633b46c13f0e271ce1757a5f6431c5302bd51c8957aecf9837dd5048d`;
- release ID357137705, asset ID558697358; последняя публикация 05:41 UTC.

WIP не собирался в Tahoe, не устанавливался и не публиковался в релиз.
Не выдавать его за готовый новый драйвер. После настоящего функционального
исправления пользователь требует commit/push, новый кекст и обновление релиза.

## 2. Что сейчас имеет высший приоритет

**P0.1 — реальные повторные GUI-сочетания open/WPA2/WPA3, сохранённые сети,
восстановление.** Все шесть направленных cross-security переходов, повторный
выбор того же профиля и переходы между разными сохранёнными профилями.
Отдельно: awake, явный off/on, настоящий S3 sleep/wake. Не подменять GUI
networksetup/API-командами; не считать off/on доказательством восстановления
после сна. Сохранять первую ошибку, промежуточный fallback/join/toggle,
реальный BSSID/AKM/PMF, DHCP и двусторонние ICMP/TCP-контроли.

Текущий roam-слой выбран как уже воспроизведённая зависимость GUI, а не
самостоятельное статическое улучшение. Не считать его доказанной причиной
каждой пакетной потери. Независимые capabilities/телеметрия/AP/transport не
должны вытеснять пользовательскую матрицу. IWM/IWX сохраняются в области
работы вместе с IWN; общий код не означает проверку всех моделей на радио.

Эталон/decompile проверять перед изобретением поведения. Новые тяжёлые
декомпиляции — 40 workers, исправленный Ghidra `5995e24caa` на10.7.6.112.
Старый AIAM/Stage1 workflow не является внешним разрешающим этапом:
пользователь поручил агенту весь упрощённый цикл.

## 3. Что именно лежит в WIP

Подробный контракт, тесты и ограничения:
[TAHOE_GUI_ROAM_LIFECYCLE_WIP_20260912.md](TAHOE_GUI_ROAM_LIFECYCLE_WIP_20260912.md).

- Общий owner теперь копирует source/target observations, публикует progress
  event26 и защищает cumulative stages номером запроса/association epoch.
- Начало0x89/12 bytes, подготовка0x8b/12, прежний ответ0x49 или0xcf,
  итог0x50/168. Подготовка выделена в `ieee80211_wcl_reassoc_prepare()` и
  вызывается из настоящего выбора цели в `ieee80211_end_scan_owned()`.
- Источник/цель копируются без удержания node pointer в событии.
- Terminal может обогнать отложенный progress и опубликовать ещё не выданный
  реальный prefix. Повторная публикация и старый terminal после нового
  serial/epoch подавляются. Опциональный0x8a не выдумывался.
- В итоговом carrier ещё НЕ заполнены все metadata-поля. Не считать
  нулевые flags/auth/AKM/PHY/firmware-tail доказательством их правильности.
- **`setWCL_SCAN_REQ` по-прежнему отменяет принятый roam. Очередь не исправлена.**

Предкоммитные проверки с прямым сохранением exit status:

| Проверка | Результат |
|---|---|
| full lifecycle owner/controller | 25 прежних + 6 новых orderings PASS; 4/4 ранее красных lifecycle требований PASS |
| join BSS TX/cache teardown | PASS |
| roam-carrier | compile FAIL: fixture не знает новый observation/stage/helpers |
| roam-scan static contract | FAIL: ожидает assignment ROAM_STARTED непосредственно в scan completion, а он перенесён в helper |
| deferred-BSS полный gate | известный liveness FAIL, сценарий1 |
| deferred-BSS negative verifier | подтверждены оба известных FAIL134, сценарии1 и24; это не зелёный gate |

Не ослаблять проверки ради зелёного отчёта. У deferred-BSS проблема не в
новой обвязке: сохранены pre-copy/source-drain и terminal-before-arm liveness
расхождения. В scratch есть их точные выводы. Полный regression aggregate,
сборка, загрузка и новые RF-контроли не выполнены.

## 4. Первые действия принимающего агента

1. Проверить current branch/HEAD/status, прочитать WIP-документ и diff
   `7ed00715..83a2f617`. Не перезапускать уже завершённый Ghidra или тесты
   только потому, что в старой записи был session ID.
2. Исправить две несовместимости fixtures, сохраняя реальные production
   function bodies и исходные требования. Добавить вложенную failure во
   время cumulative START+PREP replay, проверить жёсткую отмену/новую эпоху,
   очистку observations, rejected admission, устаревшие comments и старый
   `ieee80211_wcl_reassoc_claim_completion`, оставшийся рядом с новым helper.
3. Проверить lock/taskq/gate порядок нового progress callback. Он теперь
   синхронно входит в controller gate; освобождение selected-BSS lock само
   по себе не доказывает отсутствие deadlock с firmware workers.
4. Завершить соответствие итогового carrier реально доступным наблюдениям.
   Новые metadata exports уже прочитаны, см. раздел5. Не тратить цикл на
   выдумывание недоступной firmware-телеметрии и не объявлять её закрытой.
5. Исправить арбитраж public scan/live roam как цельный слой для всех HAL.
   Эталон различает high-priority окно3000ms, low-RSSI roaming с исключением
   low-latency scans, join/GAS/NDD/IP-resolution/link-loss; не запрещает все
   сканы во время любого роуминга. Нижняя scan error возвращается наружу;
   автоматическая очередь для любого busy в прочитанном consumer не найдена.
6. Существующий `beginWclBackgroundScanAfterRoam`/IWN pending queue относится
   к **уже отменённому** serial/source epoch. Нельзя просто использовать его
   как очередь за живым roam. Верхний WCL ticket обязан пережить accepted
   wait/abort/reset честно, без подмены свежего скана старым census.
7. После source-проверок — точная Tahoe-сборка и imports/input audit,
   установка только в disposable guest, проверка UUID и реальные GUI
   scan/roam/repeated-profile controls. Не обновлять релиз до квалификации.

Рабочие места, просмотренные непосредственно перед запросом передачи:
`AirportItlwmV2.cpp`: `postWclReassocCompletionGated`, scan lifecycle
reserve/queue/abort/terminal, `wclPhysicalScanTerminalInterruptAction`;
`AirportItlwmSkywalkInterface.cpp`: `setWCL_SCAN_REQ`, `setWCL_SCAN_ABORT`;
`TahoeWclPhysicalScanContracts.hpp`: Starting/Queued/Active/Aborting ownership.
Никакой реализации новой scan queue в этих файлах ещё нет.

## 5. Эталон и сохранённые материалы

Авторитетное воспроизведение: [TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md](TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md).
Обычный public scan-only запрос отменял accepted roam до target AUTH,
сохраняя source link. Старый driver выдавал0x49/0xcf, но не полный lifecycle.
FSM показывает: только0x50/roamDone снимает timer/pending bookkeeping.

На `dima@10.7.6.112`:

- Исходный83-function пакет:
  `/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.WYYx6P`.
- Новый91-function пакет:
  `/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.x4SovM`.
  TERMINAL11:20:49UTC, exit0,91 complete,40 requested/40 actual interfaces.
  Manifest SHA256 `ce2cac9db82eed8be5e9138ba8de9e7cca14a4c206a2aa0cb8f1654538760d6d`.
- Patched install:
  `/home/dima/Projects/ghidra_output/aiam-tool-5995e24caa-20260911.IBJLHp/install`.
- Read-only project `wifi_surface_25C56_full_20260711`, program
  `BootKC_guest_25C56.kc`; corrected returning memcpy annotation is retained
  by the batch script without saving the original reference database.

Восемь новых функций прочитаны полностью: getCurrentBSSAKMs,
getAssociatedAuthType, Core getBssPhyModde, WCL getCurrentPhyModde,
getBand/getChanSWSpec/getRSSI, WCLAdaptiveRoam handleRoamEventConfiguration.
Последний потребляет success status и сбрасывает adaptive policy state;
он не доказывает обязательность заполнения всех diagnostic fields.
Также прочитаны полностью WCLScanManager sendRequest/handleScanRequest и
NetManager handleRoamDoneEvent. Не повторять всю декомпиляцию без нужды.

RAM scratch: `/dev/shm/aiam-gui-roam-lifecycle-20260912.2AEEv7`.
**Долговечный архив**:
`/home/dima/Projects/itlwm/aiam-gui-roam-lifecycle-handoff-20260912.HyoPu3`.
Все213 файлов проверены по `SHA256SUMS`; SHA256 самого manifest:
`c761a2e25fdddd54a53628aa57d4f09c557553266e5d0d3b448d1548c75ba7b6`.
Включены source patch, все результаты проверок, copied91-function package,
скрипты, старый baseline и свежий guest state. Архив запечатан, не изменять.
`HANDOFF_CHECKPOINT.md` новее исторического `WIP_1122.md`.

## 6. Живая лаборатория: подтверждено read-only в11:30:36UTC

- Guest boot `7A5FAA36-5FAA-451F-A32B-70EB251B8660`, UUID AF16 выше.
- Guest `en1`, WPA3_SAE, DHCP BOUND `172.16.66.219`, BSSID
  `82:c3:97:84:51:ca`, channel9; MAC `4e:bc:8d:ff:50:23`.
- Native WiFiAgent PID5894/runs576/running; airport preferences читаются
  console user. Не запускать новый GUI-прогон при неисправной native service.
- Host AX211 `wlp0s20f3` управляется NetworkManager: LabAP,
  `82:c3:97:84:51:c9`, channel153, `172.16.66.226/24`.
- Host wired default `172.16.16.1` через `enx1cbfce6c92ea`,
  source `172.16.16.200`, не менять.
- Owned QEMU PID1406911 жив, имя `aiam-iwn-after-scd-control`.
  SSH3338/VNC5905 слушают; HTTP18089 не слушает.
- Все наши test/AP/monitor/capture/Ghidra/scp/push sessions к этому срезу
  terminal. Нет оставленной новой фоновой работы, которую надо ждать.

Доступ к гостю только через management forward:

```sh
ssh -F /dev/null -o HostKeyAlias=172.16.66.219 \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/tmp/aiam-tahoe-wifi.known_hosts \
  -o ControlMaster=no -o ControlPath=none -o BatchMode=yes \
  -o ConnectTimeout=5 -p3338 devops@127.0.0.1
```

Guest shell zsh; для скриптов явно `/bin/bash -s`, sudo-n доступен.
Console devops UID502. Пароли/ключи не добавлять в публичные commits.
USB-management guest en2/10.0.6.15 независим от тестируемого Wi-Fi.
VNC `/home/dima/.local/bin/vncdotool -s 127.0.0.1::5905`, экран1280x800;
перед кликом новый screenshot, старые координаты не считать актуальными.

QEMU monitor/pidfile:
`/home/dima/Projects/itlwm/aiam-iwn-profile-reset.bhi2aO/qemu-scd-control-monitor.sock`,
`qemu-scd-control.pid` в той же папке. Текущий overlay:
`/home/dima/Projects/itlwm/aiam-iwn-auth-beacon-runtime.yIJOEP/tahoe-auth-beacon.qcow2`;
serial `serial-auth-beacon.log` там же. VFIO6235 `0000:25:00.0`.
Перед lifecycle-действием сверить exact PID/name/overlay, не печатать полную
QEMU command line с OSK. Никаких broad pkill, изменений базового qcow2 или
соседнего apple-virgl QEMU/порта33100. Физический10.90.10.22 не трогать.
AX211/host-device binding и транспорт не менять ради этой передачи.

Ранее проверенные build/activation примеры находятся в архиве
`/home/dima/Projects/itlwm/aiam-gui-cache-runtime-20260912.gHMjC3`
(`build-gui-cache-q1.sh`, `activate-gui-cache-q1.sh`). Это исторические
скрипты с привязкой к конкретным input/boot/UUID: сначала прочитать и
адаптировать, не запускать вслепую. Никакой live kext unload.

## 7. GUI-результаты и открытая поверхность

Последние пять серий дают26 service controls:20 без потерь,6 с сохранёнными
потерями. Это не26 независимых закрытых клеток полной матрицы.

| Документ | Controls | Результат |
|---|---:|---|
| `TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md` | 7 | 7 PASS |
| `TAHOE_GUI_OPEN_SAVED_RECOVERY_20260912.md` | 6 | 5 PASS,1 loss |
| `TAHOE_GUI_SETTINGS_SECURITY_PAIRS_20260912.md` | 4 | 3 PASS,1 loss |
| `TAHOE_GUI_SETTINGS_WPA2_SAE_RECOVERY_20260912.md` | 7 | 5 PASS,2 loss |
| `TAHOE_GUI_SAE_LOCAL_L2_RECOVERY_20260912.md` | 2 | обе TCP/hash PASS, обе zero-loss gate FAIL |

Последний same-L2 прогон:600/600+598/600 после saved-SAE reselect,
593/600+593/600 после off/on; TCP~40s проходит в обоих. В первом нет
наблюдавшейся смены BSS; во втором13→9. Endpoint captures локализуют
отсутствующие запросы/ответы, но не точную RF/AP/driver причину.

Сети: pure-SAE LabAP виден на BSSID ca/ch9,02/ch13,c9/ch153; OpenWrt
имеет как PSK-only, так и transition BSS — SSID не определяет AKM.
Controlled `AIAM-GUI-OPEN-0912` и `AIAM-GUI-WPA2-0912` — сохранённые
fixture-профили, сейчас AP не поднят. Один host AX211 поддерживает один
нужный AP-slot; чистые open/WPA2/SAE fixtures были последовательными.

Для paired captures: routed guest→10.7.6.112 проходит SNAT10.7.6.2.
Фильтр только по guest IP пропускает эти forward packets. Использовать
маркер ICMP или независимый same-L2 host. Clock offset не калиброван:
идентифицировать пакеты, а не объявлять one-way latency по разным часам.

Настоящий S3 остаётся открыт: Wi-Fi/DHCP возвращались, но GUI застревал
в WindowServer/IOFBAcknowledgeNotification→IOFramebuffer extAck/sleepGate.
Перезагрузка восстановила GUI, но не является S3 PASS. См.
`TAHOE_GUI_STA_SLEEP_MATRIX_20260912.md`; точная причина владения gate
не установлена, не объявлять ни Wi-Fi, ни graphics единственной причиной.

WPA2 AP через GUI имеет отдельный реальный PASS с внешним DHCP/трафиком.
Mixed WPA2/WPA3 AP, полная post-sleep AP-матрица, ad-hoc и hardware runtime
покрытие IWM/IWX не закрыты. Прежние подтверждённые результаты не стирать
и не расширять на непроверенное оборудование или режимы.
