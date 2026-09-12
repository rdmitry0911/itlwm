# GUI status read-surface — functional-equivalence runtime validation

Срез: 2026-09-12. По директивам пользователя (функциональная эквивалентность, не
только допустимые значения; приоритет = частота использования + GUI-ветви раньше
terminal/system) — проверка НАИБОЛЕЕ ЧАСТО читаемой GUI-поверхности (меню Wi-Fi /
network-info) на РЕАЛЬНУЮ функциональную эквивалентность, runtime на загруженном
WIP (`951D4653`, 6235/IWN) в живом госте (SSH en2 :3338).

## Метод

`system_profiler SPAirPortDataType` + `wdutil info` + собственные CoreWLAN/direct-
apple80211 инструменты на госте; независимая перекрёстная проверка эфира Linux
`iw scan` с ХОСТА (AX211 STA на той же сети LabAP).

## Результат: все ключевые GUI-поля функционально верны

| Поле (GUI/terminal) | itlwm | проверка | вердикт |
|---|---|---|---|
| RSSI | −47 dBm | реальный ni_rssi | ✓ |
| Noise/SNR | −93 dBm | реальный getBSSNoise | ✓ |
| Tx Rate | idle 6/MCS0 → под нагрузкой **MCS12/78 Mbps** | ramp подтверждён нагрузкой на en1 | ✓ (idle base rate = норма) |
| PHY Mode | 802.11n | реальный active-BSS mode | ✓ |
| Channel | 5 (2GHz,20MHz) | реальный | ✓ |
| Security | WPA2/WPA3 Personal | реальный RSN IE | ✓ |
| BSSID | реальный ni_bssid | ✓ |
| **Country Code** | **ZW** | **AP РЕАЛЬНО вещает Country: ZW** (Linux `iw scan` с хоста: `Country: ZW Environment: Indoor/Outdoor` для LabAP) | ✓ **верно** |
| Supported Channels | 2g 1–13 + 5g | **корректно для ZW** (не FCC 1–11) | ✓ |

## Ключевой урок (почему «prove first» важно)

Первичная гипотеза «Country=ZW при Locale=FCC — баг/мусор» ОКАЗАЛАСЬ НЕВЕРНОЙ.
Доказательство эфиром (независимый Linux-сканер на хосте) показало: физические
LabAP реально анонсируют 802.11d Country IE = **ZW**. Значит itlwm ЧЕСТНО читает
и репортит анонсированную AP страну — это функциональная ТОЖДЕСТВЕННОСТЬ с
эталоном (эталон так же прочитал бы beacon country IE → ZW). `copyCurrentBss-
80211dCountry` (парсинг country-элемента из IE текущего BSS) РАБОТАЕТ верно.
Список каналов 1–13 — тоже верен для ZW. Гипотеза о «slow wifi» (Tx Rate 6) —
тоже отклонена: это idle base rate, под нагрузкой рейт-контрол поднимает до
MCS12/78 Mbps (эталонное поведение).

## Остаток (минор, не доказуемая дивергенция)

`IO80211Locale` захардкожен в `FCC` (`localePropertyString(APPLE80211_LOCALE_FCC)`)
независимо от страны. Для ZW «FCC» спорен, НО: в эталоне (AppleBCMWLAN) НЕТ
отдельного getLOCALE-хендлера — locale выставляется как свойство рядом со страной
(`configureDefaultCountryCode`/`handleCountryCodeChangeToRepopulateChannels`),
эталонная деривация locale из country не доказана. Поэтому это НЕ доказуемая
нетождественность (нельзя чинить без эталонного контракта — иначе догадка/костыль).
Поле низкоприоритетное (редко-читаемое, в US-среде FCC верен).

## Вывод

Часто-используемая GUI read-поверхность (меню Wi-Fi + network-info: signal,
rate, PHY, channel, security, BSSID, country, channels) — **функционально
эквивалентна эталону, подтверждено runtime + независимой эфирной проверкой**.
Реальных дивергенций в Tier-1/2 не найдено; кандидат (country ZW) доказан КОРРЕКТНЫМ.
