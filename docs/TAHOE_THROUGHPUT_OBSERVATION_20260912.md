# Throughput/rate observation on the WIP — needs controlled isolation

Срез: 2026-09-12, загруженный WIP.

## Наблюдение

`system_profiler` на ггосте: 802.11n, сигнал −47..−66 dBm (сильный), но
Transmit Rate=6/MCS 0 (idle) → под нагрузкой MCS 1 (13 Mbps). Bulk-скачивание
(curl http от 172.16.66.226 по en1): **335 B за 10s** — TCP-хендшейк+HTTP-хедеры
прошли, тело застряло. Мелкие пакеты (ping/DHCP/scan) работают.

## Оценка (честно, без мис-атрибуции)

1. `getRATE`/`system_profiler` ЧЕСТНО отражают низкий MCS — контакт-surface НЕ
   расходится (сообщает реальную скорость). Это НЕ contact-surface non-identity.
2. Паттерн «мелкие OK, bulk застревает» = data-path stall (aggregation/MTU/
   маршрут), НЕ чистая rate-control проблема (MCS 1 дал бы MB за 10s если бы
   данные текли).
3. **Не изолировано**: сервер 172.16.66.226 (host wlp0s20f3) на ДРУГОМ AP/канале,
   чем текущий AP гостя (OpenWrt ch5) — путь cross-AP bridged. en2-baseline
   непоказателен (QEMU slirp сам медленный). Нужен КОНТРОЛИРУЕМЫЙ same-AP/same-L2
   iperf (гость и iperf-сервер на одном AP), чтобы отделить itlwm от
   лаб-топологии / VFIO-passthrough.
4. Pre-existing (rate/data-path не трогались roam-WIP; AF16 вёл бы себя так же).

## Вывод

Реальное пользовательское наблюдение (медленный bulk), НО: getRATE честен
(идентичность контакт-surface соблюдена), корень не изолирован (вероятно
лаб-топология cross-AP или VFIO-passthrough, а не драйверная дивергенция).
Требует same-AP iperf для атрибуции. Вне строгой contact-surface-identity зоны,
пока не доказано, что это драйверный дефект против эталонного контракта.
Отдельно: `SLOW_WIFI_FEATURE_ENABLED`(0x187) и `setBYPASS_TX_POWER_CAP` — itlwm
их реализует (getSLOW_WIFI_FEATURE_ENABLED@6315, setBYPASS_TX_POWER_CAP@6459);
сверка их значений с эталоном — отдельный selector-audit item.
