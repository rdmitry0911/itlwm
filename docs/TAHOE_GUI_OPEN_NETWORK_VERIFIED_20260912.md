# Open-network connect path — runtime-verified on WIP

Срез: 2026-09-12. Пользователь разблокировал лаб-реконфиг (управление гостем —
независимый проводной en2). Поднята OPEN-AP фикстура и проверен open-путь на WIP.

## Фикстура (проверенный launcher)
`.../aiam-gui-open-recovery-runtime-20260912.KEtvcn/fixture-carrier.sh open <label> 1`
поверх host AX211 (wlp0s20f3): STA→unmanaged (не просто disconnect — NM-сканы
рвали AP), `iw dev wlp0s20f3 interface add uif3ap type __ap`, `ip addr add
192.168.73.1/24`, hostapd (ssid=AIAM-GUI-OPEN-0912, hw_mode=g, ch9, wpa=0),
dnsmasq (192.168.73.20-40), http 18089. Bounded 900s; cleanup восстанавливает STA
(nmcli con up UUID bbed72a6). Teardown чистый: FIXTURE_RESTORE_RESULT=0, uif3ap
удалён, host STA снова на LabAP.

## Результат на WIP (гость 6235/IWN)
Join через `networksetup -setairportnetwork en1 AIAM-GUI-OPEN-0912` (CoreWLAN
SSID-match не годится — location-фильтр прячет ssid у не-authorized процесса).
**DHCP 192.168.73.34 за 2с**, ch9, security=none (open), ping AP-gw 1.9-4.5мс.
Полный JOIN FSM контракт ЧИСТ: NET_MANAGER LINK_UP→LEAVE_NETWORK→DEAUTH→LINK_DOWN;
JOIN_MANAGER JOIN_REQ→IN_PROGRESS→TRY_NEXT_CANDIDATE→ASSOC_DONE→CONNECT_COMPLETE
→IDLE; NET_MANAGER→WAITING_FOR_CONNECT_COMPLETE→WAITING_FOR_IP; ROAM_MANAGER→LINK_UP.
Драйверных ошибок нет. После снятия фикстуры гость авто-переподключился к saved
сети (172.16.66.x).

Побочно: на прямом open-AP (чистый ch9) латентность 1.9-4.5мс — подтверждает, что
ранее наблюдавшийся медленный throughput = ОКРУЖЕНИЕ (перегруженный ch5), не
драйверная дивергенция.

## Итог: open-network путь идентичен/чист на WIP. Закрыт как user-demanded residual.
