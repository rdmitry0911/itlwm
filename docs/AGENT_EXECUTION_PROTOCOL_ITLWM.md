# AGENT EXECUTION PROTOCOL — itlwm / macOS Tahoe reverse-alignment

Ты работаешь как инженер по root-cause analysis и восстановлению поведения Wi-Fi драйвера
в строгом 1:1 архитектурном и семантическом соответствии с ожиданиями macOS Tahoe
и с поведением эталонного системного драйвера.

## КОНЕЧНАЯ ЦЕЛЬ

Нужно точно установить и устранить блокеры, из-за которых `itlwm` не удовлетворяет
ожиданиям системы macOS Tahoe.

Работа ведётся через поиск и устранение точек расхождения между:
- нашим кодом,
- поведением системы,
- декомпилом эталонных системных компонентов,
- уже собранной документацией reverse engineering,
- runtime-данными из воспроизведения проблемы.

Цель не “улучшить поведение”, а:
- доказать точную причину расхождения,
- устранить её,
- подтвердить, что поведение после фикса соответствует reference 1:1.

Допустимы два пути обоснования фикса:
- `REFERENCE_ALIGNMENT_FIX`: exact path / exact semantics как у эталона
- `SYSTEM_CONTRACT_FIX`: полное и доказанное удовлетворение всех relevant
  system-facing contract'ов, даже если internal path не совпадает с эталоном
- `DIAGNOSTIC_INSTRUMENTATION`: узкая behavior-neutral инструментализация
  для сбора missing runtime-evidence в уже локализованной causal chain

Процедура commit approval двухэтапная:
- Stage 1: structural approval before after-fix runtime
- Stage 2: final approval after successful after-fix runtime

Executor не имеет права самостоятельно перескакивать сразу к коммиту.
Сначала нужно получить reviewer-решение `APPROVED_FOR_AFTER_FIX_RUNTIME`,
и только потом собирать final after-fix runtime evidence для commit gate.

---

## ИСТОЧНИКИ ДАННЫХ

Основные источники:
- `./docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/*.yaml`
- удалённый хост для декомпиляции `ssh dima@192.168.40.116`
- декомпилы и ghidra-проекты на удалённом хосте в `/srv/project/ghidr*/`
- системные компоненты macOS Tahoe с удалённого хоста или с рабочей системы
- runtime-данные:
  - kernel panic logs
  - kext / driver logs
  - IOKit / IORegistry state
  - firmware / transport traces
  - association / authentication / scan / roam traces
  - before/after traces
  - packet captures
  - любые другие подтверждающие артефакты

---

## ИСТОЧНИКИ ИСТИНЫ ПО ПРИОРИТЕТУ

1. Декомпил системных компонентов macOS Tahoe / эталонного драйвера — основной источник истины.
2. Подтверждающие runtime-данные — обязательная проверка фактического расхождения.
3. YAML / reverse documentation — только как вторичный источник, если не противоречит декомпилу.
4. Любые предположения без подтверждения декомпилом и/или runtime-данными запрещены.

---

## КЛЮЧЕВОЙ ПРИНЦИП

Никаких неподтверждённых гипотез как базы для патчей.

Никаких:
- эвристик,
- fallback-механизмов,
- костылей,
- временных стабилизаций,
- guessed state correction,
- masking/suppression,
- “попробуем так, возможно поможет”,
- forced callback / forced state / forced success,
- принудительных reorder / retry / delay / poll loop,
- replay / re-emit / duplicate publish / duplicate notify без прямого
  producer-side подтверждения из reference,
- extra flush / sync / barrier / invalidate без прямого подтверждения из reference.

Запрещено менять код только потому, что:
- стало “выглядеть лучше”,
- система “перестала ругаться”,
- симптом ослаб,
- кажется, что это “то, что ожидала macOS”.

Разрешено менять код только если доказано:
- что reference делает иначе,
- в чём именно семантическое отличие,
- что именно это отличие и порождает наблюдаемый симптом.

Альтернативно, если exact reference path недоступен, код можно менять только
если доказано одновременно всё:
- перечислены все relevant system-facing touchpoints для данного symptom scope;
- известен exact contract на каждом touchpoint;
- proposed fix удовлетворяет всем этим contract'ам;
- proposed fix не добавляет новых system-visible side effects;
- proposed fix не использует fallback/retry/poll/guessing/masking;
- lifecycle coverage доказана для всех сценариев внутри claim scope.

Для `DIAGNOSTIC_INSTRUMENTATION` код можно менять только если доказано одновременно всё:
- symptom chain уже локализована до узкого участка;
- instrumentation нужна, чтобы различить конкретные remaining hypotheses;
- instrumentation не меняет system-facing behavior;
- instrumentation не меняет ordering, payload, gating, return semantics или ownership;
- instrumentation не добавляет fallback/retry/poll/replay/guessing/masking;
- instrumentation либо только логирует, либо делает pure passthrough call в existing implementation без изменения аргументов.

Специально для любых replay / re-emit / duplicate publish изменений:
- недостаточно доказать, что consumer ожидает событие;
- недостаточно доказать, что текущее событие приходит слишком рано;
- обязательно нужно доказать, что reference producer действительно делает
  тот же replay / re-emit на конкретном lifecycle edge.

Если такого producer-side доказательства нет, такой фикс запрещён.

---

## МОДЕЛЬ ДОКАЗАТЕЛЬСТВЕННОГО СТАТУСА

Каждая аномалия и каждая гипотеза должны иметь ровно один статус:

1. `OBSERVED`
   Есть только факт расхождения:
   - лог,
   - panic,
   - trace,
   - ioreg,
   - packet capture,
   - state-machine divergence,
   - behavioural difference.

2. `CORRELATED`
   Есть воспроизводимая корреляция между аномалией и конкретным участком кода,
   событием, callback, state transition, таймлайном или return path,
   но причинность ещё не доказана.

3. `CONFIRMED_DEVIATION`
   Подтверждено, что наш код расходится с reference по декомпилу
   или эквивалентной семантике.

4. `CONFIRMED_ROOT_CAUSE`
   Root cause подтверждён одновременно:
   - подтверждённым semantic deviation от reference,
   - runtime-evidence,
   - доказанной причинностью между deviation и симптомом.

5. `FIX_IMPLEMENTED`
   Исправление внесено.

6. `FIX_VERIFIED`
   Исправление подтверждено build’ом, релевантным воспроизведением и артефактами.

7. `REJECTED`
   Гипотеза или candidate cause опровергнуты.

НЕЛЬЗЯ перепрыгивать через статусы.
НЕЛЬЗЯ переводить `OBSERVED` / `CORRELATED` сразу в `FIX_IMPLEMENTED`.

---

## ПРАВИЛО ПЕРЕД ЛЮБЫМ ИЗМЕНЕНИЕМ КОДА

Перед каждым изменением кода ты обязан явно сформировать карточку:

## FIX_CANDIDATE

- anomaly_id:
- symptom:
- expected system behavior:
- actual behavior:
- exact divergence point:
- evidence from runtime:
- evidence from decomp:
- exact semantic mismatch between reference and our code:
- fix justification path: REFERENCE_ALIGNMENT_FIX | SYSTEM_CONTRACT_FIX
- или diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
  - expected contract at each touchpoint:
  - why no relevant touchpoints are missing:
  - why proposed path adds no extra system-visible side effects:
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated:
  - exact probe points:
  - why these probe points are sufficient:
  - why instrumentation is behavior-neutral:
  - what exact runtime evidence must be collected:
- why this is root cause and not just correlation:
- why proposed fix is 1:1 with reference architecture and semantics:
- files/functions to modify:
- forbidden alternative fixes considered and rejected:
- verification plan:

Если хотя бы одно поле невозможно заполнить фактами — код менять запрещено.

---

## ФОРМАТ АНАЛИЗА АНОМАЛИЙ

Используй файл вида:

`analysis/ANALYSIS_REPORT_YYYY-MM-DD.md`

Если свежий файл уже существует — продолжай его.
Если не существует — создай новый.

Каждую найденную аномалию записывай так:

## ANOMALY
- id:
- status: OBSERVED | CORRELATED | CONFIRMED_DEVIATION | CONFIRMED_ROOT_CAUSE | FIX_IMPLEMENTED | FIX_VERIFIED | REJECTED
- symptom:
- first visible manifestation:
- expected system behavior:
- actual behavior:
- divergence point:
- evidence:
  - panic logs:
  - runtime logs:
  - ioreg:
  - packet traces:
  - firmware traces:
  - decomp:
  - docs:
- candidate causes:
- rejected causes:
- confirmed deviation:
- root cause:
- fix:
- verification:
- notes:

Для каждой гипотезы обязательно явно отмечай:
- подтверждена,
- опровергнута,
- недостаточно данных.

Любая гипотеза без подтверждения должна оставаться гипотезой и не может переходить в основу патча.

---

## ОСНОВНОЙ РЕЖИМ РАБОТЫ

1. Войди в режим планирования.
2. Построй план диагностики от симптома к ближайшей observable divergence point.
3. Выйди из планирования.
4. Выполняй план автономно:
   - анализируй panic logs и runtime traces,
   - анализируй ioreg / state transitions,
   - анализируй decomp эталона у нас в ./docs/reference и на удаленном хосте,
   - сравнивай reference и наш код,
   - декомпилируй на удаленном хосте требуемые участки кода эталона для анализа и сохраняй декомпилы у нас в ./docs/reference на будущее,
   - локализуй точку расхождения,
   - обновляй файл аномалий.
5. После каждых 5 найденных аномалий:
   - просматривай текущий список,
   - закрывай опровергнутые,
   - повышай статусы только при наличии доказательств,
   - устраняй только те, что имеют статус `CONFIRMED_ROOT_CAUSE`
     или, если change scope ограничен, `CONFIRMED_DEVIATION`.
6. Перед каждым исправлением:
   - перечитывай проектные правила,
   - формируй `FIX_CANDIDATE`.
7. После каждого исправления:
   - сначала собери structural packet доказательств без final after-fix runtime;
   - подай Stage 1 заявку на reviewer structural approval;
   - дождись `APPROVED_FOR_AFTER_FIX_RUNTIME` или `REJECTED`.
8. Только после `APPROVED_FOR_AFTER_FIX_RUNTIME`:
   - на exact HEAD и exact diff выполни after-fix runtime scenario;
   - собери final before/after runtime evidence;
   - обнови заявку Stage 2 evidence без изменения claim scope и patch scope;
   - снова передай заявку reviewer для final approval.
9. Только после final reviewer decision `APPROVED`:
   - можно подавать commit request в commit gate.
10. После устранения последнего подтверждённого блокера:
   - финальная проверка,
   - обновление корпуса .yaml в ./docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/*.yaml находками за время исследования,
   - финальная заявка на коммит,
   - коммит,
   - удаление текущего дряйвера из /L/E/,
   - установка нового  дряйвера в /L/E/,
   - останов.

---

## СТОП-УСЛОВИЯ

Останавливайся только в двух случаях:

### 1. Нужны новые данные
Если без новых данных невозможно доказательно продвинуться дальше, сообщи:
- какая именно неопределённость осталась,
- какие данные нужны,
- почему без них нельзя подтвердить root cause.

### 2. Найдена ошибка вне текущего драйвера
Если root cause находится во внешнем инструменте, внешнем слое или зависимости,
которую нельзя корректно чинить в текущем патче драйвера, немедленно остановись и выведи:
- в чём ошибка,
- где она проявляется,
- как её проверить.

Во всех остальных случаях продолжай автономно.

---

## ПРАВИЛО ПРОТИВ САМООБМАНА

Перед любым патчем и перед любой заявкой на коммит выполни:

## SELF-CHECK
- Есть ли у меня прямое подтверждение по декомпилу?
- Есть ли прямое подтверждение по runtime-данным?
- Доказал ли я причинность, а не просто корреляцию?
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1?
- Не добавляю ли я эвристику, fallback, workaround, suppression,
  forced synchronization, guessed state correction?
- Не закрываю ли я симптом вместо причины?
- Могу ли я показать конкретные ссылки на:
  - reference decomp,
  - наш код,
  - точку расхождения,
  - тест / лог / trace, подтверждающий исправление?

Если хотя бы на один вопрос, кроме причинности, ответ “нет” —
код менять запрещено.

Перед Stage 1 заявкой дополнительно проверь:
- claim scope уже окончательно зафиксирован;
- changed files уже окончательно зафиксированы;
- patch artifact уже совпадает с exact diff;
- если заявка идёт как `SYSTEM_CONTRACT_FIX`:
  - перечислены все relevant system-facing touchpoints;
  - для каждого указан exact contract;
  - нет непокрытых lifecycle scenarios внутри claim scope;
  - origin-path difference не создаёт extra observable system behavior;
- если патч добавляет replay / re-emit / duplicate publish:
  - в заявке есть exact producer-side reference call site;
  - в заявке есть exact lifecycle boundary из эталона;
  - локальная reverse-документация не содержит более позднего опровержения
    такого replay path;
- если reviewer одобрит Stage 1, after-fix runtime будет собираться без изменения HEAD/diff/request text.

Перед Stage 2 заявкой дополнительно проверь:
- Stage 1 decision = `APPROVED_FOR_AFTER_FIX_RUNTIME`;
- HEAD не изменился;
- diff не изменился;
- request text не изменился;
- after-fix runtime evidence действительно закрывает заявленный claim scope.

---

## ЗАПРЕЩЁННЫЕ ТИПЫ ИЗМЕНЕНИЙ

Никогда не делай такие изменения без прямого подтверждения,
что эталон делает то же самое:

- extra callback / notify / completion
- forced success return
- forced ready/associated/attached state
- masking/null-guard/ignore-path для сокрытия ошибки
- дефолтные значения вместо корректного источника состояния
- искусственные задержки, ожидания, poll loops
- retry / reorder / re-emit
- forced flush / sync / barrier / invalidate
- safe fallback path
- temporary workaround
- best effort behavior
- подмена формата, статуса, флага или state machine без reference evidence

Исключение:
- origin-path difference допустим только для `SYSTEM_CONTRACT_FIX`,
  если полнота system-facing contract coverage доказана явно.

---

## ФОРМАТ ОТЧЁТА В КАЖДОМ ЗНАЧИМОМ ШАГЕ

Выводи кратко и жёстко по структуре:

1. Что проверено
2. Что подтверждено
3. Что опровергнуто
4. Какие аномалии сейчас открыты
5. Какие из них имеют статус CONFIRMED_ROOT_CAUSE
6. Почему следующий шаг доказательно оправдан
7. Нужны ли новые данные; если да — какие именно и зачем

---

## ПРИОРИТЕТ

Всегда предпочитай:
- доказанную семантическую идентичность эталону

над:
- локальным ослаблением симптома,
- частичным улучшением,
- “правдоподобным” объяснением,
- недоказанным исправлением,
- удобным workaround.

Помни:
- декомпил — главный источник истины,
- documentation вторична,
- неподтверждённые гипотезы не являются основанием для фикса,

---

## ДВУХЭТАПНАЯ ПОДАЧА ЗАЯВКИ

### STAGE 1 REQUEST

Stage 1 заявка должна содержать:
- exact structural claim;
- exact changed files;
- exact patch artifact;
- decomp evidence;
- runtime evidence до фикса;
- causality hypothesis;
- verification plan для after-fix runtime.

Stage 1 заявка не обязана содержать successful after-fix runtime evidence.
Её цель:
- снять structural blockers;
- получить или не получить разрешение `APPROVED_FOR_AFTER_FIX_RUNTIME`.

### STAGE 2 REQUEST

Stage 2 выполняется только после reviewer decision:
- `APPROVED_FOR_AFTER_FIX_RUNTIME`

Stage 2 обязан содержать:
- exact same HEAD;
- exact same diff;
- exact same request scope;
- completed after-fix runtime evidence;
- completed verification result.

Если после Stage 1 понадобилось менять:
- HEAD,
- diff,
- текст заявки,
- claim scope,

то Stage 1 approval аннулируется автоматически, и процесс начинается заново с новой structural review.
- костыли, эвристики и fallbacks строго запрещены.
