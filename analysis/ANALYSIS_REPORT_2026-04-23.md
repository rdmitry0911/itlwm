# ANALYSIS REPORT 2026-04-23

## ANOMALY
- id: A-REGDIAG-BOOT-BIND-001
- status: CORRELATED
- symptom: после добавления прежнего диагностического слоя драйвер переставал быть видимым в Wi-Fi UI либо зависал в конце инициализации.
- first visible manifestation: новый драйвер после перезагрузки не появлялся в UI; в другой попытке загрузка зависала в фазе драйвера.
- expected system behavior: состояние CR-052 остается видимым в UI и показывает сети; диагностический слой не меняет system-facing topology, ordering, payload, return semantics или ownership.
- actual behavior: прежняя диагностика добавляла отдельную IOKit/userclient поверхность и меняла код на чувствительных путях, после чего UI binding регрессировал.
- divergence point: диагностическая поверхность была заведена через отдельный service/userclient и через изменения, затрагивающие boot/control path.
- evidence:
  - panic logs: runtime от пользователя: зависание при инициализации после установки диагностической сборки.
  - runtime logs: runtime от пользователя: драйвер не виден в UI после установки диагностической сборки.
  - ioreg: требуется новый after-fix снимок для текущего патча.
  - packet traces: отсутствуют.
  - firmware traces: отсутствуют.
  - decomp: для текущей diagnostic-only задачи не используется как fixing evidence; патч не заявляет исправление Tahoe contract.
  - docs: AGENT_EXECUTION_PROTOCOL_ITLWM допускает только behavior-neutral `DIAGNOSTIC_INSTRUMENTATION`.
- candidate causes:
  - подтверждена корреляция с изменением IOKit topology/userclient surface.
  - подтверждена корреляция с инструментированием чувствительных control-path методов.
  - недостаточно данных для утверждения единственной root cause.
- rejected causes:
  - CR-052 как база не отвергнута: пользователь подтвердил, что CR-052 виден в UI и показывает сети.
- confirmed deviation: нет, это не фикс behavior; это замена способа сбора runtime evidence.
- root cause: не подтверждена.
- fix: не применяется; вносится только диагностическое инструментирование.
- verification: сборка Tahoe, проверка отсутствия `IOUserClient`/новых diagnostic service/class строк, runtime пользователя: драйвер должен остаться видимым в UI, затем `airport_itlwm_regdiag get snapshot/trace` после попытки подключения.
- notes: рабочий пример `../VoodooHDA` использует runtime flags и telemetry, но его IOUserClient-подход не переносится на AirportItlwm из-за уже наблюдавшейся чувствительности Wi-Fi binding surface.

## FIX_CANDIDATE

- anomaly_id: A-REGDIAG-BOOT-BIND-001
- symptom: нужен диагностический слой для поиска причины, почему видимые в UI сети не подключаются, но прежний способ диагностики ломал загрузку/UI binding.
- expected system behavior: без явного runtime-включения диагностика не меняет загрузку, IOKit matching, userclient negotiation, Wi-Fi control return path, packet ownership или event ordering.
- actual behavior: прежний диагностический слой был небезопасен для boot/UI path.
- exact divergence point: диагностический способ, а не CR-052 networking logic: отдельный service/userclient и изменения на contested control path.
- evidence from runtime: пользователь подтвердил, что CR-052 виден в UI и показывает сети; диагностические сборки после этого не видны в UI или зависают.
- evidence from decomp: не заявляется `REFERENCE_ALIGNMENT_FIX`; это `DIAGNOSTIC_INSTRUMENTATION`.
- exact semantic mismatch between reference and our code: не применяется, патч не исправляет system contract.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- exact hypotheses being disambiguated:
  - H1: framework вызывает hidden WCL associate carrier, а не public `setASSOCIATE`.
  - H2: public/hidden associate доходит до драйвера, но net80211 state/auth/RSN/BSSID параметры не приводят к переходу в RUN.
  - H3: link state меняется, но TX/RX/EAPOL path не проходит после association.
  - H4: TX или RX Skywalk path теряет EAPOL/data после association.
- exact probe points:
  - `getAWDL_PEER_TRAFFIC_STATS` только как hidden-assoc carrier seam.
  - `setASSOCIATE` и `setWCL_ASSOCIATE` на входе/выходе.
  - `setLinkStateGated` после существующего `setLinkState`.
  - `outputPacket` и `skywalkRxInput` только счетчики/результаты пакетов и EAPOL marker.
  - `watchdogAction` только публикация registry snapshot/trace после явного enable.
- why these probe points are sufficient: они покрывают переход от UI-visible network selection к public/hidden associate, затем link notification, затем TX/RX/EAPOL data path.
- why instrumentation is behavior-neutral: по умолчанию все flags равны нулю; нет новых IOKit class/service/userclient/personality; нет новых полей в `AirportItlwm`; `get` из userspace читает только IORegistry свойства; `set` пишет только строковое свойство, применяемое существующим watchdog раз в секунду; без `Intervention` block flags не меняют return path.
- what exact runtime evidence must be collected: `AirportItlwmDiagSnapshot`, `AirportItlwmDiagTrace`, kext log around one connection attempt, and optional packet-level counters with EAPOL markers.
- why this is root cause and not just correlation: root cause не заявляется; патч предназначен для сбора missing runtime-evidence.
- why proposed fix is 1:1 with reference architecture and semantics: не заявляется; diagnostic path deliberately out-of-band and system-invisible until manually enabled.
- files/functions to modify:
  - `include/ClientKit/AirportItlwmRegDiag.h`
  - `AirportItlwm/AirportItlwmRegDiag.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwmRegDiag/airport_itlwm_regdiag.c`
  - `scripts/build_regdiag.sh`
- forbidden alternative fixes considered and rejected:
  - новый `IOUserClient`: отвергнут, уже коррелировал с UI binding regression.
  - новый diagnostic `IOService`/`registerService`: отвергнут, меняет IOKit topology.
  - добавление member field в `AirportItlwm`: отвергнуто, меняет object layout.
  - инструментирование `processApple80211Ioctl`/`isCommandProhibited`: отвергнуто как contested control seam.
  - принудительный success/retry/replay/reorder: запрещено протоколом.
- verification plan: `git diff --check`, сборка CLI, сборка Tahoe kext с BootKC symbol check, строковая проверка отсутствия старых diagnostic service/userclient поверхностей.

## ANOMALY
- id: A-REGDIAG-CONTROL-SET-001
- status: FIX_IMPLEMENTED
- symptom: CLI `set` не включает диагностику в уже загруженном драйвере.
- first visible manifestation: `airport_itlwm_regdiag on` вернул `IORegistryEntrySetCFProperty failed: 0xe00002c7`.
- expected system behavior: user-space control write должен доходить до существующего `AirportItlwm` без userclient и без изменения IOKit topology.
- actual behavior: `IORegistryEntrySetCFProperty` вызывает kernel-side `setProperties`, а inherited implementation возвращает `kIOReturnUnsupported`.
- divergence point: отсутствовал узкий `AirportItlwm::setProperties(OSObject *)` для диагностического mailbox key.
- evidence:
  - panic logs: нет.
  - runtime logs: нет.
  - ioreg: `AirportItlwm` active/registered, но `AirportItlwmDiag*` properties отсутствуют после failed set.
  - packet traces: нет.
  - firmware traces: нет.
  - decomp: не требуется; это diagnostic transport bug.
  - docs: протокол допускает behavior-neutral diagnostic instrumentation.
- candidate causes:
  - подтверждено: registry write не применялся из-за отсутствия `setProperties`.
- rejected causes:
  - права пользователя/root: `sudo -n` вернул тот же `0xe00002c7`.
- confirmed deviation: диагностический mailbox не имел kernel-side consumer для user-space `set`.
- root cause: для control-plane диагностики подтверждена; не является root cause Wi-Fi association failure.
- fix: добавить override существующего virtual slot `setProperties(OSObject*)`, принять только `AirportItlwmDiagControl`, применить команду и делегировать unknown properties в `super`.
- verification: build + BootKC symbol check; after reboot `airport_itlwm_regdiag on` должен вернуть control string, а через watchdog должны появиться snapshot/trace.
- notes: override не добавляет class/service/userclient/personality и не меняет object layout.

## ANOMALY
- id: A-ASSOC-SET-MAC-001
- status: FIX_IMPLEMENTED
- symptom: сеть видна в UI/scan, но попытка подключения к SSID `btn-vno` завершается до входа в локальные `setASSOCIATE` / `setWCL_ASSOCIATE`.
- first visible manifestation: `networksetup -setairportnetwork en0 btn-vno ...` печатает `Failed to join network btn-vno` и `tmpErr`.
- expected system behavior: перед association WCLJoinManager успешно применяет private/link MAC через `APPLE80211_IOC_SET_MAC_ADDRESS`, затем продолжает join path.
- actual behavior: `APPLE80211_IOC_SET_MAC_ADDRESS` возвращает `0xe00002c7`, после чего WCLJoinManager abort'ит association и IO80211Family возвращает `-536870201`.
- divergence point: current Tahoe runtime вызывает `APPLE80211_IOC_SET_MAC_ADDRESS(368)` с 9-байтным carrier; на локальном драйвере этот carrier попадает в `getAWDL_PEER_TRAFFIC_STATS(len=0x9)` и возвращает `kIOReturnUnsupported`.
- evidence:
  - panic logs: нет.
  - runtime logs: `cmdIouc@145:Fail to Set cmd=<APPLE80211_IOC_SET_MAC_ADDRESS, 368> res=<unknown Apple80211 ReturnToString, 0xe00002c7>`.
  - runtime logs: `handleJoinRequest@1215:WCLJoinManager unable to set mac addre rVal[-536870201]`.
  - runtime logs: `Exit-setASSOCIATE:153 ret:-536870201`.
  - runtime logs: `itlwm: DEBUG VTABLE [470] getAWDL_PEER_TRAFFIC_STATS len=0x9` immediately before the SET_MAC_ADDRESS failure.
  - diagnostic snapshot: после join attempt `public_assoc=0 hidden_assoc=0`, значит failure происходит до локальных assoc handlers.
  - decomp: `IO80211Family_decompiled.c` `setSET_MAC_ADDRESS` rejects NULL, then calls the common MAC helper with mode `2`.
  - decomp: common MAC helper copies the first six bytes into interface MAC state, calls the link-layer address setter when the interface is enabled, posts message `0x3b` with the 6-byte MAC payload, and updates the MAC registry/property state.
- candidate causes:
  - confirmed: missing local handling for the SET_MAC_ADDRESS carrier causes WCLJoinManager to abort before association.
- rejected causes:
  - scan/UI visibility: rejected, runtime sees `btn-vno` BSSIDs and UI lists networks.
  - public/hidden associate body parsing: not reached in this failure; counters remain zero.
  - diagnostic layer boot regression: rejected for this symptom; current driver is visible and scan works.
- confirmed deviation: local `len=0x9` SET_MAC_ADDRESS carrier returns unsupported instead of reference MAC-update semantics.
- root cause: confirmed for the current visible-networks-but-cannot-connect symptom up to the first join gate.
- fix: route the observed 9-byte carrier into local `setSET_MAC_ADDRESS`, copy the first six bytes to `ic_myaddr`/ifnet link-layer address, publish `APPLE80211_M_LINK_ADDRESS_CHANGED(0x3b)` with a 6-byte payload, update `kIOMACAddress`, and return success.
- verification: build + BootKC symbol check; after reboot, repeat one join attempt and confirm SET_MAC_ADDRESS no longer returns `0xe00002c7`; next failure, if any, must occur later than WCLJoinManager `unable to set mac`.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-SET-MAC-001
- symptom: UI-visible networks cannot be joined; join aborts before local assoc handlers.
- expected system behavior: `APPLE80211_IOC_SET_MAC_ADDRESS(368)` accepts the private/link MAC carrier and updates interface/link-layer MAC state before association.
- actual behavior: the 9-byte carrier is dispatched to the local slot named `getAWDL_PEER_TRAFFIC_STATS` and returns `kIOReturnUnsupported`.
- exact divergence point: `AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS(data,len=0x9)` currently falls through to unsupported.
- evidence from runtime: sudo logs show `getAWDL_PEER_TRAFFIC_STATS len=0x9` immediately followed by `Fail to Set cmd=<APPLE80211_IOC_SET_MAC_ADDRESS, 368> res=0xe00002c7`, `WCLJoinManager unable to set mac`, and `Exit-setASSOCIATE ret:-536870201`.
- evidence from decomp: `IO80211Family_decompiled.c` lines around `203312..203329` identify `setSET_MAC_ADDRESS`; lines around `187715..187756` show the shared MAC helper copies six bytes, updates link-layer state, posts message `0x3b`, and updates MAC property state.
- exact semantic mismatch between reference and our code: reference treats this as a MAC update operation; local code treats the same carrier as an unsupported AWDL/unknown fallback.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: WCLJoinManager logs the failed SET_MAC_ADDRESS as the immediate reason for aborting handleJoinRequest, and local assoc counters prove association body handling is not reached.
- why proposed fix is 1:1 with reference architecture and semantics: it implements the same externally visible MAC-state update, message `0x3b`, property update, and success return for the same carrier, without retries/replays/delays or init-path changes.
- files/functions to modify:
  - `include/Airport/apple80211_ioctl.h`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
- forbidden alternative fixes considered and rejected:
  - blind `return kIOReturnSuccess`: rejected, would mask WCL gate without updating MAC/link-layer state.
  - calling `AirportItlwm::setHardwareAddress`: rejected, it disables/enables the HAL when active and adds non-reference side effects.
  - changing vtable layout broadly: rejected for this batch, high boot/UI regression risk and not required to satisfy the observed SET_MAC_ADDRESS carrier.
  - adding userclient/service/plist diagnostics: rejected, unrelated and previously regressed UI binding.
- verification plan: `git diff --check`; Tahoe build; BootKC undefined-symbol check; confirm artifact contains the current git hash; user runtime should show no `Fail to Set cmd=<APPLE80211_IOC_SET_MAC_ADDRESS, 368>` on the next join attempt.

## FIX_CANDIDATE

- anomaly_id: A-BUILD-SKYWALK-CALLBACK-ABI-001
- symptom: Tahoe build initially fails in `AirportItlwmV2.cpp`; changing local callbacks to match the unpatched SDK then compiles but fails BootKC symbol verification.
- expected system behavior: local `MacKernelSDK` declarations for Skywalk queue callbacks match symbols exported by the target BootKC.
- actual behavior: restored CR-052 code matches BootKC (`UInt32` callbacks, TX `IOSkywalkPacket * const *`), but the local SDK headers declare `IOReturn` callbacks and TX `const IOSkywalkPacket **`.
- exact divergence point: unpatched SDK headers force clang to emit references to non-exported `withPool(... int (*)(...))` symbols instead of exported `withPool(... unsigned int (*)(...))` symbols.
- evidence from build: clang rejects CR-052 callback signatures against unpatched SDK typedefs.
- evidence from BootKC symbol check: the `IOReturn` local change produces unresolved `IOSkywalkTxSubmissionQueue::withPool(... int (*)(... const IOSkywalkPacket ** ...))` and `IOSkywalkRxCompletionQueue::withPool(... int (*)(...))`.
- evidence from system contract: `nm -g /System/Library/KernelCollections/BootKernelExtensions.kc | c++filt` shows exported `withPool` overloads use `unsigned int (*)(...)` and TX `IOSkywalkPacket* const*`.
- fix justification path: SYSTEM_CONTRACT_FIX
- fix: keep CR-052 callback signatures in driver code and extend `scripts/build_tahoe.sh` to patch local `MacKernelSDK` Skywalk queue typedefs to BootKC ABI before building.
- why this is behavior-neutral for the Wi-Fi regression under investigation: it changes only local build declarations; driver runtime topology, IORegistry diagnostics, scan, and association control paths are unchanged.
- verification plan: rebuild Tahoe target and run BootKC undefined-symbol check; no unresolved `IOSkywalk*Queue::withPool` symbols may remain.

## FIX_CANDIDATE

- anomaly_id: A-BUILD-SUPPLICANT-CHECK-PIPEFAIL-001
- symptom: `scripts/build_tahoe.sh` can stop at `ERROR: Tahoe target missing USE_APPLE_SUPPLICANT` even when `xcodebuild -showBuildSettings` contains `USE_APPLE_SUPPLICANT`.
- expected system behavior: sanity check only fails when the effective Tahoe target definitions actually lack `USE_APPLE_SUPPLICANT`.
- actual behavior: with `set -o pipefail`, `printf "$BUILD_SETTINGS" | grep -q USE_APPLE_SUPPLICANT` can return failure after `grep -q` exits early and `printf` receives SIGPIPE.
- evidence: direct `xcodebuild -showBuildSettings ... | rg USE_APPLE_SUPPLICANT` shows the target definition present; the script still emitted the missing-supplicant error.
- fix justification path: DIAGNOSTIC_INSTRUMENTATION
- fix: replace the pipe/`grep -q` check with shell pattern matching against the captured build settings.
- why this is behavior-neutral: it only corrects a build-script assertion and does not alter driver binary code.
- verification plan: rerun `scripts/build_tahoe.sh`; it must proceed past the sanity check and complete BootKC symbol verification.

## ANOMALY
- id: A-ASSOC-SET-MAC-GATE-002
- status: FIX_IMPLEMENTED
- symptom: после установки exact `CR-064` driver сети видны, но join к `btn-vno` всё ещё падает до association.
- first visible manifestation: `airportd` возвращает `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ASSOCIATE ... return 1/0x00000001`.
- expected system behavior: `APPLE80211_IOC_SET_MAC_ADDRESS(368)` должен попасть в MAC-update path и вернуть success после обновления MAC state.
- actual behavior: `APPLE80211_IOC_SET_MAC_ADDRESS(368)` возвращает raw `1`; локальные логи `routing set-mac carrier` / `setSET_MAC_ADDRESS` отсутствуют.
- divergence point: `AirportItlwmSkywalkInterface::isCommandProhibited(int)` short-circuit'ит `APPLE80211_IOC_SET_MAC_ADDRESS` через public fallback gate `[411]`, потому что `isTahoePublicFallbackRequest(...)` ошибочно включал selector `368`.
- evidence:
  - panic logs: нет.
  - runtime logs: loaded driver `AirportItlwm build=fe953b4`.
  - runtime logs: `cmdIouc@145:Fail to Set cmd=<APPLE80211_IOC_SET_MAC_ADDRESS, 368> res=<unknown Apple80211 ReturnToString, 0x1>`.
  - runtime logs: `handleJoinRequest@1215:WCLJoinManager unable to set mac addre rVal[1]`.
  - runtime logs: `Exit-setASSOCIATE:153 ret:1`.
  - runtime logs: нет `routing set-mac carrier` и нет `setSET_MAC_ADDRESS`, значит handler не достигнут.
  - decomp: `setSET_MAC_ADDRESS` reference wrapper вызывает common MAC helper with mode `2`, а не public fallback `[411]` raw-return path.
- candidate causes:
  - confirmed: inclusion of `APPLE80211_IOC_SET_MAC_ADDRESS` in the public fallback gate returns raw `1` before the set-mac handler can run.
- rejected causes:
  - handler body returning `1`: rejected, handler logs are absent and local handler returns `kIOReturnSuccess`.
  - old unsupported carrier path: rejected for current runtime, error changed from `0xe00002c7` to raw `1`.
- confirmed deviation: `SET_MAC_ADDRESS` was treated as a public fallback request, but reference treats it as a MAC-update operation.
- root cause: confirmed for the current post-CR-064 `rVal[1]` join abort.
- fix: remove `APPLE80211_IOC_SET_MAC_ADDRESS` from `isTahoePublicFallbackRequest(...)`; keep explicit set-side routing and carrier handler.
- verification: build + BootKC symbol check; after reboot, join attempt must no longer show `SET_MAC_ADDRESS` returning `0x1` and must show either local set-mac handler logs or a later join failure.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-SET-MAC-GATE-002
- symptom: exact `CR-064` runtime still aborts join before association; WCL reports `SET_MAC_ADDRESS` failure with raw `1`.
- expected system behavior: the set-mac carrier should execute the reference MAC-update semantics and return success.
- actual behavior: the public fallback gate returns non-zero raw `1` before the set-mac handler runs.
- exact divergence point: `AirportItlwmSkywalkInterface::isTahoePublicFallbackRequest(...)` included `APPLE80211_IOC_SET_MAC_ADDRESS`, so `isCommandProhibited(...)` returned `true` at slot `[411]`.
- evidence from runtime: sudo logs at `2026-04-23 15:53:34` show `Fail to Set cmd=<APPLE80211_IOC_SET_MAC_ADDRESS, 368> res=0x1`, `WCLJoinManager unable to set mac`, and `Exit-setASSOCIATE ret:1`; no local `setSET_MAC_ADDRESS` log appears.
- evidence from decomp: `IO80211Family_decompiled.c` lines around `203319..203326` show `setSET_MAC_ADDRESS` forwarding non-null carrier to the common MAC helper with mode `2`; lines around `187739..187755` show MAC copy, link-layer update, message `0x3b`, and property update.
- exact semantic mismatch between reference and our code: reference executes a MAC-update operation; local code short-circuits the selector through a public fallback/prohibition gate and leaks raw `1`.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the error value changed from unsupported `0xe00002c7` to raw `1` exactly after adding the selector to the public fallback set, and absence of handler logs proves the intended MAC-update path is bypassed.
- why proposed fix is 1:1 with reference architecture and semantics: removing the selector from the fallback gate allows the already implemented set-mac MAC-update path to own this command, matching the reference wrapper/helper split.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - returning `false` for all public fallback requests: rejected, would regress prior UI-visible request gates.
  - mapping raw `1` to success in WCL path: rejected, would mask failure without MAC update.
  - forcing success from `isCommandProhibited`: rejected, current bug is exactly a raw gate return.
  - adding retry/replay/delay: rejected, no reference basis.
- verification plan: `git diff --check`; Tahoe build; BootKC undefined-symbol check; create new exact-diff request; after approval install/reboot and verify `SET_MAC_ADDRESS` no longer returns `0x1`.

## ANOMALY
- id: A-ASSOC-HIDDEN-NULL-PSK-003
- status: FIX_IMPLEMENTED
- symptom: после установки exact CR-065 сети видны, но первая попытка подключения вызывает kernel panic.
- first visible manifestation: `/Users/bob/Projects/itlwm/crash.txt` показывает `Kernel trap ... type=14 page fault`, `CR2=0`, backtrace в `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...) + 0x782`.
- expected system behavior: hidden WCL association candidate must not import PSK/PMK bytes from a source that is not present in `apple80211AssocCandidates`; reference programs auth/SSID context and hands the candidate to JoinAdapter.
- actual behavior: local hidden WCL bridge calls legacy `associateSSID(..., key=NULL, key_len=0)`, then legacy PSK branch executes `memcpy(ic->ic_psk, key, sizeof(ic->ic_psk))`.
- divergence point: `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)` reused public `associateSSID(...)` legacy key-import semantics for hidden WCL candidates, although the hidden carrier has no `apple80211_key` field.
- evidence:
  - panic logs: `/Users/bob/Projects/itlwm/crash.txt` has `CR2=0`, `RSI=0`, `RDX=0x20`, `RCX=0x20`, `R8=0x20`, matching a 32-byte memcpy from NULL.
  - runtime logs: user performed one join attempt after reboot; networks were visible before the panic.
  - ioreg: not required for this crash root; loaded binary UUID is identified by panic.
  - packet traces: absent.
  - firmware traces: absent.
  - decomp: `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)` sets BSS auth context/SSID and calls `AppleBCMWLANJoinAdapter::performJoin(...)`; it does not copy PSK material out of the candidate.
  - docs: `docs/tahoe_signal_chain_audit.md` already identifies hidden `0x45/0x3ad8` WCL association as the active owner and says the target is the hidden association carrier, not generic public `associateSSID()` debugging.
- candidate causes:
  - confirmed: hidden WCL candidate path calls the legacy PSK import path with `key=NULL`.
- rejected causes:
  - boot/UI regression: rejected for this symptom; user confirmed networks are visible.
  - old `SET_MAC_ADDRESS` abort: rejected for this symptom; flow reached `setWCL_ASSOCIATE`.
  - generic diagnostic layer boot damage: rejected for this symptom; panic occurs in association after visible scan/UI.
- confirmed deviation: reference WCL association has no candidate PSK import; local code imports PSK from a NULL hidden-candidate key pointer.
- root cause: confirmed for the current kernel panic.
- fix: split Skywalk association PMK ownership explicitly: public `apple80211_assoc_data` keeps importing `ad_key`, while hidden WCL candidates run the same SSID/auth/RSN setup with local PMK import disabled because their carrier has no key field.
- verification: `git diff --check`; Tahoe build; BootKC symbol check; Stage 1 request; after approved runtime, one join attempt should not panic and should expose the next association/auth failure through logs/diagnostics.
- notes: this does not claim full association success; it only removes the confirmed hidden-WCL null PMK import divergence and allows the next blocker to be observed.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-HIDDEN-NULL-PSK-003
- symptom: exact CR-065 runtime panics on the first join attempt after networks are visible.
- expected system behavior: WCL association must not dereference or import a PSK/PMK buffer unless that exact key material is part of the local association carrier; reference WCL path forwards the candidate to JoinAdapter without extracting PSK from it.
- actual behavior: local WCL bridge passes `NULL, 0` as key material to `associateSSID`, and `associateSSID` unconditionally copies 32 bytes from the pointer for PSK AKMs.
- exact divergence point: `AirportItlwmSkywalkInterface::associateSSID(...)` line with `memcpy(ic->ic_psk, key, sizeof(ic->ic_psk))` is reached from `setWCL_ASSOCIATE(...)` with `key == NULL`.
- evidence from runtime: panic registers in `/Users/bob/Projects/itlwm/crash.txt` show `CR2=0`, `RSI=0`, and 32-byte copy length registers while the return address is inside `setWCL_ASSOCIATE + 0x782`; the built kext UUID matches the loaded panic UUID.
- evidence from decomp: `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)` around `docs/reference/AppleBCMWLAN_Core_decompiled.c:116065` sets auth context/SSID and calls `AppleBCMWLANJoinAdapter::performJoin(...)`, with no PSK copy from the candidate payload.
- exact semantic mismatch between reference and our code: reference WCL association does not synthesize a local PMK source from the candidate; local code treated the hidden candidate as if it were public `apple80211_assoc_data` containing `ad_key`.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the faulting registers are the exact argument pattern of `memcpy(destination, NULL, 32)`, and the only local path passing `NULL,0` into the PSK branch is `setWCL_ASSOCIATE(...)`.
- why proposed fix is 1:1 with reference architecture and semantics: it removes the non-reference PSK import from hidden WCL candidates by making PMK import an explicit public-carrier-only path, while leaving auth/SSID/RSN setup and public `setASSOCIATE` key import semantics unchanged.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - force success from `setWCL_ASSOCIATE`: rejected, would mask the remaining association state machine.
  - fabricate zero/default PSK: rejected, guessed secret material and guaranteed wrong handshake.
  - return early on missing hidden-WCL key: rejected, reference still starts JoinAdapter from the WCL candidate and this would hide the next blocker.
  - pass the user password from diagnostics/userspace: rejected, secrets are not part of the hidden candidate contract and would create a new side channel.
  - change broad public `AirportItlwm::associateSSID`: rejected for this batch because the panic is in the Tahoe Skywalk WCL path and non-Skywalk public assoc carries `ad_key`.
- verification plan: `git diff --check`; Tahoe build; BootKC undefined-symbol check; create CR-066 exact-diff Stage 1 request; after approval install/reboot and confirm the same join attempt no longer panics.

## ANOMALY
- id: A-ASSOC-HIDDEN-WCL-EXTERNAL-PMK-004
- status: REJECTED
- symptom: after CR-066 runtime networks are visible and hidden WCL association no longer panics, but manual join to SSID `btn-vno` fails with `airportd` error `-3905`.
- first visible manifestation: `airportd` reports `Failed to associate ... returned error code -3905` after the local driver logs repeated `ieee80211_node_choose_bss reject ssid=btn-vno fail=0x40`.
- expected system behavior: a WCL join request that carries a validated `CIPHER_PMK` owner must be allowed past local PSK-only BSS filtering without importing PMK bytes from a carrier that does not contain them; the Apple supplicant/WCL key owner remains the source of PMK material.
- actual behavior: the hidden WCL path correctly avoids the CR-066 NULL PMK memcpy, but it also leaves `IEEE80211_F_PSK` clear, so local net80211 rejects every PSK-only BSS before association can start.
- divergence point: `AirportItlwmSkywalkInterface::associateSSID(... importLocalPmk=false)` handles PSK AKMs by configuring WPA params but neither imports local PMK bytes nor marks the current association as externally PMK-owned for the local BSS selector.
- evidence:
  - panic logs: no new panic after CR-066; current failure is non-panic association rejection.
  - runtime logs: `WCLJoinRequest: lowerAuth = AUTHTYPE_OPEN, upperAuth = AUTHTYPE_SHA256_PSK, key = CIPHER_PMK, Valid Private Mac Addr`.
  - runtime logs: `setWCL_ASSOCIATE [btn-vno] ... auth_upper=1024 ...`, then `associateSSID ... key_len=0 ...`, then `WCL candidate has no local PMK source key_len=0`.
  - runtime logs: `ieee80211_node_choose_bss reject ssid=btn-vno fail=0x40 des_esslen=7 auto_join=1`, followed by `airportd ... error code -3905`.
  - decomp: `IO80211Family` stores the WCL key separately from assoc candidates: `WCLJoinRequest::checkValidationForApple80211Key(...)` returns the pointer at request private offset `+0x18`, while the `0x45/0x3ad8` carrier sent via `sendIOUCToWcl(..., 0x45, payload, 0x3ad8, ...)` is the assoc-candidates payload.
  - decomp: `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)` programs auth/SSID context and delegates to `AppleBCMWLANJoinAdapter::performJoin(...)`; it does not copy PMK bytes from `apple80211AssocCandidates`.
  - local source: `ieee80211_node_choose_bss` sets `IEEE80211_NODE_ASSOCFAIL_WPA_PROTO` (`0x40`) when AP AKMs are PSK-only and `IEEE80211_F_PSK` is clear.
  - local source: with `USE_APPLE_SUPPLICANT`, EAPOL input is forwarded to Apple user space and PTK/GTK installation remains in `setCIPHER_KEY(...)`; local PMK bytes are not the WCL hidden-candidate source.
- candidate causes:
  - confirmed: hidden WCL external-PMK ownership is not represented in the local net80211 PSK capability gate.
- rejected causes:
  - wrong scan visibility: rejected; scan cache lists two `btn-vno` BSS entries and UI shows networks.
  - CR-066 panic: rejected; no new trap, and `setWCL_ASSOCIATE` returns success before the BSS rejection.
  - missing local `setCIPHER_KEY(CIPHER_PMK)` call before association: rejected for this runtime blocker; logs from boot/join window contain `WCLJoinRequest key=CIPHER_PMK` but no local `setCIPHER_KEY` call, and reference keeps the WCL key outside the assoc-candidates carrier.
  - copying zero/default/user-provided PMK in the driver: rejected, the hidden carrier does not contain PMK bytes and reference does not synthesize them there.
- confirmed deviation: reference WCL has an out-of-band key owner for the join request, while the local net80211 compatibility layer treats lack of local PMK bytes as lack of PSK capability and rejects the BSS.
- root cause: confirmed for the current `fail=0x40` / `-3905` association stop.
- fix: keep CR-066's no-copy rule for hidden WCL PMK bytes, but explicitly mark the current PSK association as externally PMK-owned for the local BSS selector by setting `IEEE80211_F_PSK` when the hidden WCL path reaches a PSK AKM with Apple-supplicant ownership.
- verification: `git diff --check`; Tahoe build; BootKC symbol check; Stage 1 request; after approval install/reboot and verify join no longer fails at `ieee80211_node_choose_bss fail=0x40`; subsequent EAPOL/key/data failures, if any, must be captured as later anomalies.
- notes: this fix does not claim final data path success. It only removes the confirmed local pre-association PSK gate that blocks a WCL request already carrying `CIPHER_PMK`.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-HIDDEN-WCL-EXTERNAL-PMK-004
- symptom: after CR-066, networks remain visible and the hidden WCL association call returns success, but `btn-vno` join fails before association because all PSK-only BSS candidates are rejected with `fail=0x40`.
- expected system behavior: WCL/Apple-supplicant PMK ownership should satisfy the local pre-association PSK capability gate without requiring PMK bytes to be present inside `apple80211AssocCandidates`.
- actual behavior: hidden WCL calls `associateSSID(..., key=NULL, key_len=0, importLocalPmk=false)` and therefore leaves `IEEE80211_F_PSK` clear; local net80211 rejects PSK-only APs before association.
- exact divergence point: `AirportItlwmSkywalkInterface::associateSSID(...)` PSK branch distinguishes public local PMK import from hidden no-import, but lacks a third state for validated external WCL PMK ownership.
- evidence from runtime: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-067-before-current-failed-join-20260423-172930.log` shows WCL `key = CIPHER_PMK`, local `associateSSID key_len=0`, then `ieee80211_node_choose_bss reject ssid=btn-vno fail=0x40` and `airportd` `-3905`.
- evidence from decomp: `IO80211Family_decompiled.c` shows `WCLJoinRequest::checkValidationForApple80211Key(...)` returning the request key pointer at private offset `+0x18`; `sendIOUCToWcl(..., 0x45, payload, 0x3ad8, ...)` sends only the assoc-candidates payload; `AppleBCMWLANCore::setWCL_ASSOCIATE(...)` delegates that payload to JoinAdapter without candidate PMK copy.
- exact semantic mismatch between reference and our code: reference separates WCL key ownership from the assoc-candidates carrier, while local net80211 currently equates "no candidate PMK bytes" with "no PSK capability" and aborts BSS selection.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: hidden `setWCL_ASSOCIATE(...)` return semantics; local `ieee80211_node_choose_bss(...)` PSK capability gate; WCL/Apple-supplicant EAPOL ownership; `setCIPHER_KEY(...)` PTK/GTK installation path; absence of PMK bytes in `apple80211AssocCandidates`.
  - expected contract at each touchpoint: hidden associate keeps returning success after configuring auth/SSID/RSN; BSS selector must not reject PSK-only AP when WCL has a validated PMK owner; EAPOL remains forwarded to Apple user space under `USE_APPLE_SUPPLICANT`; PTK/GTK keys are installed only through existing `setCIPHER_KEY(...)`; driver must not fabricate or copy PMK bytes from the hidden carrier.
  - why no relevant touchpoints are missing: the current failure occurs before firmware association and before EAPOL; the runtime contains no local `setCIPHER_KEY` call, no PTK/GTK traffic, and no data-path event before `fail=0x40`.
  - why proposed path adds no extra system-visible side effects: it changes only the internal net80211 PSK capability flag for the current hidden WCL PSK association after `ieee80211_disable_rsn(...)` cleared prior state; it does not alter return codes, callbacks, ordering, notifications, BSSID/SSID payloads, PMK bytes, or key-install paths.
- why this is root cause and not just correlation: `fail=0x40` maps directly to `IEEE80211_NODE_ASSOCFAIL_WPA_PROTO`, and the exact local branch sets it for PSK-only AKMs when `IEEE80211_F_PSK` is clear; the runtime proves `btn-vno` reaches that branch after WCL `CIPHER_PMK`.
- why proposed fix is 1:1 with reference architecture and semantics: it preserves reference separation of WCL key ownership from assoc-candidates PMK bytes, while providing the minimal local compatibility representation needed by our net80211 BSS selector.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - restore hidden WCL `memcpy(ic_psk, NULL, 32)`: rejected, CR-066 proved it panics and reference does not copy candidate PMK bytes.
  - fabricate zero/default PMK bytes: rejected, guessed secret material and may corrupt handshake.
  - derive PMK from user password in the driver or diagnostics utility: rejected, password is not part of the WCL driver contract and would add a new secret side channel.
  - force success from `ieee80211_node_choose_bss` or mask `fail=0x40`: rejected, would bypass the selector rather than representing WCL PMK ownership.
  - add retry/replay/delay: rejected, the failure is deterministic local state, not timing.
- verification plan: run `git diff --check`; build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`; create exact-diff Stage 1 request; after approval install without unload, reboot, and verify `btn-vno` no longer produces `ieee80211_node_choose_bss fail=0x40`.

## ANOMALY
- id: A-TX-LEGACY-NETSTAT-NULL-005
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-067 runtime networks are visible, but the first manual join attempt panics during the association management-frame TX path.
- first visible manifestation: `/Users/bob/Projects/itlwm/crash.txt` reports `Kernel trap ... type=14 page fault`, `CR2=0x8`, with the top frame `ItlIwm::_iwm_start_task(...) + 0x2c4`.
- expected system behavior: Tahoe/Skywalk association management-frame TX must not depend on the legacy `IONetworkInterface::configureInterface()` stats buffer, because the active interface is registered through `IOSkywalkNetworkBSDClient` after `deferBSDAttach(false)`.
- actual behavior: the iwm TX path sends the management frame successfully and then unconditionally increments `ifp->netStat->outputPackets`; `ifp->netStat` is NULL on the Tahoe Skywalk path, causing a page fault at offset `0x8`.
- divergence point: `AirportItlwm::start(...)` wires the internal `_ifnet` into the Tahoe Skywalk path but does not provide legacy `IONetworkStats` storage for the OpenBSD compatibility layer; `ItlIwm::_iwm_start_task(...)` still assumes that storage exists.
- evidence:
  - panic logs: `/Users/bob/Projects/itlwm/crash.txt` shows `CR2=0x8`, kext UUID `D2919320-0693-3B27-8694-0DC35969F679`, and top frame `_iwm_start_task`.
  - panic logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-067-after-crash-symbolication-20260423-1805.txt` symbolicates the fault to `itlwm/hal_iwm/mac80211.cpp:3596`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-067-after-crash-kernel-airportd-20260423-1758.log` shows Tahoe Skywalk registration (`registerEthernetInterface=0x0`, `deferBSDAttach(false)`) and no `configureInterface`/`network statistics buffer` log before the join crash.
  - local source: `mac80211.cpp:3596` is `ifp->netStat->outputPackets++`; `IONetworkStats.outputPackets` offset is `0x8`, matching the panic `CR2`.
  - local source: `AirportItlwmV2.cpp:2698..2719` only assigns `ifp->netStat` inside legacy `configureInterface(IONetworkInterface *)`, while the Tahoe start path comments state that BSD ifnet creation is handled by `IOSkywalkNetworkBSDClient` after `deferBSDAttach(false)`.
  - decomp: reference Tahoe path uses `IOSkywalkEthernetInterface::registerEthernetInterface`/BSDClient attach and separate IO80211/Skywalk data-path stats virtuals; it does not expose our legacy OpenBSD `_ifnet::netStat` pointer as a required TX contract.
  - docs: `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/85_bsd_attach_chain_xref_checked.yaml` records that reporters/stats are lazy and not required during start.
- candidate causes:
  - confirmed: legacy OpenBSD compatibility counters dereference a NULL `ifp->netStat` on the Tahoe Skywalk path.
- rejected causes:
  - CR-067 hidden external PMK flag: rejected for this panic; the crash occurs later, after management TX reaches `_iwm_start_task`.
  - `ieee80211_node_choose_bss fail=0x40`: rejected for this runtime; the panic stack is TX/newstate, not BSS selection.
  - NULL mbuf/node in `iwm_tx`: rejected for this panic; the faulting line is after `iwm_tx(...) == 0`.
  - UI/scan regression: rejected; user confirmed networks are visible and runtime logs contain `btn-vno` scan entries.
- confirmed deviation: the Tahoe Skywalk start path has no legacy `configureInterface` stats assignment, while the reused OpenBSD iwm TX path treats `ifp->netStat` as mandatory.
- root cause: confirmed for the CR-067 join panic because the panic address, symbolicated line, and struct offset exactly match `NULL->outputPackets`.
- fix: provide a Tahoe-only driver-owned `IONetworkStats` backing store for the legacy OpenBSD compatibility `_ifnet` during `AirportItlwm::start(...)`; keep `configureInterface(...)` able to replace it with a real `IONetworkStats` buffer if that legacy path ever runs.
- verification: `git diff --check`; Tahoe build; BootKC undefined-symbol check; Stage 1 request; after approval install without unloading, reboot, and verify one join attempt no longer panics at `_iwm_start_task:3596`.

## FIX_CANDIDATE

- anomaly_id: A-TX-LEGACY-NETSTAT-NULL-005
- symptom: CR-067 runtime panics on the first join attempt after networks are visible.
- expected system behavior: Tahoe/Skywalk management-frame TX can use the OpenBSD compatibility `_ifnet`, but legacy packet/error counters must have valid storage or be absent from the TX contract; they must not be able to fault the association state machine.
- actual behavior: `ItlIwm::_iwm_start_task(...)` sends the management frame and then increments `ifp->netStat->outputPackets` while `ifp->netStat == NULL`.
- exact divergence point: `AirportItlwm::start(...)` initializes the Tahoe Skywalk interface via `registerEthernetInterface(...)`/`deferBSDAttach(false)` but leaves `fHalService->get80211Controller()->ic_ac.ac_if.netStat` unset because legacy `configureInterface(IONetworkInterface *)` is not invoked in this path.
- evidence from runtime: `/Users/bob/Projects/itlwm/crash.txt` has `CR2=0x8` and `_iwm_start_task + 0x2c4`; `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-067-after-crash-symbolication-20260423-1805.txt` maps the address to `mac80211.cpp:3596`; CR-067 boot logs show Skywalk registration and no `configureInterface` log.
- evidence from decomp: Tahoe reference registration is the Skywalk/BSDClient path (`registerEthernetInterface` and later BSD ifnet creation), with IO80211/Skywalk stats exposed through dedicated data-path stats/reporting methods rather than our legacy `_ifnet::netStat` pointer.
- exact semantic mismatch between reference and our code: reference Skywalk TX does not require a legacy IOEthernetInterface stats buffer to exist before association TX; local compatibility code makes that buffer a hard requirement even though Tahoe does not create it through the legacy path.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: Tahoe Skywalk interface registration; OpenBSD compatibility `_ifnet` used by iwm/net80211; management-frame TX return semantics; legacy packet/error counters; optional legacy `configureInterface(...)` stats replacement.
  - expected contract at each touchpoint: Skywalk registration and UI visibility remain unchanged; `_ifnet` has valid counter storage before any iwm/net80211 TX/RX counter touch; management-frame TX return/order/payload are unchanged; counters are local bookkeeping only; `configureInterface(...)` may still replace the fallback with a real OS-provided stats buffer if invoked.
  - why no relevant touchpoints are missing: the panic occurs after `iwm_tx(...)` succeeds and before any external association result callback, EAPOL, or data-path key install; the only faulting state is the legacy counter pointer.
  - why proposed path adds no extra system-visible side effects: it assigns a zeroed fallback `IONetworkStats` struct to the existing local `_ifnet::netStat` pointer and logs the pointer once; it does not change IOCTL routing, association payloads, TX queueing, return codes, notifications, timing gates, key handling, or Skywalk registration.
- why this is root cause and not just correlation: `IONetworkStats.outputPackets` offset is exactly `0x8`, matching panic `CR2=0x8`, and the symbolicated fault line is the unguarded `outputPackets++` immediately after successful management-frame TX.
- why proposed fix is 1:1 with reference architecture and semantics: it preserves the Tahoe Skywalk producer path and treats legacy OpenBSD counters as local compatibility storage rather than as a reference-visible association contract.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - guard only `mac80211.cpp:3596`: rejected, would leave the same NULL stats pointer for neighboring TX/RX/error counter paths.
  - remove all netStat increments: rejected, broader behavior change to shared legacy code and unnecessary for the Tahoe compatibility gap.
  - force legacy `configureInterface(...)` or recreate `IOEthernetInterface`: rejected, contradicts the established Tahoe Skywalk/BSDClient path and previously caused UI/boot regressions.
  - suppress management TX or force association success: rejected, would mask the real join path.
  - add retry/delay/replay: rejected, the panic is a deterministic NULL pointer, not timing.
- verification plan: run `git diff --check`; build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`; create CR-068 exact-diff Stage 1 request; after approval install without unloading, reboot, and verify the same `btn-vno` join attempt no longer panics in `_iwm_start_task`.

## ANOMALY
- id: A-ASSOC-CURRENTLINK-CACHE-006
- status: FIX_IMPLEMENTED
- symptom: after CR-068 runtime networks are visible and manual join to `btn-vno` no longer panics, but WCL still times out and airportd reports `-3905`.
- first visible manifestation: after local `ASSOC -> RUN` and `associated with 00:58:28:26:1c:1a`, external `GET SSID`, `GET BSSID`, and `GET CURRENT_NETWORK` continue returning `0xe0822403`; later auto-join reports `driver not available`.
- expected system behavior: on real Tahoe link edges the Skywalk interface current-AP cache is cleared when not associated and populated with the current AP BSSID when associated, so WCL/CoreWiFi current-link probes observe the same state as the link transition.
- actual behavior: local code updates link state and posts BSSID/SSID/link notifications, but never drives `AirportItlwmSkywalkInterface::setCurrentApAddress(...)`; the public request gate also still admits disproven `SSID/BSSID/CHANNEL/CURRENT_NETWORK/ROAM_PROFILE` request numbers and leaks raw `1` for channel/roam-profile probes.
- divergence point: `AirportItlwm::setLinkStateGated(...)` has the only confirmed local RUN/link-down edge but does not call the already recovered `setCurrentApAddress(NULL/BSSID)` producer before publishing the link transition.
- evidence:
  - panic logs: no panic in CR-068 runtime; previous `_iwm_start_task` crash is gone.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-068-after-join-filtered-20260423-182800.log` shows `ASSOC -> RUN`, `associated with 00:58:28:26:1c:1a`, `setLinkStatus status=0x3`, and `setLinkStateGated`.
  - runtime logs: the same log has repeated `APPLE80211_IOC_SSID -> 0xe0822403`, `APPLE80211_IOC_BSSID -> 0xe0822403`, `APPLE80211_IOC_CURRENT_NETWORK -> 0xe0822403`, and no `setCurrentApAddress` line.
  - runtime logs: `APPLE80211_IOC_CHANNEL` and `APPLE80211_IOC_ROAM_PROFILE` return raw `1`, matching the previously disproven public request-number gate behavior.
  - runtime logs: WCL aborts after timeout with `sendWCLJoinDone lastStatusCode=1009 extendedCode=1007 joinedBSSID=00:58:28:26:1C:1A`, then airportd reports `-3905`.
  - ioreg: not yet collected after the proposed fix; current loaded UUID is `E0326944-5F74-3760-AB97-236EB40378F4`.
  - packet traces: absent.
  - firmware traces: absent.
  - decomp: `AppleBCMWLANSkywalkInterface::setCurrentApAddress(ether_addr*)` calls `IO80211InfraInterface::setCurrentApAddress(NULL)` and clears validity for NULL, or calls the base method with a real AP address and sets validity for non-NULL.
  - decomp: `IO80211Family_decompiled.c` slot `+0xcc8` fallback helpers for public request numbers return on non-zero and abort/route incorrectly on zero; live CR-068 proves widening public request numbers to this gate is not a payload-producing current-link owner.
  - decomp: `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` while its BSS manager is not associated, so the missing current-link producer explains the persistent low-level not-associated status after local RUN.
  - docs: `docs/tahoe_discrepancy_inventory.md` and `docs/tahoe_signal_chain_audit.md` record the latest correction: remove public request numbers from slot `[411]`, keep hidden assoc carriers, and drive `setCurrentApAddress(nullptr / real BSSID)` on actual link edges.
- candidate causes:
  - confirmed: current-link cache producer is present as an override but never invoked on local link edges.
  - confirmed: public request-number admission in `isCommandProhibited(...)` is a disproven routing theory that leaks raw `1` and does not populate SSID/BSSID/CURRENT_NETWORK.
- rejected causes:
  - CR-067 `fail=0x40`: rejected for this runtime; `btn-vno` reaches AUTH/ASSOC/RUN.
  - CR-067 `netStat` panic: rejected for this runtime; no panic and management TX completes.
  - generic missing scan visibility: rejected; scan cache contains both `btn-vno` BSSIDs and UI lists networks.
  - replaying/duplicating BSSID/SSID notifications: rejected; no producer-side evidence that reference replays notifications after link-up.
- confirmed deviation: Apple has an explicit Skywalk current-AP producer with NULL/non-NULL validity semantics; local Tahoe link-edge code omits that producer and relies only on link notifications plus stale public request gating.
- root cause: confirmed for the post-RUN current-link invisibility that causes WCL timeout/airportd `driver not available`; full EAPOL/data success remains outside this claim.
- fix: implemented: on every Tahoe `setLinkStateGated(...)` edge call `AirportItlwmSkywalkInterface::setCurrentApAddress(real BSSID)` for link-up with `ic_state == IEEE80211_S_RUN` and a valid `ic_bss`, otherwise call `setCurrentApAddress(nullptr)`; narrow `AirportItlwmSkywalkInterface::isCommandProhibited(...)` back to hidden assoc carriers only.
- verification: `git diff --check` passed; Tahoe build passed; BootKC symbol check passed with `OK: all 856 undefined symbols resolve against BootKC`; after approved install/reboot, one `btn-vno` attempt must show `setCurrentApAddress addr=00:58:28:26:1c:1a` before/at link-up, no raw `CHANNEL/ROAM_PROFILE -> 1` from the old public gate, and current-link probes must no longer keep returning `0xe0822403` after RUN.
- notes: this does not force join success; if EAPOL/key or data-path issues remain, they must appear after current-link cache becomes visible.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-CURRENTLINK-CACHE-006
- symptom: CR-068 reaches local RUN for `btn-vno`, but WCL times out because Apple-visible current SSID/BSSID/current-network remain unavailable.
- expected system behavior: link-up with an associated BSS must publish the current AP address through the Skywalk current-link cache producer; link-down/non-associated state must clear it with NULL.
- actual behavior: local `setLinkStateGated(...)` calls `setLinkState`, posts BSSID/SSID/link notifications, and reports link status, but never calls `setCurrentApAddress(...)`; public request selectors remain in slot `[411]` despite prior runtime showing they are not the payload owner.
- exact divergence point: Tahoe branch of `AirportItlwm::setLinkStateGated(...)` and `AirportItlwmSkywalkInterface::isCommandProhibited(int)`.
- evidence from runtime: CR-068 log shows `ASSOC -> RUN`, `associated with 00:58:28:26:1c:1a`, repeated `SSID/BSSID/CURRENT_NETWORK -> 0xe0822403`, raw `CHANNEL/ROAM_PROFILE -> 1`, no `setCurrentApAddress`, and final WCL timeout `1009/1007`.
- evidence from decomp: `AppleBCMWLANSkywalkInterface::setCurrentApAddress(ether_addr*)` has exact NULL clear and non-NULL valid-current-AP semantics; `IO80211Family` public request-number helpers consult slot `+0xcc8`, while `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` until its BSS/current-link manager is associated.
- exact semantic mismatch between reference and our code: reference has a current-AP cache producer separate from generic link-state notification; local code publishes link-state notifications without updating that cache and keeps a disproven request gate as if it were the producer.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: Tahoe Skywalk current-AP cache; link-state edge; BSSID/SSID/link notifications; public request gate slot `[411]`; WCL/airportd current SSID/BSSID/current-network probes.
  - expected contract at each touchpoint: current-AP cache is NULL when not associated and real BSSID when associated; link-state ordering remains one edge per net80211 transition; notifications remain the existing single publish; public request gate only admits proven hidden assoc carriers; WCL/airportd probes must observe current-link cache rather than raw not-associated status after RUN.
  - why no relevant touchpoints are missing: the failure appears after local association and before EAPOL/key install; scan, hidden assoc, management TX, and link notification paths already execute, leaving current-link cache as the remaining system-visible state mismatch in this claim.
  - why proposed path adds no extra system-visible side effects: it does not force success, retry, delay, duplicate notify, alter packets, or change key ownership; it only invokes the recovered Apple cache producer once on the same link edge that already publishes link/BSSID/SSID state, and removes the disproven public selector gate.
- why this is root cause and not just correlation: the runtime shows local RUN while every Apple-visible current-link query still reports not associated; the only recovered Apple producer for that cache is present but unused locally; the old public gate is directly evidenced by raw `1` returns and cannot produce current-link payloads.
- why proposed fix is 1:1 with reference architecture and semantics: it uses the reference-named Skywalk interface producer with the exact NULL/non-NULL semantics recovered from decomp and ties it to actual local link edges rather than guessed boot seeding or duplicate event replay.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - zero-BSSID `setCurrentApAddress(...)` seeding during boot: rejected, previously marked guessed; reference distinguishes NULL clear from non-NULL valid AP.
  - force success from `getSSID/getBSSID/getCURRENT_NETWORK`: rejected, would mask the missing cache producer and may fabricate current state.
  - replay/duplicate BSSID/SSID/link notifications after RUN: rejected, no producer-side reference evidence.
  - keep public `SSID/BSSID/CHANNEL/CURRENT_NETWORK/ROAM_PROFILE` in slot `[411]`: rejected by CR-068 raw `1` and persistent `0xe0822403` runtime evidence.
  - add delay/retry/poll: rejected; current-link state is missing deterministically after RUN.
- verification plan: `git diff --check`; Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`; create exact-diff Stage 1 request; after approval install without unloading, reboot, perform one `btn-vno` join attempt, and capture sudo logs for `setCurrentApAddress`, current-link probe return codes, WCL timeout/success, `setCIPHER_KEY`, and data-path counters.

## ANOMALY
- id: A-ASSOC-CURRENTAP-RUN-NONEDGE-007
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-069 загружается без UI regression, сети видны, join к `btn-vno` доходит до локального `ASSOC -> RUN`, но Apple-visible current SSID/BSSID/current-network остаются недоступны и WCL завершает join timeout.
- first visible manifestation: после `ASSOC -> RUN` в runtime нет `setCurrentApAddress(addr=<BSSID>)`, а `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK` продолжают возвращать `0xe0822403`.
- expected system behavior: когда net80211 входит в `IEEE80211_S_RUN` с выбранным `ic_bss`, Skywalk current-AP cache должен быть синхронизирован с реальным BSSID; когда association отсутствует, cache должен быть очищен через NULL.
- actual behavior: CR-069 вызывает `setCurrentApAddress(NULL)` только на раннем driver-ready link-up edge (`ic_state=0`), а реальный `RUN` приходит как `setLinkStatus(status=0x3, prev=0x3)` и выходит через ранний `return` без current-AP публикации.
- divergence point: `AirportItlwm::setLinkStatus(...)` считает `status == currentStatus` no-op и возвращает до Tahoe current-AP producer path; `AirportItlwm::setLinkStateGated(...)` не вызывается на реальном `RUN`, потому что active status уже был установлен ранним low-latency/driver-ready edge.
- evidence:
  - panic logs: нет; текущая сборка не паникует и не ломает UI visibility.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-069-after-join-filtered-20260423-1911.log`, sha256 `8810f22ffbb7e3f7a8a21b29898c14ad1df431a4e6658e57413d0df5da673aeb`.
  - runtime logs: `19:01:05.720057 setLinkStateGated linkState=2`, затем `setCurrentApAddress addr=(null) ic_state=0`.
  - runtime logs: `19:05:25.885889 ASSOC -> RUN` и `associated with 00:58:28:26:1c:1a ssid`.
  - runtime logs: immediately after RUN, `19:05:25.885960 setLinkStatus status=0x3 (prev=0x3) active=1 ... ic_state=4`; there is no `setLinkStateGated linkState=2` and no `setCurrentApAddress addr=00:58:28:26:1c:1a`.
  - runtime logs: after RUN and deauth window, current-link probes still return `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK -> 0xe0822403`, WCL reports `sendWCLJoinDone lastStatusCode=1009 extendedCode=1007`, and auto-join reports `driver not available`.
  - runtime logs: `SET_MAC_ADDRESS`, hidden `setWCL_ASSOCIATE`, external PMK marker, BSS selection, AUTH, ASSOC, and RUN all execute, so the current failure is later than CR-064..CR-068 blockers.
  - decomp: `AppleBCMWLANSkywalkInterface::setCurrentApAddress(ether_addr*)` has exact NULL clear and non-NULL valid-current-AP semantics.
  - decomp: `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` while the BSS/current-link manager is not associated.
  - decomp/docs: `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)` calls base enable, `reportLinkStatus(3,0x80)`, then `setLinkState(kIO80211NetworkLinkUp,1,false,0,0)`, proving the early link-up edge can be a driver-ready edge and is not identical to association `RUN`.
- candidate causes:
  - confirmed: CR-069 tied the current-AP producer only to `setLinkStateGated(...)`, but real association `RUN` is a same-link-status update and bypasses that path.
- rejected causes:
  - missing `SET_MAC_ADDRESS`: rejected for this runtime; local set-mac handler logs success before WCL associate.
  - hidden WCL null PMK panic: rejected; no panic and WCL path logs external PMK ownership.
  - local PSK BSS gate `fail=0x40`: rejected; BSS is selected and the state machine reaches AUTH/ASSOC/RUN.
  - legacy `netStat` NULL panic: rejected; management TX completes and no panic occurs.
  - public request-number gate raw `CHANNEL/ROAM_PROFILE -> 1`: rejected for CR-069 current logs; channel requests return success and public selector gate was already narrowed.
  - replaying link-state or BSSID/SSID notifications: rejected; no producer-side evidence and the missing state is current-AP cache ownership, not duplicate notifications.
- confirmed deviation: the local Tahoe current-AP cache producer is not synchronized to actual association `RUN`; it is synchronized only to an earlier driver-ready link-state edge that can occur with `ic_state=0`.
- root cause: confirmed for the remaining Apple-visible current-link unavailability after local `RUN`; full EAPOL/key/data success remains outside this claim and must be evaluated after current-AP cache is visible.
- fix: synchronize Tahoe current-AP cache from the actual net80211 association state as well as link-down edges: publish real BSSID exactly when `ic_state == IEEE80211_S_RUN && ic_bss != nullptr` and the cached current AP is absent/stale; publish NULL when association is cleared; do not replay link-state or notification events.
- verification: `git diff --check`; Tahoe build; BootKC symbol check; Stage 1 request; after approval install without unloading, reboot, perform one `btn-vno` join attempt, and verify the log contains `setCurrentApAddress addr=00:58:28:26:1c:1a` at/after `ASSOC -> RUN` before WCL completion.
- notes: this is a correction to the CR-069 placement of the current-AP producer, not a new diagnostic layer and not a forced association success.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-CURRENTAP-RUN-NONEDGE-007
- symptom: CR-069 reaches `ASSOC -> RUN` for `btn-vno`, but WCL/CoreWiFi still see no current SSID/BSSID/current-network and the join times out.
- expected system behavior: the Skywalk current-AP cache reflects the real associated AP when net80211 is in `IEEE80211_S_RUN`, and is invalid/NULL outside association.
- actual behavior: CR-069 publishes NULL during early driver-ready link-up (`ic_state=0`) and never publishes the real BSSID at `RUN` because `status == currentStatus` causes an early return.
- exact divergence point: `AirportItlwm::setLinkStatus(...)` line with `if (status == currentStatus) return true;` runs at `ic_state=IEEE80211_S_RUN`; `AirportItlwm::setLinkStateGated(...)` remains the only current-AP producer but is not called on this non-edge.
- evidence from runtime: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-069-after-join-filtered-20260423-1911.log` shows early `setCurrentApAddress addr=(null) ic_state=0`, later `ASSOC -> RUN`, `associated with 00:58:28:26:1c:1a`, `setLinkStatus status=0x3 (prev=0x3) ic_state=4`, no real-BSSID `setCurrentApAddress`, continued `SSID/BSSID/CURRENT_NETWORK -> 0xe0822403`, and WCL `1009/1007`.
- evidence from decomp: `AppleBCMWLANSkywalkInterface::setCurrentApAddress(ether_addr*)` distinguishes NULL clear from non-NULL valid current AP; `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` when its BSS/current-link manager is not associated; `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)` emits an early link-up independent of association.
- exact semantic mismatch between reference and our code: reference exposes a current-AP cache producer whose state must track the BSS/current-link manager, while CR-069 wired that producer only to a generic link-active edge that can precede association and then suppresses the real RUN update as a status no-op.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: Tahoe Skywalk current-AP cache; net80211 `IEEE80211_S_RUN`/`ic_bss`; link-down/deauth clearing; existing link-state notification path; WCL/CoreWiFi `SSID/BSSID/CURRENT_NETWORK` probes.
  - expected contract at each touchpoint: current-AP cache is real BSSID only when `ic_state == RUN` and `ic_bss` is present; current-AP cache is NULL when association is not valid; link-state and BSSID/SSID notifications are not replayed; public getters observe the cache through existing IO80211 paths; WCL/CoreWiFi no longer see stale not-associated current-link state after local RUN.
  - why no relevant touchpoints are missing: current runtime already passes set-mac, hidden WCL associate, PMK ownership, BSS selection, management TX, AUTH, ASSOC, and RUN; no EAPOL/key/data path begins before WCL times out, and every remaining Apple-visible failure is current-link state.
  - why proposed path adds no extra system-visible side effects: it invokes only the recovered current-AP cache producer with exact NULL/BSSID payloads, tracks the last published value to avoid duplicate publishes, and does not change return codes, packets, key material, retries, ordering, link-state calls, notifications, or data-path behavior.
- why this is root cause and not just correlation: the log proves `RUN` occurs while current-link probes continue returning the exact reference not-associated code; the only local reason the recovered producer is not invoked is the same-status early return, and the decomp shows that current-link queries depend on associated current-BSS state.
- why proposed fix is 1:1 with reference architecture and semantics: it keeps the Apple current-AP producer as the sole owner and synchronizes it to the actual association lifecycle, while preserving the earlier low-latency link-up edge as driver-ready/link state rather than treating it as association.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - call `setLinkStateGated(kIO80211NetworkLinkUp)` again at RUN: rejected as replay/duplicate link-state without producer-side reference evidence.
  - repost `APPLE80211_M_BSSID_CHANGED` / `APPLE80211_M_SSID_CHANGED`: rejected as duplicate notify; current missing owner is cache state, not notification delivery.
  - force success or fabricate payloads in `getSSID/getBSSID/getCURRENT_NETWORK`: rejected, would mask cache ownership and can publish stale state.
  - add delay/retry/poll loop around WCL timeout: rejected, failure is deterministic missing state publication.
  - alter PMK/EAPOL/key handling in this patch: rejected, current runtime has not reached a verified EAPOL/key blocker after visible current-AP cache.
- verification plan: `git diff --check`; Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`; BootKC undefined-symbol check; create exact-diff Stage 1 request; after approval install without unloading, reboot, perform one `btn-vno` join attempt, and collect sudo logs for current-AP publish, current-link getter return codes, WCL completion, EAPOL, key install, and data counters.

## ANOMALY
- id: A-ASSOC-WCL-CURRENTBSS-LINKUP-008
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-070 publishes the real current AP BSSID at local `ASSOC -> RUN`, but WCL/CoreWiFi current SSID/BSSID/current-network remain unavailable and the join still times out.
- first visible manifestation: after `setCurrentApAddress addr=00:58:28:26:1c:1a ic_state=4`, `APPLE80211_IOC_SSID` still returns `-528342013/0xe0822403` while local `ic_state` is `IEEE80211_S_RUN`.
- expected system behavior: the association link-up producer must make the IO80211/WCL current-BSS manager associated with the selected BSS before current-link probes are expected to succeed.
- actual behavior: CR-070 updates only the Skywalk current-AP address cache on the same-status RUN non-edge; it does not drive an association link-state producer after the real BSSID is available, so IO80211/WCL current-BSS state remains not-associated.
- divergence point: `AirportItlwm::setLinkStatus(...)` same-status active path calls `syncTahoeCurrentApAddress(false, false)` and returns; no association-edge link-state update is sent after current AP becomes non-NULL.
- evidence:
  - panic logs: none in CR-070 runtime; no boot hang and no UI regression.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-070-after-join-filtered-20260423-2001.log`, sha256 `d56a2acb731f1fbab7e44dc550aa9d7a45c5a644b09f1f0ff25fe80e96398e5e`.
  - runtime logs: `20:00:03.763438 ASSOC -> RUN`, `20:00:03.763447 associated with 00:58:28:26:1c:1a ssid`.
  - runtime logs: `20:00:03.763476 syncTahoeCurrentApAddress addr=00:58:28:26:1c:1a ic_state=4` and `20:00:03.763481 setCurrentApAddress addr=00:58:28:26:1c:1a ic_state=4`.
  - runtime logs: `20:00:05.573422 APPLE80211_IOC_SSID return -528342013/0xe0822403`, before AP deauth at `20:00:07.765387`.
  - runtime logs: `GET OP MODE` and `GET POWER` reach local vtable/getter paths immediately before the failing current-link probe, proving the interface is alive and the failure is specific to the current-link/BSS-associated cache.
  - runtime logs: local `getSSID`, `getBSSID`, and `getCURRENT_NETWORK` logs are absent for the failing public probes, so this is not a local helper return-value bug.
  - runtime logs: early driver-ready link-up at `19:57:26.591696` calls `IO80211InfraInterface::setLinkState` with current AP still NULL, causing IO80211Family `getBSSIDData()` failure; the real RUN later has current AP valid but no association link-state update.
  - ioreg: loaded CR-070 UUID `9CE6341E-662A-32FE-B6DC-CD5083B33974`; loaded-state evidence sha256 `7666558b4bc9d64286006563d7f82a44fdcb07be309e66697c49e9f959c74be6`.
  - packet traces: absent.
  - firmware traces: absent.
  - decomp: `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` when `IO80211BssManager::isAssociated(...)` is false.
  - decomp: `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state*)` on link-up calls `AppleBCMWLANBssManager::setCurrentBSS(...)`, updates MCS/rate state, and resets link-quality state; on link-down it clears current BSS with `setCurrentBSS(..., 0)`.
  - decomp: `WCLBssManager::setCurrentBSS(...)` writes the current BSS pointer and updates WCL current-BSS state.
  - decomp: `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)` has a separate early driver-ready `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)` path, proving that the early link-up edge is not the same producer as association/WCL link-state update.
  - docs: CR-070 intentionally avoided link-state replay because there was not yet producer-side evidence. The CR-070 runtime plus `setWCL_LINK_STATE_UPDATE` decomp now identify the missing association current-BSS producer.
- candidate causes:
  - confirmed: current AP address publication alone does not update IO80211/WCL current-BSS associated state; reference link-up producer also updates the current-BSS manager.
  - confirmed: local actual `RUN` remains a same-status active update after the early driver-ready link-up, so the association link-state producer is skipped.
- rejected causes:
  - missing current AP BSSID: rejected by CR-070 runtime; `setCurrentApAddress` publishes `00:58:28:26:1c:1a`.
  - public getter helper returning error: rejected; local `getSSID/getBSSID/getCURRENT_NETWORK` logs are absent for the failing public probes.
  - interface not loaded or UI regression: rejected; networks are visible and OP_MODE/POWER getters succeed.
  - `SET_MAC_ADDRESS`/hidden associate/BSS selection failure: rejected; set-mac, WCL associate, BSS selection, AUTH, ASSOC, and RUN all execute before the blocker.
  - EAPOL/key install as the first blocker: not yet confirmed; WCL current-link probes fail before any local `setCIPHER_KEY` evidence and before the AP deauth.
  - guessed `setWCL_LINK_STATE_UPDATE` payload construction: rejected; the local ABI struct layout is not recovered, so fabricating that payload would be guessed state correction.
- confirmed deviation: reference association link-up updates the current-BSS manager, while local CR-070 publishes only the current AP address and never sends an association-edge link-state update after the real BSSID is known.
- root cause: confirmed for the persistent `0xe0822403` current-link blocker after CR-070's real-BSSID current-AP publish; later AP deauth/EAPOL/data-path success remains outside this claim.
- fix: after `syncTahoeCurrentApAddress(false, false)` newly publishes a real BSSID on a same-status active `RUN` update, invoke a narrow Tahoe association-link refresh under the command gate that calls `IO80211InfraInterface::setLinkState(kIO80211NetworkLinkUp, 0, false, 0, 0)` only if `ic_state == IEEE80211_S_RUN && ic_bss != nullptr`; do not repost BSSID/SSID/link messages and do not fabricate WCL payloads.
- verification: `git diff --check`; Tahoe build; BootKC symbol check; Stage 1 request; after approval install without unloading, reboot, perform one `btn-vno` join attempt, and verify the log shows real-BSSID current-AP publish followed by the new association-link refresh, then current-link probes no longer persistently return `0xe0822403` before any later EAPOL/AP-deauth blocker.
- notes: this is not a generic duplicate link-state replay. It is the missing association/WCL link-up producer corresponding to reference `setWCL_LINK_STATE_UPDATE`, deliberately separated from the earlier low-latency driver-ready link-up edge and stripped of duplicate user-visible notifications.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-CURRENTBSS-LINKUP-008
- symptom: CR-070 reaches `ASSOC -> RUN` and publishes `setCurrentApAddress(00:58:28:26:1c:1a)`, but WCL/CoreWiFi current-link probes still return `0xe0822403` and the join times out.
- expected system behavior: after association link-up, IO80211/WCL current-BSS state is associated with the selected BSS, so current SSID/BSSID/current-network probes can observe the current link.
- actual behavior: local Tahoe code updates only the current-AP address cache on the real RUN same-status path; the association link-state/current-BSS producer is not run after current AP becomes valid.
- exact divergence point: `AirportItlwm::setLinkStatus(...)` same-status active branch at `ic_state == IEEE80211_S_RUN`; it calls `syncTahoeCurrentApAddress(false, false)` and returns without any association link-state/current-BSS update.
- evidence from runtime: CR-070 log shows `ASSOC -> RUN`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, then `APPLE80211_IOC_SSID -> 0xe0822403` during RUN before AP deauth; OP_MODE/POWER getters work and local current-link helper logs are absent.
- evidence from decomp: `AppleBCMWLANCore::getCURRENT_NETWORK(...)` gates success on `IO80211BssManager::isAssociated(...)`; `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)` link-up calls `AppleBCMWLANBssManager::setCurrentBSS(...)`; `WCLBssManager::setCurrentBSS(...)` writes the current BSS; `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)` is a separate early link-up producer.
- exact semantic mismatch between reference and our code: reference has both an early driver-ready link-up and an association/WCL link-up producer that updates current BSS; local CR-070 has the early link-up and current-AP address publish, but lacks the association current-BSS/link-state producer after real RUN.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: early low-latency driver-ready link-up; actual net80211 `IEEE80211_S_RUN` with `ic_bss`; Tahoe current-AP address cache; IO80211InfraInterface link-state update; IO80211/WCL current-BSS associated state; current SSID/BSSID/current-network probes; link-down current-AP clearing.
  - expected contract at each touchpoint: early driver-ready link-up may occur before association; current-AP cache is real BSSID only at valid RUN; association link-up must refresh family current-link/current-BSS state after the real BSSID is available; public current-link probes must not see persistent not-associated state during local RUN; link-down clears current AP and current-BSS state through the existing down edge.
  - why no relevant touchpoints are missing: CR-070 runtime already proves UI visibility, scan visibility, set-mac, WCL associate, BSS selection, AUTH, ASSOC, RUN, and current-AP BSSID publication; the remaining blocker occurs between RUN/current-AP publication and AP deauth, exactly at the current-BSS/current-link probe layer.
  - why proposed path adds no extra system-visible side effects: the new gated refresh is invoked only when a real BSSID was newly published at RUN; it does not repost `APPLE80211_M_LINK_CHANGED`, `APPLE80211_M_BSSID_CHANGED`, or `APPLE80211_M_SSID_CHANGED`; it does not force success, retry, delay, poll, change packets, change keys, fabricate getter payloads, or construct guessed WCL structs.
- why this is root cause and not just correlation: the observed return code is the exact reference not-associated current-network code, the current-AP-only fix succeeded but did not change that code, and the reference link-up producer missing locally is the one that updates current BSS.
- why proposed fix is 1:1 with reference architecture and semantics: the exact Apple WCL payload ABI is not recovered, so the patch does not fabricate it; instead it uses the already linked IO80211InfraInterface link-state producer as the local system-facing association link-up touchpoint, after publishing the same real BSSID that reference would bind into current BSS.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - call local `getSSID/getBSSID/getCURRENT_NETWORK` directly from public GETs: rejected, previous public-routing attempts leaked raw `1` or bypassed the real current-link owner.
  - fabricate success/current network payloads: rejected, masks current-BSS ownership and can publish stale state.
  - construct `apple80211_wcl_update_link_state` by guessing offsets: rejected, exact payload ABI is not recovered.
  - call full `setLinkStateGated(...)` from RUN: rejected because it would also repost BSSID/SSID/link notifications; the proposed refresh keeps only the needed link-state producer call.
  - add retries, delays, polls, barriers, or AP-deauth suppression: rejected, the failure is deterministic current-BSS state absence before deauth.
  - change PMK/EAPOL/key/data path in this patch: rejected, those layers are not the first confirmed blocker in CR-070 runtime.
- verification plan: `git diff --check`; build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`; BootKC ABI check; create CR-071 exact-diff Stage 1 request; after approval install without unloading, reboot, make one `btn-vno` join attempt, and collect sudo logs for `setCurrentApAddress`, new association-link refresh, `SSID/BSSID/CURRENT_NETWORK` return codes, `setCIPHER_KEY`, WCL completion, AP deauth reason, and data counters.

## ANOMALY
- id: A-ASSOC-WCL-CURRENTBSS-PRODUCER-009
- status: CORRELATED
- symptom: CR-071 Stage 1 request was rejected even though CR-070 runtime proved that real-BSSID current-AP publication alone does not clear the `0xe0822403` current-link blocker.
- first visible manifestation: reviewer rejected CR-071 because the diff used a state-changing second link-up call without proving that it is the same producer contract as reference `setWCL_LINK_STATE_UPDATE(...) -> setCurrentBSS(...)`.
- expected system behavior: before any further state-changing patch, the exact Tahoe producer seam for current-BSS ownership must be identified or the remaining uncertainty must be reduced with behavior-neutral runtime evidence.
- actual behavior: we have narrowed the blocker to the current-BSS producer plane, but still do not know whether the local system ever reaches `setWCL_LINK_STATE_UPDATE(...)`, whether `setLinkStateInternal(...)` runs at real RUN, or whether `getAssocState()` itself is carrying an ABI/contract mismatch.
- divergence point: unresolved producer seam between local net80211 RUN/current-AP publication and the reference current-BSS owner `setWCL_LINK_STATE_UPDATE(...)`.
- evidence:
  - panic logs: none; CR-070 runtime is stable enough for instrumentation.
  - runtime logs: CR-070 shows `ASSOC -> RUN`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, then `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK -> 0xe0822403`.
  - runtime logs: existing logs already show `getAssocState base=831823872` on CR-070 and other large build-dependent garbage-like values on CR-067..CR-069, which is not yet explained.
  - runtime logs: existing corecapture lines show `setLinkStateInternal@604` at early driver-ready link-up and on link-down, but not yet at the real RUN window.
  - decomp: reference current-BSS producer is `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)`, which updates `AppleBCMWLANBssManager::setCurrentBSS(...)`.
  - reviewer decision: `/Users/bob/Projects/itlwm/commit-approval/decisions/COMMIT_DECISION_CR-071.md` rejects the previous state-changing refresh as structurally unproven and explicitly allows resubmission as `DIAGNOSTIC_INSTRUMENTATION` if the intent is to gather evidence.
- candidate causes:
  - hypothesis: local association path never reaches `setWCL_LINK_STATE_UPDATE(...)`, so the current-BSS producer is absent upstream of the interface.
  - hypothesis: local system does reach `setWCL_LINK_STATE_UPDATE(...)`, but the base producer returns failure or receives malformed payload.
  - hypothesis: `getAssocState()` has an ABI/signature/return-contract mismatch and current-link consumers are gated before the current-BSS producer can be observed.
  - hypothesis: `setLinkStateInternal(...)` already runs on a hidden RUN edge, and we are missing only the evidence for its ordering and arguments.
- rejected causes:
  - direct second `setLinkState(kIO80211NetworkLinkUp, ...)` as the next runtime patch: rejected by reviewer as an unproven state-changing refresh.
  - fabricating `apple80211_wcl_update_link_state`: rejected; exact payload ABI is still unrecovered.
- confirmed deviation: none yet at the exact producer seam; only the symptom chain is localized.
- root cause: not yet proven at the exact producer seam.
- fix: pending exact producer evidence; current allowed next step is behavior-neutral instrumentation only.
- verification: one post-instrumentation reboot and one `btn-vno` join attempt must reveal whether `setWCL_LINK_STATE_UPDATE(...)` and/or `setLinkStateInternal(...)` are actually invoked around RUN, and whether `getAssocState()` carries a sane contract value on the same window.
- notes:
  - CR-071 Stage 1 reviewer verdict: `REJECTED`, `allow_after_fix_runtime: NO`, `allow_commit_now: NO`.
  - This anomaly exists to justify a narrow diagnostic resubmission, not another guessed state transition.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-CURRENTBSS-PRODUCER-009
- symptom: the exact current-BSS producer seam remains unresolved after CR-070, and CR-071 was rejected because its replacement producer was not proven.
- expected system behavior: the next change should gather only the missing runtime evidence needed to distinguish the remaining producer hypotheses without changing system-facing behavior.
- actual behavior: current evidence proves the symptom and the reference owner, but not which local callback seam owns or misses the producer.
- exact divergence point: between local `ASSOC -> RUN` / `setCurrentApAddress(...)` and the unseen reference producer `setWCL_LINK_STATE_UPDATE(...)`.
- evidence from runtime: CR-070 runtime proves real-BSSID current-AP publication plus persistent `0xe0822403`; existing logs also show build-dependent garbage-like `getAssocState base=...` values and `setLinkStateInternal` only on early boot/down edges.
- evidence from decomp: `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)` is the reference producer; `getCURRENT_NETWORK(...)` depends on associated current-BSS state; reviewer explicitly rejected the previous state-changing substitute and requested either proof of the actual producer or behavior-neutral instrumentation.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- exact hypotheses being disambiguated:
  - H1: local association path already invokes `setWCL_LINK_STATE_UPDATE(...)`, and the failure is inside or after the base producer.
  - H2: local association path never invokes `setWCL_LINK_STATE_UPDATE(...)`, so the producer is missing before it reaches the interface.
  - H3: `getAssocState()` carries an ABI/signature/return-contract mismatch that is itself a current-link contract break.
  - H4: `setLinkStateInternal(...)` already runs at real RUN with meaningful arguments, and the missing evidence is ordering, not absence.
- exact probe points:
  - `AirportItlwmSkywalkInterface::setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *)`
  - `AirportItlwmSkywalkInterface::setLinkStateInternal(IO80211LinkState,uint,bool,uint,uint)`
  - `AirportItlwmSkywalkInterface::getAssocState(void)` with more explicit raw-value logging
- why these probe points are sufficient:
  - if `setWCL_LINK_STATE_UPDATE(...)` is never logged during the join, H1 is rejected and H2 gains support.
  - if it is logged, the raw bytes, local state, and base return value tell us whether the producer reached the interface and what contract surface it carried.
  - if `setLinkStateInternal(...)` is or is not logged around RUN, we will know whether a hidden internal link-state seam already exists there.
  - if `getAssocState()` continues to produce build-dependent garbage-like values, H3 becomes a concrete contract candidate instead of a vague suspicion.
- why instrumentation is behavior-neutral:
  - every new override is a strict passthrough to the existing base implementation with identical arguments and identical return value propagation.
  - no new state transitions, callbacks, retries, delays, polls, payload mutation, cache mutation, packet changes, or notification replays are introduced.
  - added reads are limited to logging pointer/raw-byte snapshots and already available local state.
- what exact runtime evidence must be collected:
  - one reboot with the instrumented driver.
  - one join attempt to `btn-vno`.
  - sudo logs covering `setWCL_LINK_STATE_UPDATE`, `setLinkStateInternal`, `getAssocState`, `setCurrentApAddress`, `ASSOC -> RUN`, `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK`, and any subsequent AP deauth/EAPOL/key events.
- why this is root cause and not just correlation: not applicable yet; this patch is explicitly diagnostic and does not claim to fix root cause.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it does not alter the architecture at all; it only exposes whether the reference-named producer seam is ever reached and what the existing base owner returns.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - keep the CR-071 state-changing refresh: rejected by reviewer.
  - guess `apple80211_wcl_update_link_state` layout: rejected.
  - add broader tracing across unrelated getters/setters: rejected as unnecessary scope expansion.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - create CR-072 Stage 1 request as `DIAGNOSTIC_INSTRUMENTATION`
  - after approval, install without unloading, reboot, run one `btn-vno` join attempt, and collect the targeted sudo logs above.

## ANOMALY
- id: A-ASSOC-WCL-LINKSTATE-PRODUCER-010
- status: CONFIRMED_ROOT_CAUSE
- symptom: exact CR-072 runtime still shows visible networks and a local successful join to `btn-vno`, but the system never recognizes the interface as associated and tears the link back down.
- first visible manifestation: with loaded UUID `3F179B5E-0593-3ABB-A293-6F4CF4D27048`, the join reaches `SCAN -> AUTH -> ASSOC -> RUN`, publishes `setCurrentApAddress addr=00:58:28:26:1c:1a`, yet `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK` continue returning `0xe0822403`, after which the interface falls back to `RUN -> AUTH`.
- expected system behavior: after a successful association to `btn-vno`, the same success path must reach the reference current-BSS producer plane so that IO80211/WCL current-link state becomes associated before CoreWiFi/airportd re-probes SSID/BSSID/current-network.
- actual behavior: CR-072 proves that neither `setWCL_LINK_STATE_UPDATE(...)` nor any hidden `setLinkStateInternal(...)` link-up edge is reached at the real RUN window; only the early driver-ready path and later link-down path touch `setLinkStateInternal(...)`.
- divergence point: local success path between hidden/public association acceptance and the reference current-BSS owner `setWCL_LINK_STATE_UPDATE(...)`; the producer is absent upstream of the interface rather than malformed inside the interface override.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-072-after-join-filtered-20260423-2050.log`, 6209 lines.
  - runtime logs: `20:56:12.341630 WCLJoinRequest Beacon Info ... BSSID=00:58:28:26:1C:1A ssid='btn-vno'`.
  - runtime logs: `20:56:12.341657 setWCL_ASSOCIATE [btn-vno] ...`, followed by `setWCL_ASSOCIATE BSSID=00:00:00:00:00:00`; the hidden carrier itself is not a trustworthy current-BSS source.
  - runtime logs: `20:56:27.042260 ieee80211_node_join_bss selbs=btn-vno mac=00:58:28:26:1c:1a`.
  - runtime logs: `20:56:27.046306 SCAN -> AUTH`, `20:56:27.086843 AUTH -> ASSOC`, `20:56:27.096341 ASSOC -> RUN`.
  - runtime logs: `20:56:27.096375 syncTahoeCurrentApAddress addr=00:58:28:26:1c:1a` and `20:56:27.096385 setCurrentApAddress addr=00:58:28:26:1c:1a`.
  - runtime logs: after that RUN edge there are still immediate `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK -> 0xe0822403` failures, for example `20:56:28.136713 APPLE80211_IOC_SSID -> 0xe0822403`.
  - runtime logs: `20:56:31.105640 RUN -> AUTH`, then `syncTahoeCurrentApAddress addr=(null) forceClear=1`.
  - runtime logs: `setWCL_LINK_STATE_UPDATE` is absent from the entire CR-072 evidence file.
  - runtime logs: `setLinkStateInternal state=2` appears only during the early driver-ready enable window (`20:51:08`, `20:51:10`), and `setLinkStateInternal state=1` appears only on the later teardown at `20:56:31`; there is no link-up/internal producer call at the real RUN window.
  - decomp: `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)` is the reference link-up producer that drives `AppleBCMWLANBssManager::setCurrentBSS(...)`.
  - decomp: `AppleBCMWLANCore::getCURRENT_NETWORK(...)` returns `0xe0822403` while current-BSS state is not associated.
- candidate causes:
  - confirmed: the local join success path reaches RUN/current-AP publication but never reaches the reference-named current-BSS producer.
  - confirmed: current-AP publication alone is insufficient for CoreWiFi/airportd association visibility.
  - confirmed: the hidden assoc carrier's logged BSSID bytes are zero and therefore are not the owner of the eventual associated BSSID seen at RUN.
- rejected causes:
  - malformed local `setWCL_LINK_STATE_UPDATE(...)` override: rejected for this runtime; the override is never called.
  - hidden/internal link-up already happening at RUN but missed by logging: rejected; CR-072 added direct passthrough logging and there is no `setLinkStateInternal(...)` call at the RUN window.
  - `setCurrentApAddress(...)` missing or wrong BSSID: rejected; CR-072 publishes the real `00:58:28:26:1c:1a` immediately at RUN.
  - hidden-assoc PMK crash path: rejected for this runtime; there is no panic and the join reaches RUN.
- confirmed deviation: the local driver has no observed association-success producer corresponding to reference `setWCL_LINK_STATE_UPDATE(...) -> setCurrentBSS(...)`, even though the radio-side association itself succeeds.
- root cause: confirmed for the current "networks visible but connection does not complete in UI" blocker on CR-072; later EAPOL/data-path issues must only be investigated after this producer plane exists.
- fix: restore the real current-BSS/link-state producer at the local join-success seam; do not reuse the rejected artificial second `setLinkState(...)` refresh and do not source current-BSS identity from the zero-BSSID hidden assoc carrier.
- verification: after the next fix, one reboot and one `btn-vno` join attempt must show a post-RUN current-BSS producer event before `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK` stop returning `0xe0822403`; `RUN -> AUTH` must no longer be the immediate aftermath of a locally successful join.
- notes:
  - CR-072 achieved its intended purpose: it disproved both "the producer already reaches the interface" and "a hidden internal link-up already runs at RUN".
  - The next state-changing patch must target the missing producer seam itself, not `getSSID/getBSSID/getCURRENT_NETWORK`.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-LINKSTATE-PRODUCER-010
- symptom: CR-072 proves that local association succeeds up to `ASSOC -> RUN`, but the current-BSS producer plane is never invoked and the system falls back to not-associated state.
- expected system behavior: the exact local join-success seam that corresponds to reference association completion must emit the current-BSS/link-state producer before CoreWiFi/airportd re-evaluate current-link state.
- actual behavior: the only observed producers are early driver-ready `setLinkStateInternal state=2` and later teardown `state=1`; the successful RUN window publishes only current-AP address and then collapses back to `RUN -> AUTH`.
- exact divergence point: local success path upstream of `AirportItlwmSkywalkInterface::setWCL_LINK_STATE_UPDATE(...)`; the interface-level producer override exists and is callable, but no caller reaches it on successful association.
- evidence from runtime: CR-072 evidence file shows `WCLJoinRequest Beacon Info` with real BSSID, `setWCL_ASSOCIATE` acceptance, `SCAN -> AUTH -> ASSOC -> RUN`, real-BSSID `setCurrentApAddress`, no `setWCL_LINK_STATE_UPDATE`, no `setLinkStateInternal` at RUN, persistent `0xe0822403`, then `RUN -> AUTH`.
- evidence from decomp: reference uses `AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE(...)` as the current-BSS owner; `getCURRENT_NETWORK(...)` depends on associated current-BSS state; the earlier artificial second link-up refresh was correctly rejected because it did not prove ownership.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- if REFERENCE_ALIGNMENT_FIX:
  - enumerated system-facing touchpoints: hidden/public association acceptance; local join-success callback/seam; IO80211/WCL current-BSS producer; current-AP cache; current SSID/BSSID/current-network probes; teardown/down edge.
  - expected contract at each touchpoint: hidden/public association setup may remain as-is; the exact success seam must invoke the current-BSS producer once per successful join; current-AP cache continues to mirror the associated BSSID; public probes stop seeing `0xe0822403` during valid RUN; teardown still clears state through the existing down edge.
  - why no relevant touchpoints are missing: CR-072 already proves that radio-side join, auth, assoc, and RUN succeed; the only missing reference-visible edge before system rollback is the current-BSS producer.
  - why proposed path adds no extra system-visible side effects: it targets the missing owner once at the real join-success seam and avoids replaying generic link notifications, getter fabrication, retries, delays, or guessed carrier data from the hidden assoc blob.
- why this is root cause and not just correlation: current-link probes stay at the exact reference not-associated code after a proven local RUN, and CR-072 directly proves that the reference producer plane is never invoked in that window.
- why proposed fix is 1:1 with reference architecture and semantics: it moves the recovery target from a guessed secondary link refresh to the actual missing producer seam that reference uses to transition current-BSS state after association success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - another synthetic `IO80211InfraInterface::setLinkState(...)` refresh from `setLinkStatus(...)`: rejected by CR-071 review and by CR-072 evidence.
  - fabricating success/current-network payloads in getters: rejected; masks the missing owner.
  - sourcing current-BSS identity from logged hidden-assoc carrier BSSID bytes: rejected; the carrier currently shows zero BSSID while the real associated BSSID appears later from scan/join state.
  - stopping at additional diagnostics: rejected; CR-072 already gave the missing negative proof.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - exact-diff Stage 1 request
  - after approval, install without unloading, reboot, run one `btn-vno` join attempt, and collect sudo logs for the recovered producer seam, `setCurrentApAddress`, current-link probe return codes, and whether `RUN -> AUTH` still occurs.

## ANOMALY
- id: A-DRIVER-READY-FALSE-LINKUP-011
- status: FIX_VERIFIED
- symptom: the Tahoe driver-ready path marks the main infrastructure interface link-up before any association exists, so the real association-success edge is later discarded as a duplicate and never reaches the current-link producer path.
- first visible manifestation: during boot at `2026-04-23 20:51:08`, `performTahoeBootChipImage` first logs `setLinkStatus status=0x1 (prev=0x1) active=0`, then `setInterfaceEnable enable=1`, after which `setLinkStateInternal state=2 ... ic_state=0 currentStatus=0x1` immediately flips into `setLinkStatus status=0x3 (prev=0x1) active=1` while still not associated.
- expected system behavior: driver-ready publication must make the driver visible in UI without placing the main infra interface into active/associated link state; the first real `0x1 -> 0x3` link-active transition must happen only at association success.
- actual behavior: commit `43bf34f` aliases Apple's hidden low-latency readiness object to `fNetIf`, and its lifted `setInterfaceEnable(bool)` body executes `reportLinkStatus(3, 0x80)` plus `setLinkState(...Up...)` on the same infra interface that later owns association visibility.
- divergence point: `AirportItlwmSkywalkInterface::setInterfaceEnable(bool)` on the local Tahoe port; the Apple subclass body is being replayed on the wrong object identity.
- evidence:
  - runtime logs: `2026-04-23 20:51:08.383 IO80211InfraInterface::setInterfaceEnable AirportItlwmSkywalkInterface isEnable 1`.
  - runtime logs: immediately after that, `setLinkStateInternal state=2 ... ic_state=0 currentStatus=0x1`.
  - runtime logs: immediately after that, `setLinkStatus status=0x3 (prev=0x1) active=1 ... ic_state=0`.
  - runtime logs: the same boot window shows `setInterfaceEnable enable=1 ret=0x0`, then only afterwards `applyTahoeInterfaceReadyEdge ready-edge ret=0x0` and `setCoreWiFiDriverReadyProperty ready=1`.
  - runtime logs: during the real join at `2026-04-23 20:56:27.089`, `eventHandler msgCode=1` proves `IEEE80211_EVT_STA_ASSOC_DONE` is delivered and `postMessageGated msg=9` proves `APPLE80211_M_ASSOC_DONE` is posted.
  - runtime logs: at the real success edge `2026-04-23 20:56:27.096`, `ASSOC -> RUN` is followed by `setLinkStatus status=0x3 (prev=0x3) active=1`, so the early-return path suppresses the real link-up transition.
  - runtime logs: the same RUN window has real-BSSID `syncTahoeCurrentApAddress/setCurrentApAddress`, but no `setLinkStateGated` link-up, no `setLinkStateInternal` link-up, and no `setWCL_LINK_STATE_UPDATE`.
  - runtime logs: `airportd` then continues to read `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK -> 0xe0822403`, and the AP deauth arrives later with reason `15`; user space then reports failed association request/disassociate reason `10`.
  - source history: commit `43bf34f75a02bd2e522ee3584f888a6b2eb76ff4` introduced the lifted subclass body with `reportLinkStatus(3, 0x80)` and `IO80211InfraInterface::setLinkState(...Up...)` inside `setInterfaceEnable(bool)`.
  - after-fix runtime logs: with loaded UUID `E6B5AEC2-BD17-39A4-8833-CEAC86AC7618`, boot at `2026-04-23 21:46:20.335` shows `setInterfaceEnable enable=1 ret=0x0`, `applyTahoeInterfaceReadyEdge ready-edge ret=0x0`, `setCoreWiFiDriverReadyProperty ready=1`, and `postTahoeDriverAvailableBulletin ready=1`, but no boot-time `setLinkStateInternal state=2` and no boot-time `setLinkStatus status=0x3 (prev=0x1)`.
  - after-fix runtime logs: the first observed `setLinkStatus status=0x3 (prev=0x1)` now occurs at the real association edge `2026-04-23 21:51:04.324 ASSOC -> RUN`, immediately followed by `setLinkStateInternal state=2`, proving the bootstrap poisoning seam is removed.
- rejected causes:
  - lost association-complete event: rejected; `IEEE80211_EVT_STA_ASSOC_DONE` and `APPLE80211_M_ASSOC_DONE` are both logged on the successful join.
  - missing current AP address: rejected; the real BSSID `00:58:28:26:1c:1a` is published at RUN.
  - getter-only failure: rejected; the producer edge is suppressed before getters are even consulted.
- root cause: the premature driver-ready link-up poisons `currentStatus` to `0x3` before association, so the real `ASSOC -> RUN` transition never produces the system-facing associated/current-network state.
- fix: keep Tahoe driver-ready publication (`setInterfaceEnable` base call, ready property, DRIVER_AVAILABLE bulletin), but stop replaying link-up side effects on `fNetIf` from `AirportItlwmSkywalkInterface::setInterfaceEnable(bool)`; if Apple really needs those side effects, they must live on a separate hidden low-latency facade rather than the main infra interface.
- verification: verified by exact reviewed diff `CR-074` after-fix runtime. Boot no longer drives `currentStatus 0x1 -> 0x3` during `setInterfaceEnable(enable=1)`, driver visibility and scan visibility remain intact, and the first `setLinkStatus status=0x3 (prev=0x1)` moved to the real `ASSOC -> RUN` window together with the recovered `setLinkStateInternal state=2` link-up edge. Remaining failure is now a separate post-RUN blocker.
- reviewer note: Stage-1 request `CR-073` was rejected as `REFERENCE_ALIGNMENT_FIX` because the local delta removes Apple low-latency side effects from `fNetIf` instead of restoring them on a recovered hidden low-latency object. The same runtime evidence remains valid, but the approval path must be `SYSTEM_CONTRACT_FIX`, and the evidence manifest must use the real sha256 `2be78aa1e0c0dfaad63bb82f757090f2bc4efc3706ff55fe171f44039f278a6f` for `CR-072-after-join-filtered-20260423-2050.log`.

## FIX_CANDIDATE

- anomaly_id: A-DRIVER-READY-FALSE-LINKUP-011
- symptom: the local Tahoe port reaches real association success, but the main infra interface was already forced into link-up during driver-ready bootstrap, so the real success edge is ignored.
- expected system behavior: driver-ready and association-success must be distinct state machines; making the driver visible cannot pre-consume the only meaningful link-up transition that current-network publication depends on.
- actual behavior: `AirportItlwmSkywalkInterface::setInterfaceEnable(bool)` on `fNetIf` replays Apple's low-latency subclass link-up side effects and turns `currentStatus` active before any join.
- exact divergence point: local `setInterfaceEnable(bool)` side effects on `fNetIf`, not the later association event path.
- evidence from runtime: boot logs show `setInterfaceEnable -> setLinkStateInternal(link<2>) -> setLinkStatus 0x1->0x3` while `ic_state=0`; join logs later show `ASSOC -> RUN` with `setLinkStatus 0x3->0x3`, no link-up producer, persistent `0xe0822403`, and rollback.
- evidence from source history: `43bf34f` explicitly added the replayed hidden subclass body; the same file now proves the lifted side effects run on `AirportItlwmSkywalkInterface`, which is the main local infra interface object.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: `fNetIf->setInterfaceEnable(true)` base ready-edge lifecycle callback; `CoreWiFiDriverReadyKey = OSString("true"/"false")`; `APPLE80211_M_DRIVER_AVAILABLE` (`0x37`) bulletin through controller/PostOffice with payload length `0xf8`; scan-result and scan-done publication (`APPLE80211_M_WCL_SCAN_RESULT`, `APPLE80211_M_WCL_SCAN_DONE`, `APPLE80211_M_SCAN_DONE`); boot-time main-interface link state (`currentStatus`); real `ASSOC -> RUN` active edge; Apple-visible current `SSID` / `BSSID` / `CURRENT_NETWORK` probes; existing down/teardown edge.
  - expected contract at each touchpoint: the base ready-edge lifecycle callback must still run so the interface stays attached/alive; the ready property must remain published as OSString, not OSBoolean; the `DRIVER_AVAILABLE` bulletin must keep its exact message code, transport, payload size, and polarity; scan results and scan completion must continue to post directly through `fNetIf` without requiring associated link state; boot-time `currentStatus` on the main infra interface must remain non-associated until a real join succeeds; the first `0x1 -> 0x3` active transition on that interface must occur only at `ASSOC -> RUN`; current-network probes may stop failing only after that real active edge; teardown must still clear link/current state through the existing down path.
  - why no relevant touchpoints are missing: every currently proven user-visible behavior in scope enters through one of those seams. Driver visibility is already explained by `publishTahoeDriverReadyState()` (`applyTahoeInterfaceReadyEdge` base enable, `setCoreWiFiDriverReadyProperty`, `postTahoeDriverAvailableBulletin`). Network visibility is already explained by `postWclScanResultsGated(...)` and `eventHandler(... IEEE80211_EVT_SCAN_DONE ...)`, both of which post directly to `fNetIf` and do not consult `currentStatus`. The failing symptom begins only after successful `ASSOC -> RUN`, so no earlier hidden touchpoint remains unaccounted for inside this claim scope.
  - why proposed path adds no extra system-visible side effects: the fix keeps the ready-edge base lifecycle callback, ready property publication, bulletin payload/transport, scan-result publication, and teardown path unchanged. It only removes the proven-wrong bootstrap `reportLinkStatus(3, 0x80)` plus `setLinkState(...Up...)` side effects from the main infra interface, and it adds no new producer, replay, retry, timing heuristic, getter fabrication, or message mutation.
- why this is root cause and not just correlation: the same `currentStatus=0x3` written at boot is the exact value that suppresses the only later `ASSOC -> RUN` link-up call by equality short-circuit (`status == currentStatus`), and that suppression directly matches the absence of all downstream producer logs.
- why proposed fix is 1:1 with reference architecture and semantics: this is not claimed as hidden-object identity replay. It is claimed as exact recovery of the system-facing separation that reference exposes: availability producers remain on the ready path, scan visibility remains independent of association state, and associated/current-network state is no longer pre-consumed before the real join edge. Within the user-visible contract boundary, the resulting semantics match reference without inventing any extra visible producer or state transition.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - add yet another synthetic link-up after `ASSOC -> RUN`: rejected; duplicates the already rejected CR-071 shape.
  - keep the false boot-time link-up and try to clear it later with another synthetic down/up pair: rejected; adds more state churn on the same wrong object and risks UI regressions.
  - fabricate associated getters despite poisoned currentStatus: rejected; masks the root seam and leaves airportd/CoreWiFi state machines inconsistent.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - Stage 1 request for the exact diff
  - after approval, install without unloading, reboot, and verify:
    - boot no longer flips `currentStatus` to `0x3` during `setInterfaceEnable(enable=1)`
    - the first `setLinkStatus status=0x3 (prev=0x1)` occurs at `ASSOC -> RUN`
    - link-up producer logs appear at the real join edge
    - `APPLE80211_IOC_SSID/BSSID/CURRENT_NETWORK` stop returning `0xe0822403` before any rollback.

## ANOMALY
- id: A-ASSOC-CURRENTLINK-PROBE-ROUTE-012
- status: CORRELATED
- symptom: after `CR-074` restores the real `ASSOC -> RUN` active edge, the association still fails because current-link probes continue returning `0xe0822403` immediately after the corrected link-up.
- first visible manifestation: on the exact `CR-074` after-fix runtime, `2026-04-23 21:51:04.324 ASSOC -> RUN` is followed by `setLinkStatus status=0x3 (prev=0x1)`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, and `setLinkStateInternal state=2`, yet in that same moment IO80211Family logs `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`, and later airportd logs `GET SSID -> 0xe0822403`.
- expected system behavior: once the main-interface active edge is correctly moved to the real RUN window, the next current-link probes must either traverse the permissive local SSID/BSSID getters or observe an associated current-BSS owner state; they must not still fail as not-associated in the same RUN window.
- actual behavior: the corrected real RUN edge now occurs, but the first post-RUN BSSID probe still fails immediately and the link later rolls back to `RUN -> AUTH` with deauth reason `15`.
- divergence point: unresolved current-link probe route and/or current-BSS association owner between `IO80211Family::getBSSIDData()` and the local permissive SSID/BSSID handler layer after real `setLinkStateInternal(state=2)`.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-074-afterfix-boot-ready-window-20260423.log` sha256 `9217a8210448207108b9ff587b8efeaefff3a67f964397a7c46a06a8b2285a99`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-074-afterfix-assoc-window-20260423.log` sha256 `8ee4d95e54edabed8899aa2518bf649d1fd6bf3c3f19683b1db58d6d6d109737`.
  - runtime logs: boot at `21:46:20.335` shows `setInterfaceEnable enable=1 ret=0x0`, ready-property/bulletin publication, and no boot-time `setLinkStateInternal state=2`; this confirms anomaly `A-DRIVER-READY-FALSE-LINKUP-011` is no longer the active blocker.
  - runtime logs: `21:51:04.324 ASSOC -> RUN`, `setLinkStatus status=0x3 (prev=0x1)`, `syncTahoeCurrentApAddress addr=00:58:28:26:1c:1a`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, `setLinkStateInternal state=2 ... ic_state=4 currentStatus=0x3`.
  - runtime logs: in the same RUN window, `IO80211Family` logs `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`.
  - runtime logs: `setWCL_LINK_STATE_UPDATE(...)` is still absent from the same association window.
  - runtime logs: later at `21:51:08.332`, the interface falls back through `RUN -> AUTH`, `deauthReason=15`, `setCurrentApAddress addr=(null)`, and link-down `setLinkStateInternal state=1`.
  - runtime logs: airportd at `21:51:08.974` logs `BEGIN REQ [GET SSID] ... ifname['en0'] IOCTL type 1/'APPLE80211_IOC_SSID' return -528342013/0xe0822403`.
  - source code: `AirportItlwmSkywalkInterface::getSSID(...)` and `getBSSID(...)` zero-fill and return success, and `AirportItlwm::getSSID(...)` / `getBSSID(...)` in `AirportSTAIOCTL.cpp` do the same when `ic_state == IEEE80211_S_RUN`.
  - source code: `processBSDCommand(...)` / `processApple80211Ioctl(...)` already log Tahoe current-link probes, yet those route logs do not appear around the failing `getBSSIDData()` window.
- candidate causes:
  - hypothesis: the failing `getBSSIDData()` / `GET SSID` probes are using a controller-side direct ingress (`apple80211Request(...)` and/or `handleCardSpecific(...)`) that is not currently instrumented for current-link probes.
  - hypothesis: the probe reaches a different family/WCL owner path and fails because current-BSS associated state is still not established, even though `setLinkStateInternal(state=2)` now runs at RUN.
  - hypothesis: the direct probe happens before or outside the local skywalk BSD bridge, so the permissive skywalk getter contract is never exercised for this exact call site.
- rejected causes:
  - bootstrap false link-up still pre-consuming the real RUN edge: rejected by the CR-074 after-fix runtime.
  - missing real current AP address at RUN: rejected; the real BSSID `00:58:28:26:1c:1a` is published before the first failure.
  - missing real link-up/internal edge at RUN: rejected; `setLinkStateInternal state=2` now occurs exactly at RUN.
- notes:
  - this anomaly is narrower than `A-ASSOC-WCL-LINKSTATE-PRODUCER-010`: the real active edge is restored, so the remaining uncertainty is now the post-RUN probe/owner route.
  - the next safe step is behavior-neutral instrumentation of the remaining current-link ingress points, not a guessed state-changing fix.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-CURRENTLINK-PROBE-ROUTE-012
- symptom: after the corrected real RUN edge, `IO80211Family::getBSSIDData()` and later airportd `GET SSID` still fail with `0xe0822403`.
- expected system behavior: the first current-link probes after a real `ASSOC -> RUN` must either traverse the local permissive SSID/BSSID handlers or show an associated current-BSS owner state; the exact ingress must be observable.
- actual behavior: the corrected RUN edge now emits `setLinkStateInternal(state=2)`, but the first observed post-RUN BSSID probe still fails before we can attribute the failure to a specific local ingress path.
- exact divergence point: unresolved direct current-link probe ingress between `IO80211Family` current-link consumers and the remaining local controller/skywalk probe routes after real RUN.
- evidence from runtime: CR-074 after-fix runtime proves the corrected RUN edge, immediate `getBSSIDData()` failure, later airportd `GET SSID -> 0xe0822403`, no `setWCL_LINK_STATE_UPDATE`, and rollback to `RUN -> AUTH`.
- evidence from code: Tahoe already logs public current-link probes on the skywalk BSD bridge in `processBSDCommand(...)` / `processApple80211Ioctl(...)`; controller-side `apple80211Request(...)`, `handleCardSpecific(...)`, and leaf `getSSID/getBSSID` were the remaining Tahoe-active local observation points. `AirportItlwm::apple80211_ioctl(...)` exists only under `#if __IO80211_TARGET < __MAC_26_0` in both declaration and implementation, so it is dead code on Tahoe and cannot be part of the active hypothesis set.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated:
    - H1: `IO80211Family::getBSSIDData()` uses controller `apple80211Request(...)`, and the controller path is returning or propagating `0xe0822403`.
    - H2: the failing probe bypasses the already logged skywalk BSD bridge and instead uses controller `handleCardSpecific(...)` before reaching any permissive local leaf handler.
    - H3: no local ingress handles the probe at all, and the failure occurs inside framework/WCL current-BSS ownership after real `setLinkStateInternal(state=2)`.
  - exact probe points:
    - existing Tahoe skywalk bridge logs in `AirportItlwmSkywalkInterface::processBSDCommand(...)` and `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)`
    - `AirportItlwm::apple80211Request(unsigned int request_type, int request_number, IO80211Interface *interface, void *data)`
    - `AirportItlwm::handleCardSpecific(IO80211SkywalkInterface *interface, unsigned long cmd, void *data, bool isSet)`
    - controller-side `AirportItlwm::getSSID(...)` and `AirportItlwm::getBSSID(...)`
  - why these probe points are sufficient: `processBSDCommand(...)` / `processApple80211Ioctl(...)` already cover the Tahoe public BSD Apple80211 path; the new controller logs cover the remaining Tahoe-active local controller seams; the leaf `getSSID/getBSSID` logs prove whether the failing probe reaches the permissive local handlers. If none of these Tahoe-active observation points fire in the failing window, the remaining route is upstream of all local Tahoe request handling and the blocker stays in framework/WCL ownership; if any fire, the exact owner path becomes concrete in one reboot.
  - why instrumentation is behavior-neutral: the proposed change only logs request numbers, ingress choice, `ic_state`, and return codes. It does not alter routing, payloads, state, timing, or ownership.
  - what exact runtime evidence must be collected:
    - boot-ready window proving CR-074 behavior remains intact
    - one join attempt window covering `ASSOC -> RUN`, `setLinkStateInternal(state=2)`, current-link probe ingress logs, `getSSID/getBSSID` controller helper logs if any, `setWCL_LINK_STATE_UPDATE` presence/absence, and the subsequent `RUN -> AUTH` / deauth edge
- why this is root cause and not just correlation: not yet proven; this candidate is explicitly diagnostic and is intended to turn the remaining route ambiguity into a concrete owner seam.
- why proposed fix is 1:1 with reference architecture and semantics: it does not change semantics at all; it only reveals which already-existing ingress or framework owner handles the failing current-link probes after the corrected RUN edge.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportSTAIOCTL.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - add another state-changing current-BSS/link-up replay after RUN: rejected; still guessed without proving the active failing ingress.
  - force success from `getSSID/getBSSID/getCURRENT_NETWORK`: rejected; would mask the unresolved route/owner seam.
  - infer the ingress from existing logs alone: rejected; the failing window currently lacks controller-side probe-route instrumentation.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - Stage 1 request as `DIAGNOSTIC_INSTRUMENTATION`
  - after approval, install without unloading, reboot, run one `btn-vno` join attempt, and collect the targeted probe-route logs above.

## ANOMALY
- id: A-ASSOC-DATAPATH-ASSOC-GATE-013
- status: CORRELATED
- symptom: after the real `ASSOC -> RUN` edge, current-link ownership still never becomes associated, and the framework later resets DPS on teardown.
- first visible manifestation: on `CR-074` / `CR-076` after-fix runtime, the join reaches `ASSOC -> RUN`, `setLinkStatus status=0x3 (prev=0x1)`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, and `setLinkStateInternal state=2`, yet the same window immediately logs `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`; on the later down edge the same window logs `IO80211Family Reseting DPS state... in resetStuckDataPathCheck`.
- expected system behavior: after a real RUN edge, the framework's internal association/data-path gate should consult `getAssocState()`, enter `setDataPathState(...)`, and continue into the normal post-link data-path/current-link machinery before any current-link getter re-queries state.
- actual behavior: the real RUN edge clearly reaches local `setLinkStateInternal(state=2)`, but there is no evidence yet that the corresponding framework assoc/data-path gate ever consults `getAssocState()` in the same RUN window or enters `setDataPathState(...)`.
- divergence point: unresolved framework gate between `IO80211InfraInterface::setLinkState(...)` and downstream `setDataPathState(...)`, with `getAssocState()` as the most likely guard seam.
- evidence:
  - runtime logs: `CR-076-afterfix-assoc-window-20260423-231548.log` shows `eventHandler msgCode=1`, `postMessageGated msg=9`, `ASSOC -> RUN`, `setLinkStatus status=0x3 (prev=0x1)`, `setCurrentApAddress addr=00:58:28:26:1c:1a`, `setLinkStateInternal state=2`, immediate `getBSSIDData()` failure, then rollback and `sendWCLJoinDone lastStatusCode=1009 extendedCode=1007`.
  - runtime logs: `CR-074-afterfix-assoc-window-20260423.log` shows the same RUN edge and later `IO80211Family Reseting DPS state... in resetStuckDataPathCheck` on the down edge, which implies the framework DPS plane stayed unresolved through the failed association.
  - runtime logs: all observed `getAssocState` logs so far carry a stable low byte of `0x00` despite garbage upper bits in the raw `int` register image, e.g. `0x85240000`, `0x511ae000`, `0x31950000`, while boot/runtime state remains not-associated in those windows.
  - decomp: `IO80211Family` `FUN_ffffff8002214486` calls interface slot `[429] getAssocState()` and returns early if the returned low byte is zero; only the non-zero path delegates to `FUN_ffffff8002214504(...) -> setDataPathState(...)`.
  - decomp: `FUN_ffffff80022e673c` is the internal `setDataPathState(bool)` implementation and immediately enters `IO80211SkywalkInterface::reportDataPathEventsGated(...)`, which is the framework-owned datapath/event path absent from current runtime evidence.
- candidate causes:
  - hypothesis: on the real RUN edge, `getAssocState()` is consulted but still returns a zero low byte, so the framework gate aborts before `setDataPathState(...)`.
  - hypothesis: the local `IO80211InfraInterface::setLinkState(...)` call on this Tahoe port never reaches `FUN_ffffff8002214486`, so neither `getAssocState()` nor `setDataPathState(...)` are involved on the failing up edge.
  - hypothesis: `setDataPathState(...)` actually fires, but the current-BSS/current-link owner still breaks later in a deeper framework seam.
- rejected causes:
  - missing real RUN edge: rejected; the runtime clearly shows `ASSOC -> RUN` and `setLinkStateInternal(state=2)`.
  - missing current AP address publication: rejected; the real BSSID is published via `setCurrentApAddress(...)`.
  - local Tahoe request-routing ingress: rejected by `CR-077`; the remaining failure is above all Tahoe-active local current-link request handlers.
- notes:
  - the raw `getAssocState` integer value is not itself a confirmed bug because the decomp uses only the low byte; the real question is what low byte the framework sees at the real RUN edge.
  - this anomaly is intentionally narrower than the earlier current-link ingress question and focuses only on the framework assoc/data-path gate.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-DATAPATH-ASSOC-GATE-013
- symptom: after the corrected real RUN edge, current-BSS ownership still never becomes associated and the framework later resets DPS on teardown.
- expected system behavior: the post-RUN framework gate must either consult `getAssocState()` and enter `setDataPathState(...)`, or we must prove that this entire gate is bypassed on the active Tahoe path.
- actual behavior: current runtime proves `setLinkStateInternal(state=2)` at RUN and later DPS reset on teardown, but does not yet prove whether `getAssocState()` / `setDataPathState(...)` ran on the successful up edge.
- exact divergence point: unresolved framework assoc/data-path gate immediately downstream of `IO80211InfraInterface::setLinkState(...)`.
- evidence from runtime: `CR-074` / `CR-076` after-fix runtime shows real RUN, real `setLinkStateInternal(state=2)`, immediate `getBSSIDData()` failure, no `setWCL_LINK_STATE_UPDATE(...)`, and later `resetStuckDataPathCheck`.
- evidence from decomp: `FUN_ffffff8002214486` gates `setDataPathState(...)` on interface `getAssocState() != 0`; `FUN_ffffff80022e673c` shows that `setDataPathState(...)` is the entry into framework datapath event handling.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated:
    - H1: the real RUN edge reaches the family assoc gate, but `getAssocState()` still presents low-byte zero, so `setDataPathState(...)` is skipped.
    - H2: the real RUN edge never reaches the family assoc gate at all, so neither `getAssocState()` nor `setDataPathState(...)` run on the active Tahoe path.
    - H3: `setDataPathState(...)` does run, and the remaining blocker is deeper than this gate.
  - exact probe points:
    - `AirportItlwmSkywalkInterface::getAssocState()`
    - `AirportItlwmSkywalkInterface::setDataPathState(bool)`
  - why these probe points are sufficient:
    - if `getAssocState()` fires at RUN with low-byte `0`, H1 is proven;
    - if `setDataPathState(...)` fires, H3 is proven and this gate is no longer the blocker;
    - if neither fires in the same RUN window even though `setLinkStateInternal(state=2)` does, H2 is proven and the missing seam is above or beside this family gate.
  - why instrumentation is behavior-neutral: both probes are strict logging passthroughs to existing interface implementations; they do not mutate state, arguments, payloads, ordering, or ownership.
  - what exact runtime evidence must be collected:
    - one boot-ready window showing the instrumentation does not perturb startup;
    - one join window showing `ASSOC -> RUN`, `setLinkStateInternal(state=2)`, any `getAssocState()` calls with raw and low-byte values, any `setDataPathState(...)` calls, and the later rollback/DPS reset if the association still fails.
- why this is root cause and not just correlation: not yet proven; this candidate is diagnostic and is intended to distinguish whether the remaining blocker sits exactly at the family assoc/data-path gate or deeper in framework ownership.
- why proposed fix is 1:1 with reference architecture and semantics: it does not change semantics; it only reveals whether the active Tahoe path is entering the same framework gate that the reference architecture uses.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - guessed replay of `setDataPathState(true)` from the driver: rejected; bypasses the reference guard and was already disallowed earlier.
  - another synthetic link/current-BSS replay after RUN: rejected; still state-changing without proving this gate first.
  - treating the garbage upper bits of `getAssocState` as a confirmed ABI bug: rejected; current decomp only proves low-byte semantics.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - Stage 1 request as `DIAGNOSTIC_INSTRUMENTATION`
  - after approval, install without unloading, reboot, run one reproducible join attempt, and collect a filtered sudo log window containing `ASSOC -> RUN`, `setLinkStateInternal`, `getAssocState`, `setDataPathState`, and the later teardown.

## ANOMALY
- id: A-ASSOC-SKYWALK-LINKEVENT-DISPATCH-014
- status: CORRELATED
- symptom: the real Tahoe `ASSOC -> RUN` edge reaches local `setLinkStateInternal(state=2)`, but current-link ownership still collapses before any observed framework association/data-path callback reaches local overridable hooks.
- first visible manifestation: on `CR-078` after-fix runtime, the failing join window shows `ASSOC -> RUN`, `setLinkStateInternal state=2 debounceTimeout=0 debounce=0 code=0 connectionId=0`, immediate `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`, and only then `setLinkStateInternal ret=0x1`.
- expected system behavior: the state-2 link-up path should drive the current Tahoe skywalk event chain into the normal system-facing link-status/data-path callbacks before any current-link getter still sees `not associated`.
- actual behavior: the first public `getBSSIDData()` failure happens inside the active `setLinkStateInternal(state=2)` call before it returns, while previous diagnostics already proved that `getAssocState()` and `setDataPathState(...)` do not run on this up edge.
- divergence point: unresolved current Tahoe skywalk event-dispatch seam downstream of `IO80211InfraInterface::setLinkStateInternal(state=2)` and upstream of any local overridable link-status/data-path callback.
- evidence:
  - runtime logs: `CR-078-afterfix-assoc-gate-window-20260424-073603.log` shows `setLinkStateInternal state=2 ... currentStatus=0x3`, immediate `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`, and only then `setLinkStateInternal ret=0x1`, proving the first failure is born inside the state-2 link-up call and not in our later `postMessage(...)` / extra `reportLinkStatus(...)` tail.
  - runtime logs: the same `CR-078` evidence contains no `getAssocState()` or `setDataPathState(...)` lines in the RUN window, so the active failure is earlier than the previously instrumented family assoc/data-path gate.
  - decomp / disassembly: current Tahoe `IO80211Family` x86_64 `IO80211InfraInterface::getAssocState()` at `0x1de742` returns only `(*(self+0x128)+0x180) & 1` in `AL`, so the earlier garbage upper bits in our logs are not a semantic ABI bug; only the low bit matters.
  - decomp / disassembly: current Tahoe `IO80211Family` x86_64 `IO80211InfraInterface::setLinkStateInternal(state=2)` at `0x1d7edb` calls `IOSkywalkNetworkInterface::reportLinkStatus(3, 0x80)` (relocation `001d7ef0`) and `IOSkywalkNetworkInterface::reportDataBandwidths(...)` (relocation `001d7f13`) before returning.
  - decomp / disassembly: current Tahoe `IOSkywalkFamily` x86_64 `IOSkywalkNetworkInterface::reportDataBandwidths(...)` at `0x0000fbd0` only normalizes zero inputs and calls `_ifnet_set_bandwidths`, so it is not a plausible current-BSS / association owner.
  - decomp / disassembly: current Tahoe `IOSkywalkFamily` x86_64 `IOSkywalkNetworkInterface::reportLinkStatus(...)` at `0x000103a4` tail-jumps to `IOSkywalkNetworkInterface::reportEventType(...)` at `0x0000f7b6`; for link event codes `0xe0060100/0xe0060102`, `reportEventType(...)` executes `call *0x20(%rax)` on the interface vtable and `_thread_call_enter1(expansionData+0x28, eventCode)`.
  - decomp / disassembly: the state-2 `setLinkStateInternal(...)` path does not consult `debounceTimeout`, `code`, or `connectionId` before `reportLinkStatus(...)` / `reportDataBandwidths(...)`, so changing the local association up-call from `..., 0, false, 0, 0` to `..., 1, false, 0, 0` cannot explain the observed first failure.
- candidate causes:
  - hypothesis: the active skywalk link-event dispatch never reaches local overridable callbacks such as `reportDetailedLinkStatus(...)`, `updateLinkStatus(...)`, `updateLinkStatusGated(...)`, or `reportDataPathEvents(...)`; the break is earlier, inside the synchronous `reportEventType(...)` callback and/or its queued worker.
  - hypothesis: the active skywalk link-event dispatch does reach one or more of those local callbacks, and the remaining blocker is later in current-BSS / data-path ownership.
- rejected causes:
  - raw upper bits in `getAssocState()` as the blocker: rejected; current Tahoe binary proves only low bit semantics, and the upper bits are just register garbage.
  - `reportDataBandwidths(...)` as the current-link owner: rejected; exact `IOSkywalkFamily` recovery shows it only reaches `_ifnet_set_bandwidths`.
  - forcing association up-call argument `1` instead of `0`: rejected as a causal candidate for this failure window; current state-2 path does not read that argument before the failing link-event dispatch.
- notes:
  - this anomaly supersedes the weaker earlier theory that the root seam was the old recovered `reportLinkStatus -> vtable[4]` chain alone; the current Tahoe `IOSkywalkFamily` recovery shows the exact producer is `reportLinkStatus -> reportEventType -> {vtable+0x20, thread_call_enter1}`.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-SKYWALK-LINKEVENT-DISPATCH-014
- symptom: the first post-RUN current-link failure happens inside the active state-2 skywalk link-event dispatch before any previously observed family assoc/data-path hook fires.
- expected system behavior: the current Tahoe `reportLinkStatus -> reportEventType` link-event path should either reach local system-facing link-status/data-path callbacks or we must prove that the break is still earlier than any local override seam.
- actual behavior: current runtime proves only `setLinkStateInternal(state=2)` plus the immediate internal `getBSSIDData()` failure; current binary recovery proves the active producer path is already inside `IOSkywalkFamily` link-event machinery, but we still do not know whether it ever reaches local overridable callbacks on the live interface object.
- exact divergence point: unresolved current Tahoe skywalk link-event dispatch between `IOSkywalkNetworkInterface::reportEventType(...)` and any local overridable callback seam.
- evidence from runtime: `CR-078` after-fix logs show the failure occurs before `setLinkStateInternal(state=2)` returns and after earlier diagnostics already ruled out `getAssocState()/setDataPathState(...)` on the same up edge.
- evidence from decomp: exact Tahoe 26.3 x86_64 disassembly now proves `setLinkStateInternal(state=2)` internally executes `reportLinkStatus(...)` and `reportDataBandwidths(...)`; `reportLinkStatus(...)` now resolves to `reportEventType(...)`, which synchronously calls `*vtable[0x20]` and queues `_thread_call_enter1(...)`.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated:
    - H1: the active skywalk link-event dispatch never reaches local `reportDetailedLinkStatus(...)`, `updateLinkStatus(...)`, `updateLinkStatusGated(...)`, or `reportDataPathEvents(...)`; the break is earlier than any local callback seam.
    - H2: one or more of those local callbacks does fire on the real RUN edge, and the blocker is later than the skywalk event-dispatch seam.
    - H3: `reportDataPathEvents(...)` fires, proving the event chain reaches deeper data-path machinery and shifting the blocker beyond this diagnostic window.
  - exact probe points:
    - `AirportItlwmSkywalkInterface::reportDetailedLinkStatus(if_link_status const *)`
    - `AirportItlwmSkywalkInterface::updateLinkStatus()`
    - `AirportItlwmSkywalkInterface::updateLinkStatusGated()`
    - `AirportItlwmSkywalkInterface::reportDataPathEvents(UInt, void *, unsigned long, bool)`
  - why these probe points are sufficient:
    - if any of these probes fire in the failing RUN window, then the current skywalk link-event dispatch does reach local overridable callbacks and the remaining blocker is later than this seam;
    - if none of them fires while `setLinkStateInternal(state=2)` and the immediate internal `getBSSIDData()` failure still occur, then the break is proven earlier than any local callback seam, inside `reportEventType(...)` synchronous/queued processing or another non-overridable framework path;
    - `reportDataPathEvents(...)` firing would additionally prove that the data-path event chain is alive and move the blocker even deeper.
  - why instrumentation is behavior-neutral: every added hook is a strict logging passthrough to the existing base implementation; no arguments, ordering, callbacks, payloads, or state are changed.
  - what exact runtime evidence must be collected:
    - one boot window showing the added hooks do not perturb startup;
    - one reproducible join window showing `ASSOC -> RUN`, `setLinkStateInternal(state=2)`, any `reportDetailedLinkStatus(...)`, `updateLinkStatus(...)`, `updateLinkStatusGated(...)`, or `reportDataPathEvents(...)` callbacks, the immediate `getBSSIDData()` failure if it persists, and the later rollback.
- why this is root cause and not just correlation: not yet proven; this candidate is diagnostic and is scoped only to separate the active skywalk event-dispatch seam from the already disproven later assoc/data-path theories.
- why proposed fix is 1:1 with reference architecture and semantics: no behavior is changed; the patch only exposes whether the live Tahoe path reaches the same local callback layer the system architecture provides.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - reviving the old `reportLinkStatus` ordering theory as a fix: rejected; current Tahoe binary proves `setLinkStateInternal(state=2)` already performs the internal `reportLinkStatus(...)` call and the failure happens before our tail code runs.
  - forcing the association up-call timeout/code tuple to match the earlier Apple ready-edge example: rejected; exact state-2 disassembly shows those arguments are not consulted before the failing event-dispatch seam.
  - synthetic current-BSS or data-path replay after RUN: rejected; state-changing and already disallowed without proving the active event-dispatch seam first.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - Stage 1 request as `DIAGNOSTIC_INSTRUMENTATION`
  - after approval, install without unloading, reboot, reproduce one failed `btn-vno` join attempt, and collect a filtered sudo log window containing `ASSOC -> RUN`, `setLinkStateInternal`, `reportDetailedLinkStatus`, `updateLinkStatus`, `updateLinkStatusGated`, `reportDataPathEvents`, the immediate `getBSSIDData()` failure if present, and the later rollback.

## ANOMALY
- id: A-ASSOC-LEGACY-EVENT-PAYLOAD-CONTRACT-015
- status: CONFIRMED_DEVIATION
- symptom: after the real Tahoe `ASSOC -> RUN` edge, the driver emits legacy `APPLE80211_M_LINK_CHANGED` / `APPLE80211_M_BSSID_CHANGED` / `APPLE80211_M_SSID_CHANGED` events with `NULL, 0` payloads, while current Tahoe consumers expect structured payload lengths.
- first visible manifestation: in the post-`CR-083` runtime window at `2026-04-24 17:56:55.495`, airportd logs `Driver Event ... LINK_CHANGED/4`, immediately followed by `Unexpected event payload length for APPLE80211_M_LINK_CHANGED (expected=32, actual=0)` and `Unexpected event payload length for APPLE80211_M_BSSID_CHANGED (expected=24, actual=0)`, then continues with `BSSID_CHANGED/3`, `SSID_CHANGED/2`, and repeated `APPLE80211_IOC_BSSID/SSID -> 0xe0822403`.
- expected system behavior: if the driver emits legacy post-RUN association events, each must carry the Tahoe-compatible payload size/layout the consumer expects; otherwise the event must not be emitted in that malformed form.
- actual behavior: `AirportItlwm::setLinkStateGated(...)` posts legacy `4/3/2` events with `NULL, 0`, which current Tahoe userspace rejects as malformed.
- divergence point: local `AirportItlwm::setLinkStateGated(...)` tail path after `setLinkStateInternal(...)` returns.
- evidence:
  - local code: `AirportItlwmV2.cpp` posts `APPLE80211_M_LINK_CHANGED`, `APPLE80211_M_BSSID_CHANGED`, and `APPLE80211_M_SSID_CHANGED` with `NULL, 0` in `setLinkStateGated(...)`.
  - runtime logs: `CR-084-afterfix-airportd-join-window-20260424-175655.log` proves userspace rejects `LINK_CHANGED` with `expected=32, actual=0` and `BSSID_CHANGED` with `expected=24, actual=0`.
  - decomp: `IO80211Family` `WCLNetManager::getLINK_CHANGED_EVENT_DATA(...)` accepts payload only when message length is exactly `0x20`.
  - local struct proof: `CR-084-link-changed-contract-proof-20260424.txt` shows the current local `apple80211_link_changed_event_data` layout is `sizeof=20`, not `0x20`.
  - local ABI sizes: `apple80211_bssid_data` is `sizeof=12`, while the runtime consumer expects `24`; `apple80211_ssid_data` is `sizeof=40`, while the current event catalog records legacy `APPLE80211_M_SSID_CHANGED` payload size `8`.
- candidate causes:
  - hypothesis: the legacy post-RUN event tail was carried forward from an older contract and never updated for Tahoe's current userspace payload expectations.
  - hypothesis: the current local struct definitions were assumed to be event payloads, but Tahoe uses distinct event-specific layouts for legacy `LINK_CHANGED/BSSID_CHANGED/SSID_CHANGED`.
- rejected causes:
  - the malformed legacy events as the cause of the very first internal `getBSSIDData()` failure inside `setLinkStateInternal(...)`: rejected; those posts happen later, after `setLinkStateInternal` already returned.
  - treating the current local `apple80211_link_changed_event_data`, `apple80211_bssid_data`, or `apple80211_ssid_data` as automatically valid event payloads: rejected by exact size mismatch (`20 != 32`, `12 != 24`, `40 != 8`).
- notes:
  - this is a proven local system-contract defect independent of the earlier primary internal current-link failure. It likely worsens the join collapse seen by airportd, but it does not yet localize the first failure born inside `setLinkStateInternal(...)`.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-LEGACY-EVENT-PAYLOAD-CONTRACT-015
- symptom: the local tail path emits malformed legacy post-RUN `4/3/2` events.
- expected system behavior: the driver should either emit Tahoe-compatible payloads for legacy `LINK_CHANGED/BSSID_CHANGED/SSID_CHANGED`, or stop emitting those legacy events in malformed `NULL,0` form.
- actual behavior: current code emits zero-length payloads that current Tahoe userspace rejects.
- exact divergence point: `AirportItlwm::setLinkStateGated(...)` after `IO80211InfraInterface::setLinkState(...)` returns.
- evidence from runtime: airportd rejects the emitted events with exact expected sizes `32` and `24`, then collapses into repeated `driver not available` current-link queries.
- evidence from source / decomp:
  - `WCLNetManager::getLINK_CHANGED_EVENT_DATA(...)` requires `0x20`
  - local `apple80211_link_changed_event_data` is `20` bytes, not `32`
  - local `apple80211_bssid_data` is `12` bytes, not `24`
  - local `apple80211_ssid_data` is `40` bytes, while the checked event catalog records legacy `SSID_CHANGED` payload `8`
- change class: REFERENCE_ALIGNMENT_FIX
- if REFERENCE_ALIGNMENT_FIX:
  - exact work still required before code change:
    - recover or confirm the exact Tahoe legacy payload layouts for `APPLE80211_M_BSSID_CHANGED` and `APPLE80211_M_SSID_CHANGED`
    - confirm whether current Tahoe still wants legacy `LINK_CHANGED/4` in addition to the newer `LINK_CHANGED (0xd8)` path on the association-up edge
  - why the fix is not submitted yet:
    - emitting fabricated payloads without the exact Tahoe layouts would be another contract violation
    - simply deleting the legacy events is behavior-changing and not yet proven 1:1 with the reference
  - what exact code must eventually change:
    - `AirportItlwm/AirportItlwmV2.cpp`
    - replace the three `postMessage(..., NULL, 0, true)` calls with reference-aligned payload emission or remove them only if reference recovery proves they are not required on Tahoe
- why this is root cause and not just correlation: not yet claimed as the sole root cause of failed association; it is a separately confirmed local contract defect that can independently break userspace event handling after RUN.
- why proposed fix must be 1:1 with reference architecture and semantics: the current defect is precisely a contract mismatch, so only exact Tahoe payload recovery or exact reference-backed removal is acceptable.
- forbidden alternative fixes considered and rejected:
  - stuffing current `apple80211_link_changed_event_data`, `apple80211_bssid_data`, or `apple80211_ssid_data` directly into the legacy events: rejected; exact size mismatches already disprove that shortcut.
  - keeping the current `NULL,0` legacy events and relying on polling only: rejected; current Tahoe userspace explicitly rejects those payloads.
  - deleting the legacy events immediately without reference proof: rejected; could regress compatibility paths that Tahoe still expects.

## ANOMALY
- id: A-ASSOC-REPORTLINKSTATUS-ORDER-016
- status: CONFIRMED_DEVIATION
- symptom: the local Tahoe association-up producer runs `setLinkState(...)` before `reportLinkStatus(...)`, while the recovered Apple low-latency producer body runs `reportLinkStatus(...)` first and only then `setLinkState(...)`.
- first visible manifestation: in the failing `CR-085` `CONTROL_STA_NETWORK` join window at `2026-04-25 09:22:02.958 +0300`, the driver reaches `ASSOC -> RUN`, `setCurrentApAddress(...)`, `setLinkStateInternal(state=2)`, and then enters the local tail `reportLinkStatus(...)` with `mExpansionData->linkStatus` already at terminal `0x3`.
- expected system behavior: the recovered Apple low-latency producer body for the up-edge is `super::setInterfaceEnable(true)` followed by `reportLinkStatus(3, 0x80)`, followed by `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)`.
- actual behavior before the fix: `AirportItlwm::setLinkStateGated(...)` called `IO80211InfraInterface::setLinkState(...)` first with the association-up code inherited from the caller (`0` in the observed window), then emitted legacy `4/3/2` postMessage traffic, and only afterwards called `fNetIf->reportLinkStatus(...)`.
- divergence point: local `AirportItlwm::setLinkStateGated(...)` ordering on the association-up edge.
- evidence:
  - decomp: `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(bool)` runs `IO80211InfraInterface::setInterfaceEnable(bool)` then `reportLinkStatus(3, 0x80)` then `setLinkState(2, 1, false, 0, 0)`.
  - decomp: recovered `IOSkywalkNetworkInterface::reportLinkStatus(...)` updates `mExpansionData->linkStatus` and returns without notification when the requested status already matches the cached `linkStatus`.
  - local source before the fix: `AirportItlwm::setLinkStateGated(...)` called `setLinkState(...)` first and `reportLinkStatus(...)` later.
  - runtime: `CR-085-afterfix-kernel-control_sta_network-window-20260425-092140.log` proves:
    - `pre-setLinkState`: `linkStatus=0x1`
    - inside `setLinkStateInternal(state=2)`: immediate `AirportItlwm::getBSSIDData(): Get failure: APPLE80211_IOC_BSSID: -528342013`
    - `post-setLinkState`: `linkStatus=0x3`
    - `pre-reportLinkStatus`: still `linkStatus=0x3`
    - `post-reportLinkStatus`: still `linkStatus=0x3`
  - runtime: the same window still has no `reportDetailedLinkStatus(...)`, `updateLinkStatus(...)`, or `updateLinkStatusGated(...)` callback before the later malformed legacy events.
- candidate causes:
  - confirmed: the local reversed order lets `setLinkState(...)` mutate the same skywalk carrier state that `reportLinkStatus(...)` later uses for edge detection, so the later `reportLinkStatus(...)` becomes a no-op and suppresses Apple's notification path.
  - secondary confirmed deviation: the local extra legacy postMessage tail between `setLinkState(...)` and `reportLinkStatus(...)` further diverges from the recovered Apple producer body and remains a separate userspace-event defect.
- rejected causes:
  - the low byte of `getAssocState()` as the blocker here: rejected by `CR-078`; `getAssocState()` does not run in the failing window.
  - the late malformed legacy `4/3/2` events as the first cause of the internal `getBSSIDData()` failure: rejected; those events are emitted only after `setLinkStateInternal(...)` already returned.
- notes:
  - `CR-085` closed the missing proof: the later local `reportLinkStatus(...)` is already entering with cached `linkStatus == 3`.
  - `CR-089` after-fix runtime rejects this as the active root cause: the reviewed patch moved `reportLinkStatus(3, 0x80)` before `IO80211InfraInterface::setLinkState(...)` and aligned the up-edge code to `1`, but `AirportItlwm::getBSSIDData()` still fails with `APPLE80211_IOC_BSSID -> 0xe0822403` inside `setLinkStateInternal(state=2)`.
  - this remains a confirmed producer-order deviation, but it is not sufficient to establish WCL current-BSS ownership.
  - the separate FT/`802.11r` UI observation is not this root cause; current kernel scan logs already prove FT-capable BSS entries reach the scan cache.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-REPORTLINKSTATUS-ORDER-016
- symptom: the local association-up producer likely suppresses the Apple skywalk notification edge by calling `setLinkState(...)` before `reportLinkStatus(...)`.
- expected system behavior: the Apple low-latency producer path reaches `reportLinkStatus(3, 0x80)` before `setLinkState(2, 1, false, 0, 0)`.
- actual behavior before the fix: local code called `setLinkState(...)` first and only later `reportLinkStatus(...)`, with extra legacy event traffic in between.
- exact divergence point: `AirportItlwm::setLinkStateGated(...)`
- evidence from runtime:
  - `CR-083/CR-084` join windows show `setLinkStateInternal(state=2)` is entered and fails inside `getBSSIDData()`, but no `reportDetailedLinkStatus/updateLinkStatus/updateLinkStatusGated` callbacks are observed in that window.
  - `CR-085-afterfix-kernel-control_sta_network-window-20260425-092140.log` proves the later local `reportLinkStatus(...)` enters with `linkStatus=0x3` and leaves with `linkStatus=0x3`, so the local tail is already a no-op by recovered Tahoe skywalk semantics.
  - the same `CR-085` window shows `pre-setLinkState linkStatus=0x1`, proving the missing edge existed before the local `setLinkState(...)` call consumed it.
- evidence from decomp:
  - `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(bool)` exact order is `reportLinkStatus(...)` then `setLinkState(...)`
  - `IOSkywalkNetworkInterface::reportLinkStatus(...)` does not emit its notification path when the requested status equals the cached `mExpansionData->linkStatus`
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - exact semantic mismatch between reference and our code:
    - recovered Apple up-edge producer order is `reportLinkStatus(3, 0x80)` then `setLinkState(2, 1, false, 0, 0)`
    - local code consumed the same edge inside `setLinkState(...)` first, then called `reportLinkStatus(...)` only after `linkStatus` was already `3`
    - local code also propagated `0` as the association-up `setLinkState(...)` code, while the recovered Apple producer uses `1`
  - why this is root cause and not just correlation:
    - the recovered Tahoe `reportLinkStatus(...)` logic is explicitly edge-triggered on cached `linkStatus`
    - `CR-085` runtime proves the later local `reportLinkStatus(...)` already sees the terminal cached state and therefore cannot enter its notification path
    - the only state transition between `pre-setLinkState linkStatus=0x1` and `pre-reportLinkStatus linkStatus=0x3` is the local `setLinkState(...)` call itself
  - why proposed fix is 1:1 with reference architecture and semantics:
    - move the association-up `reportLinkStatus(3, 0x80)` call before the local `setLinkState(...)`
    - align the association-up `setLinkState(...)` code to the recovered Apple `1`
    - keep the down path and the separate malformed legacy-event defect unchanged in this batch
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - forcing `reportLinkStatus(...)` twice to manufacture an edge: rejected; duplicate publish without reference producer proof is forbidden by protocol.
  - fixing the malformed legacy `4/3/2` payloads in the same batch: rejected; that is a separate Tahoe contract defect and must not be bundled into the producer-order root-cause fix.
  - inventing FT/`802.11r` gating changes in the same batch: rejected; current scan evidence proves FT-capable BSS ingestion is a separate problem space.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC ABI check
  - Stage 1 request as `SYSTEM_CONTRACT_FIX`
  - after approval, install without unloading, reboot once, reproduce one failed join, and confirm:
    - the up-edge `reportLinkStatus(...)` now runs before local `setLinkState(...)`
    - the later skywalk callback chain appears or the first failure moves later than the old internal `getBSSIDData()` seam

## ANOMALY
- id: A-ASSOC-WCL-LINK-IND-017
- status: REJECTED
- symptom: after `CR-089`, networks remain visible and local association reaches `RUN`, but WCL/CoreWiFi still cannot observe the current BSSID/current network, so manual join to `CONTROL_STA_NETWORK` fails and UI waits for delayed password/failure completion.
- first visible manifestation: `CR-089` after-fix runtime reaches `ASSOC -> RUN`, then `AirportItlwm::getBSSIDData()` returns `APPLE80211_IOC_BSSID -> 0xe0822403` inside `setLinkStateInternal(state=2)`.
- expected system behavior: the association link event must publish the structured Tahoe `LINK_CHANGED` WCL indication (`0xd8`, 16-byte payload) before WCL/current-link probes depend on current-BSS ownership.
- actual behavior: local Tahoe association-up code publishes current AP address and calls `reportLinkStatus(...)` / `IO80211InfraInterface::setLinkState(...)`, but it never posts the reference `0xd8` WCL link indication. It later emits legacy `4/3/2` messages with zero payloads.
- divergence point: local `AirportItlwm::setLinkStateGated(...)` on the real association-up edge lacks the reference `AppleBCMWLANNetAdapter::handleLink(...) -> postMessage(..., 0xd8, payload, 0x10, 1)` producer.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-089-afterfix-control_sta_network-join-edge-20260425-100409.log` shows the corrected CR-089 order (`pre-reportLinkStatus`, `post-reportLinkStatus`, `pre-setLinkState`) followed by the same immediate current-BSSID failure inside `setLinkStateInternal`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-089-afterfix-airportd-currentlink-fail-20260425-100409.log` shows userspace/current-link still sees `APPLE80211_IOC_BSSID -> 0xe0822403`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-089-afterfix-user-control_sta_network-incomplete-ui-20260425-1035.log` (`sha256 ca7362d454fbbe5318ed2cd2311c8ae9480bb9e80577265839f2f2b202971222`) preserves the post-CR-089 visible-driver state and continued idle current-link polling after the failed join report.
  - decomp: `docs/reference/AppleBCMWLAN_Core_decompiled.c`, `AppleBCMWLANNetAdapter::handleLink(wl_event_msg_t*)`, builds a 16-byte payload from BSSID bytes, link-state bit, interface type, reason code, and zero reserved field, then calls controller `postMessage(..., 0xd8, &payload, 0x10, 1)`.
  - decomp: `docs/reference/IO80211Family_decompiled.c`, `WCLNetManager::linkUp(void*)`, consumes that payload and calls `WCLNetManager::updateBss(..., *(payload+0x0c), ...)`; this is the current-BSS owner path that must run before WCL current-link probes can succeed.
  - decomp: `docs/reference/IO80211Family_decompiled.c`, the current BSSID helper calls `IO80211Glue::sendIOUCToWcl(... selector=9 ...)` and only falls back to the local implementation when WCL returns not-implemented; the observed `0xe0822403` therefore means the WCL current-BSS owner is still not associated.
  - docs: `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/86_concrete_event_payload_maps_checked.yaml` records `LINK_CHANGED` code `0xd8`, payload size `0x10`, producer `AppleBCMWLANNetAdapter::handleLink`, and consumer `WCLNetManager::linkUp`.
- candidate causes:
  - confirmed: the reference WCL link indication producer is missing on the local Tahoe association-up edge.
  - secondary confirmed deviation: local legacy `APPLE80211_M_LINK_CHANGED/BSSID_CHANGED/SSID_CHANGED` zero-payload posts still violate Tahoe userspace payload contracts and happen after the first current-link failure.
- rejected causes:
  - CR-089 `reportLinkStatus` ordering as the active root cause: rejected by after-fix runtime; the order is corrected but the current-BSS owner remains unset.
  - local public `getBSSID/getSSID` fallback as a fix: rejected because the active helper routes to WCL and returns WCL's `0xe0822403`, not a local fallback miss.
  - changing scan timers or UI filtering to fix the join failure: rejected for this symptom; the join reaches local `RUN` and fails on current-BSS ownership.
- confirmed deviation: reference posts structured `0xd8` WCL link indication from the association link event; local code does not.
- root cause: WCL current-BSS state is never established before `setLinkStateInternal(state=2)` and userspace current-link probes execute, so WCL/CoreWiFi keep reporting driver/current-network unavailable even after radio association succeeds.
- fix: add the Tahoe association-up `0xd8` WCL link indication using the recovered 16-byte payload, sourced from the real `ic_bss` BSSID after `syncTahoeCurrentApAddress(false, true)` and before `IO80211InfraInterface::setLinkState(...)`; suppress the malformed Tahoe association-up legacy `4/3/2` zero-payload tail for this edge.
- verification: `git diff --check`; Tahoe build; Stage 1 structural request; after approval install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, and verify that `0xd8` is posted before `setLinkStateInternal`, current BSSID probes stop returning `0xe0822403`, and failure moves to EAPOL/key/data if another blocker remains.
- notes: CR-090 after-fix runtime rejects this anomaly as the sufficient current join root cause. The `0xd8` producer fires on the real association-up edge and WCL receives it, but WCL immediately calls `cmdIouc(0x1b1)` / `getWCL_BSS_INFO(...)`; the returned object has the BSSID at offset `0x00` while `WCLNetManager::updateBss(...)` checks offset `0x29`, logs `Bssid address is null`, forces link down, and current-network probes keep failing. The missing-`0xd8` deviation was real and structurally corrected, but the remaining CR-090 join blocker is A-ASSOC-WCL-BSS-INFO-LAYOUT-019.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-LINK-IND-017
- symptom: after real association to `CONTROL_STA_NETWORK`, WCL/CoreWiFi current-link state remains unavailable and join fails after delayed UI/user-space completion.
- expected system behavior: the reference association link producer posts `LINK_CHANGED` code `0xd8` with a 16-byte payload to the primary infra interface, allowing `WCLNetManager::linkUp(...)` to run `updateBss(...)` and establish current-BSS ownership.
- actual behavior: local Tahoe code reaches `RUN` and calls `setLinkState(...)`, but never posts the `0xd8` WCL link indication; WCL current-BSS probes still return `0xe0822403`.
- exact divergence point: `AirportItlwm::setLinkStateGated(...)` on `kIO80211NetworkLinkUp`.
- evidence from runtime: CR-089 after-fix logs prove the corrected `reportLinkStatus` order and the persistent immediate `getBSSIDData()` failure inside `setLinkStateInternal(state=2)`; no local `0xd8` post exists in source/logs.
- evidence from decomp: `AppleBCMWLANNetAdapter::handleLink(wl_event_msg_t*)` posts message `0xd8` with payload length `0x10`; `WCLNetManager::linkUp(void*)` consumes that payload and calls `updateBss(...)`; the IO80211 current-BSSID helper asks WCL first and returns the observed `0xe0822403` when WCL has no current BSS.
- exact semantic mismatch between reference and our code: reference has a structured WCL link indication producer for the association link event; local code substitutes only current-AP cache publication, skywalk link-status/setLinkState calls, and malformed legacy zero-payload events.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the failing getter path is WCL-owned, the reference `0xd8` consumer is the WCL `updateBss(...)` current-BSS owner, and CR-089 proved that correcting the skywalk link-status order alone does not establish WCL current BSS.
- why proposed fix is 1:1 with reference architecture and semantics: the patch adds the exact recovered postMessage code (`0xd8`), exact payload size (`0x10`), field order, async flag (`1`), and association-up lifecycle boundary from `AppleBCMWLANNetAdapter::handleLink(...)`.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - synthetic current-network getter success: rejected; WCL, not local getters, owns the active current-BSS path.
  - another delayed/replayed `setLinkState(...)` or `reportLinkStatus(...)`: rejected; CR-089 already proved this path is insufficient and replay without the producer is forbidden.
  - fabricating `setWCL_LINK_STATE_UPDATE(...)` payload: rejected; that carrier ABI remains unrecovered and was not invoked in runtime.
  - keeping Tahoe association-up legacy `4/3/2` zero-payload events in the same edge: rejected; the exact reference `handleLink(...)` producer posts `0xd8`, while Tahoe userspace rejects the local zero-payload legacy events.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - create CR-090 Stage 1 request with exact patch artifact and diff hash
  - after `APPROVED_FOR_AFTER_FIX_RUNTIME`, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs for `postTahoeWclLinkInd`, `setLinkStateInternal`, current-link getter return codes, `setCIPHER_KEY`, EAPOL, and data-path counters.

## ANOMALY
- id: A-WCL-SCAN-CANDIDATE-REAP-RSSI-018
- status: CORRELATED
- symptom: UI scan list is incomplete after CR-089; some `802.11r` networks and some ordinary networks are missing even though the radio scan sees them.
- first visible manifestation: user reports missing `802.11r` and ordinary SSIDs from the UI list while the driver remains visible and networks are generally visible.
- expected system behavior: every valid fresh BSS delivered through the WCL scan-result path should remain in the WCL/CoreWiFi candidate cache long enough for UI selection and manual join.
- actual behavior: runtime/CoreCapture scan logs show `CONTROL_STA_NETWORK`, `CONTROL_STA_NETWORK 802.11r`, `Xirosterni 802.11r`, `wHouse`, `strlnk`, and other BSS entries being added/updated, but later `IO80211ScanCacheStore Reaping` removes candidates at ages around 34-42 seconds; WCL can then report `No such network: "CONTROL_STA_NETWORK"`.
- divergence point: unresolved WCL scan-result metadata/candidate-cache plane after local `APPLE80211_M_WCL_SCAN_RESULT(0xc9)` publication and before CoreWiFi/UI candidate retention.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-089-afterfix-scan-cache-rich-ui-missing-20260425-100650.log` contains FT-capable and ordinary BSS entries in the scan cache despite UI absence.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-089-afterfix-wcl-control_sta_network-no-such-network-20260425-100616.log` shows WCL later reporting `No such network: "CONTROL_STA_NETWORK"`.
  - runtime logs: CoreCapture scan rows in CR-089 evidence show `rssi=0 snr=0` for WCL-visible entries, while local BSS selection logs use non-zero `ni_rssi`; this correlates with candidate reaping/filtering.
  - source: `buildTahoeWclScanResultPayload(...)` currently fabricates Tahoe `0xc9` metadata locally; exact RSSI/SNR/metadata ABI is not yet fully recovered.
- candidate causes:
  - hypothesis: the `0xc9` scan-result metadata layout still has an ABI/offset mismatch for signal quality or validity fields, causing WCL/CoreWiFi to ingest entries with `rssi=0 snr=0` and later reap/filter them.
  - hypothesis: candidate validity flags, not just RSSI/SNR, are incomplete in the local Tahoe scan metadata.
- rejected causes:
  - radio scan absence: rejected; the kernel/CoreCapture scan cache contains the missing SSIDs.
  - `802.11r` itself as a hard local filter: rejected; FT-capable entries reach the scan cache before later consumer-plane loss.
  - scan timer/retry adjustment as a fix: rejected; the defect is after successful scan-result ingestion, not lack of scan activity.
- confirmed deviation: not yet confirmed; exact `0xc9` metadata ABI recovery is still required.
- root cause: not yet established; this remains a separate scan/UI retention issue and must not be bundled with the confirmed association current-BSS fix.
- fix: none in this batch.
- verification: recover reference `APPLE80211_M_WCL_SCAN_RESULT(0xc9)` metadata writes from decomp, then compare exact field offsets against `TahoeWclBeaconMetaData` before proposing any scan/UI patch.

## ANOMALY
- id: A-ASSOC-WCL-BSS-INFO-LAYOUT-019
- status: FIX_IMPLEMENTED
- symptom: after CR-090, the driver remains visible and local radio association to `CONTROL_STA_NETWORK` reaches `RUN`, but WCL/CoreWiFi immediately tears the link down; the password prompt and failure message are delayed, and current-network/BSSID probes still fail.
- first visible manifestation: `2026-04-25 11:05:08.763132+0300` in CR-090 after-fix runtime: `[wcl] updateBss@2491:Bssid address is null` immediately after `postTahoeWclLinkUpInd` and `getWCL_BSS_INFO`.
- expected system behavior: `WCLNetManager::updateBss(...)` must receive `cmdIouc(0x1b1)` output as a `0x844`-byte `BeaconMetaData + IE` object: metadata header at `0x00..0x43`, BSSID at `0x29`, and IE pointer at `0x44`, so `updateOrAddBeacon(...)` can return a beacon and `setCurrentBSS(...)` can establish WCL current-BSS ownership.
- actual behavior: local `getWCL_BSS_INFO(...)` zeroes/fills only a `0x84` object with a guessed legacy layout: BSSID at `0x00`, SSID length at `0x10`, SSID at `0x11`, IE length at `0x7c`, and IE bytes capped to six bytes at `0x7e`. Therefore offset `0x29` is zero for `CONTROL_STA_NETWORK`, and WCL treats the current BSS as null.
- divergence point: `AirportItlwmSkywalkInterface::getWCL_BSS_INFO(apple80211_beacon_msg *)` output layout for WCL IOC `0x1b1`.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-090-afterfix-control_sta_network-boot-join-20260425-1107.log` lines `37148..37178` show `0xd8` posted/delivered, `getWCL_BSS_INFO bssid=50:4f:3b:cd:dd:67`, then `[wcl] updateBss@2491:Bssid address is null` and WCL link-down data.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-090-afterfix-wcl-updatebss-bssinfo-null-20260425-110508.log` (`sha256 1398fcbc83855157171412448951121e0eeac1e5cbedc6e056cdbbc4ebbcf056`) is the focused failure window.
  - runtime logs: the CR-090 hexdump begins `50 4f 3b cd dd 67 ... 07 4f 70 65 6e 57 72 74 ...`, proving local code placed BSSID at `0x00` and SSID at `0x11`; by the reference check, BSSID must be at `0x29`.
  - decomp: `/Users/bob/Projects/itlwm/docs/reference/IO80211Family_decompiled.c:6985` shows `WCLNetManager::linkUp(...)` calls `updateBss(..., *(payload+0x0c), ...)`; it does not pass the `0xd8` payload BSSID to `updateBss`.
  - decomp/disasm: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-091-WCLNetManager-updateBss-disasm-20260425.txt` (`sha256 d177d6164f8244fb93628da98f6763206a63b5f5556fc9cac8402cee8f2167e2`) proves `updateBss(...)` allocates/passes output length `0x844`, checks `memcmp(out+0x29, ether_null, 6)`, calls `WCLScanCacheStore::updateOrAddBeacon(out, out+0x44)`, then calls `setCurrentBSS(...)`.
  - decomp: `/Users/bob/Projects/itlwm/docs/reference/AppleBCMWLAN_Core_decompiled.c:133313` shows `AppleBCMWLANCore::getWCL_BSS_INFO(apple80211_beacon_msg*)` delegates non-null output to the net-adapter implementation; the consumer ABI is fixed by `WCLNetManager::updateBss(...)`.
  - source: `AirportItlwm/AirportItlwmV2.cpp` already uses the same recovered `0x44 + 0x800` Tahoe `BeaconMetaData + IE` layout for WCL scan result `0xc9`.
- candidate causes:
  - confirmed: local `getWCL_BSS_INFO(...)` uses an incompatible `0x84` guessed layout and cannot satisfy WCL's `0x1b1` current-BSS ABI.
  - insufficient data for this fix: separate scan/UI retention defect A-WCL-SCAN-CANDIDATE-REAP-RSSI-018 may still make some networks disappear, but it is downstream/separate from the immediate `updateBss` null-BSSID tear-down.
- rejected causes:
  - missing `0xd8` producer as the current CR-090 root cause: rejected by CR-090 runtime because `0xd8` is posted and delivered before the failure.
  - malformed `0xd8` BSSID payload as the cause of `Bssid address is null`: rejected by decomp; `linkUp(...)` passes only `payload+0x0c` reason into `updateBss(...)`, and `updateBss(...)` fetches current BSS through `cmdIouc(0x1b1)`.
  - radio association failure before WCL: rejected; local logs show `ASSOC -> RUN` and `associated with 50:4f:3b:cd:dd:67 ssid CONTROL_STA_NETWORK channel 100`.
- confirmed deviation: WCL expects `0x1b1` BSS info as the same `BeaconMetaData + IE` ABI used by WCL scan cache ingestion; local `getWCL_BSS_INFO(...)` publishes a smaller and offset-incompatible layout.
- root cause: WCL rejects the current-BSS object before `updateOrAddBeacon(...)` / `setCurrentBSS(...)`, so the system tears down the just-associated link and keeps current-network/BSSID unavailable.
- fix: implemented locally by expanding `apple80211_beacon_msg` to `0x844` and making `AirportItlwmSkywalkInterface::getWCL_BSS_INFO(...)` fill the recovered Tahoe `BeaconMetaData + IE` ABI: IE length `0x00`, Apple channel spec `0x04`, SSID `0x06`, SSID length `0x26`, primary channel `0x27`, BSSID `0x29`, RSSI `0x30`, beacon interval `0x38`, capability `0x3a`, flags `0x40`, raw IE bytes `0x44`.
- verification: `git diff --check` passed; Tahoe build passed with BootKC symbol verification (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-091-build-20260425-wcl-bss-info-layout.txt`, sha256 `0345abec173c385088fef65a1f6d445f96deba4c485763bf966807292558f9f3`, built binary UUID `773EF963-FDFD-37F2-8CEA-8DE63A5D0ADF`, sha256 `13a3ffdf9750f29aeb5f8e0d439c6bbe103a840a3dcf370451008cfcb4caf244`); CR-091 Stage 1 request pending. After approval install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, and verify the CR-090 failure line `Bssid address is null` disappears, the `getWCL_BSS_INFO` hexdump has non-null BSSID at `0x29`, and any remaining failure moves past WCL current-BSS establishment.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-BSS-INFO-LAYOUT-019
- symptom: CR-090 reaches local association `RUN`, then WCL immediately logs `updateBss@2491:Bssid address is null`, forces link-down, and join fails with delayed UI completion.
- expected system behavior: on `cmdIouc(0x1b1)`, `getWCL_BSS_INFO(...)` returns the current BSS as a `0x844` `BeaconMetaData + IE` object with BSSID at `0x29` and IEs at `0x44`.
- actual behavior: local `getWCL_BSS_INFO(...)` returns a `0x84` guessed object with BSSID at `0x00`, so the reference consumer's `out+0x29` null-BSSID guard fires.
- exact divergence point: `AirportItlwmSkywalkInterface::getWCL_BSS_INFO(apple80211_beacon_msg *)`, backed by `include/Airport/apple80211_var.h::apple80211_beacon_msg`.
- evidence from runtime: CR-090 CONTROL_STA_NETWORK runtime posts/delivers `0xd8`, calls local `getWCL_BSS_INFO`, dumps BSSID at `0x00`, then WCL logs `Bssid address is null` and emits link-down state at `11:05:08.763132..11:05:08.763204`.
- evidence from decomp: `WCLNetManager::updateBss(...)` disassembly allocates/passes `0x844`, checks `memcmp(out+0x29, ether_null, 6)`, passes `out` and `out+0x44` to `WCLScanCacheStore::updateOrAddBeacon(...)`, then calls `setCurrentBSS(...)`.
- exact semantic mismatch between reference and our code: the reference consumer ABI is `BeaconMetaData + IE`; local code publishes a different legacy layout and too-small buffer definition for the same `0x1b1` output.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the failing WCL log is reached immediately after `cmdIouc(0x1b1)` succeeds; the disassembly shows that exact log is guarded solely by `out+0x29` being all zero; the runtime hexdump proves our BSSID is at `out+0x00`, not `out+0x29`.
- why proposed fix is 1:1 with reference architecture and semantics: it does not add events, retries, delays, fallback getters, or state forcing; it only makes the existing `getWCL_BSS_INFO` producer satisfy the exact consumer layout, size, and IE pointer offsets recovered from `WCLNetManager::updateBss(...)`.
- files/functions to modify:
  - `include/Airport/apple80211_var.h`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - modifying or replaying `0xd8`: rejected; CR-090 proves it already fires, and `linkUp(...)` does not pass the `0xd8` BSSID to `updateBss(...)`.
  - fabricating current-network getter success: rejected; it would mask WCL current-BSS ownership instead of satisfying `updateBss(...)`.
  - retrying/delaying join or password UI: rejected; the immediate failure is deterministic ABI rejection.
  - bundling the scan/UI reaping fix: rejected; A-WCL-SCAN-CANDIDATE-REAP-RSSI-018 is correlated but not yet decomp-confirmed as root cause.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - create CR-091 Stage 1 request with exact diff artifact and hashes
  - after `APPROVED_FOR_AFTER_FIX_RUNTIME`, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs around `postTahoeWclLinkUpInd`, `getWCL_BSS_INFO`, `updateBss`, `setCurrentBSS`, current-network/BSSID getters, EAPOL/key, and data path.

## ANOMALY
- id: A-ASSOC-WCL-CONNECT-COMPLETE-020
- status: FIX_IMPLEMENTED
- symptom: after CR-091, the driver is visible and the UI scan list is complete, but manual join to `CONTROL_STA_NETWORK` still fails; the password prompt and failure completion are delayed.
- first visible manifestation: `2026-04-25 11:46:16.256945+0300` in CR-091 after-fix runtime: WCL enters `NET_MANAGER_STATE_WAITING_FOR_CONNECT_COMPLETE` after local `ASSOC -> RUN`, but no WCL connect-complete producer runs before WCL aborts with `1009/1007`.
- expected system behavior: after successful association and current-BSS establishment, the reference producer `AppleBCMWLANJoinAdapter::sendConnectComplete()` posts `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT` (`0xd5`) with a `0xa4` payload to the primary infra interface.
- actual behavior: local Tahoe association-up code posts the recovered link-up/current-BSS path, and `getWCL_BSS_INFO(...)` now satisfies WCL, but it never posts the reference WCL connect-complete event. WCL therefore remains in `WAITING_FOR_CONNECT_COMPLETE` until join abort/timeout.
- divergence point: local `AirportItlwm::setLinkStateGated(...)` on the real association-up edge lacks the reference connect-complete producer that follows successful current-BSS retrieval in `AppleBCMWLANJoinAdapter`.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-afterfix-control_sta_network-current-boot-window-20260425-1207.log` (`sha256 af347f020ea823eabbf283ebbb6dfa041c5e7906ccb6f1a0a8654b92d1417310`) captures the current boot/join window.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-afterfix-control_sta_network-wcl-connect-complete-concise-20260425-1207.log` (`sha256 3b0ed9136001aad7fe699a2c8a64ac31577e3b339088787209659d8157bd16ad`) shows `ASSOC -> RUN`, `getWCL_BSS_INFO bssid=50:4f:3b:cd:dd:67`, `WCL Joined Bss`, transition to `NET_MANAGER_STATE_WAITING_FOR_CONNECT_COMPLETE`, then `JOIN_ABORT_REQ`, `sendWCLJoinDone lastStatusCode=1009 extendedCode=1007`, and `-3905 tmpErr`.
  - runtime logs: the same concise artifact shows repeated `CWEAPOLClient ... failed to retrieve 8021X state (2)` and no successful `setCIPHER_KEY` path before WCL aborts.
  - decomp/disasm: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-AppleBCMWLAN-sendConnectComplete-disasm-20260425.txt` (`sha256 4c88f8b10cbcf8c670aea24726e2e8de14ebdc0792ef0ed54e5e2b8bcf27378`) proves `sendConnectComplete()` gates on pending/not-sent/status-valid state, builds a `0xa4` payload, and calls `IO80211Controller::postMessage(primaryIface, 0xd5, payload, 0xa4, 1)`.
  - decomp: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-IO80211Family-WCLJoinManager-connectCompleteEventHandler-20260425.txt` (`sha256 8adc410a4f7812465daac8b8df2bdc757b40cabd47e199d05cedfb8cb447c38d`) proves WCLJoinManager's connect-complete handler accepts only payload length `0xa4` and feeds CommonFsm event `5`.
  - decomp: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-IO80211Family-WCLNetManager-connectComplete-decomp-20260425.txt` (`sha256 96e513d08c83c3c3e6e6ec2c7d263e2aa80cc03da7aca8244079bff918d84b00`) proves `WCLNetManager::connectComplete(...)` updates link-state/protect-IP state and posts the link-up-done IOUC path.
  - strings: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-IO80211Family-connect-complete-strings-20260425.txt` (`sha256 a4767924d604748803064753909e1385b39306f23b51180ee6d414fbd47d3823`) contains `NET_MANAGER_EVENT_CONNECT_COMPLETE`, `JOIN_MANAGER_EVENT_JOIN_CONNECT_COMPLETE`, and `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT`.
  - source absence proof: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-local-absence-connect-complete-producer-20260425.txt` (`sha256 0f6c80c8d279ded0da5785d4ee8e51646e8b27e5e5105ede171c73837bf1e3a8`) shows the restore worktree has no local WCL connect-complete producer.
- candidate causes:
  - confirmed: the reference WCL connect-complete event is missing from the local association-up lifecycle after current-BSS establishment.
  - correlated but not fixed here: WCL scan cache still stores `rssi=0 snr=0` and later reaps entries; user's current report says the UI list is complete, so this is not the active join blocker for this batch.
  - correlated but not fixed here: WCL external PMK ownership logs `key_len=0`; current runtime fails before any successful EAPOL/key path, so this remains downstream until connect-complete is delivered.
- rejected causes:
  - CR-091 BSS-info layout as the remaining blocker: rejected by after-fix runtime; `Bssid address is null` is gone, `getWCL_BSS_INFO` returns BSSID at the WCL-accepted offset, and `WCL Joined Bss` is logged.
  - scan/UI candidate absence as the current blocker: rejected for this report; the user reports a full UI list, and logs show `CONTROL_STA_NETWORK` candidates delivered.
  - another retry, timer, delay, or legacy event replay: rejected; protocol requires producer-side reference proof, and the reference producer is the `0xd5/0xa4` connect-complete event.
- confirmed deviation: reference posts WCL connect-complete `0xd5` with `0xa4` payload after successful BSS-info/current-BSS completion; local code has no such producer.
- root cause: WCL never receives the connect-complete event required to leave `NET_MANAGER_STATE_WAITING_FOR_CONNECT_COMPLETE`, so it aborts the join and userspace receives `-3905 tmpErr` after the observed delay.
- fix: implemented locally by adding the recovered `apple80211_wcl_connect_complete_event` `0xa4` payload layout and posting `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT` (`0xd5`) on the Tahoe association-up edge only when the local radio state is `IEEE80211_S_RUN` and `ic_bss` is present. The payload carries real associated BSSID in the first candidate record and success status/reason zero, matching the reference success-state producer shape.
- verification: pending Stage 1 build/review. Required checks: `git diff --check`, Tahoe build, BootKC symbol check, CR-093 Stage 1 request. After approval, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then verify `postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4`, `NET_MANAGER_EVENT_CONNECT_COMPLETE`, absence of `1009/1007` connect-complete timeout, and movement into EAPOL/key or data path.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-CONNECT-COMPLETE-020
- symptom: CR-091 makes networks visible and establishes WCL current BSS, but manual join still fails with delayed UI completion and `-3905 tmpErr`.
- expected system behavior: after successful local association/current-BSS retrieval, reference `AppleBCMWLANJoinAdapter::sendConnectComplete()` posts message `0xd5` with payload length `0xa4` and async flag `1`.
- actual behavior: local code reaches `ASSOC -> RUN`, `getWCL_BSS_INFO` succeeds, and WCL logs `WCL Joined Bss`, but WCL remains in `NET_MANAGER_STATE_WAITING_FOR_CONNECT_COMPLETE` until join abort.
- exact divergence point: `AirportItlwm::setLinkStateGated(...)` association-up path after the recovered Tahoe link/current-BSS publication.
- evidence from runtime:
  - `CR-093-afterfix-control_sta_network-wcl-connect-complete-concise-20260425-1207.log`: `ASSOC -> RUN`, `NET_MANAGER_EVENT_LINK_UP -> WAITING_FOR_CONNECT_COMPLETE`, `getWCL_BSS_INFO bssid=50:4f:3b:cd:dd:67`, then `NET_MANAGER_EVENT_LEAVE_NETWORK`, `JOIN_ABORT_REQ`, `sendWCLJoinDone 1009/1007`, and `-3905 tmpErr`.
  - `CR-093-local-absence-connect-complete-producer-20260425.txt`: local source has no `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT`, `WCL_CONNECT_COMPLETE`, `connectComplete`, or `0xd5` WCL producer.
- evidence from decomp:
  - `CR-093-AppleBCMWLAN-sendConnectComplete-disasm-20260425.txt`: reference producer posts `0xd5`, length `0xa4`, async `1`.
  - `CR-093-IO80211Family-WCLJoinManager-connectCompleteEventHandler-20260425.txt`: WCLJoinManager accepts only `0xa4` and processes event `5`.
  - `CR-093-IO80211Family-WCLNetManager-connectComplete-decomp-20260425.txt`: WCLNetManager's connect-complete action performs the missing state transition and link-up-done path.
- exact semantic mismatch between reference and our code: reference has a producer-side connect-complete message in the join adapter lifecycle; local code publishes link-up/current-BSS but omits the required join completion message.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation:
  - runtime shows WCL stuck in the exact state whose name requires connect-complete.
  - decomp proves the reference producer exists, its message id/size, and the WCL consumer handler.
  - local source absence proves the required producer cannot fire.
  - the observed WCL abort has `connectCompleteStatus=0` fields and occurs before any successful EAPOL/key path, so the failure is upstream of password/key programming.
- why proposed fix is 1:1 with reference architecture and semantics:
  - add the exact message id (`0xd5`), payload length (`0xa4`), header+ten-record payload shape, and async `postMessage` delivery recovered from reference.
  - gate publication on real local `IEEE80211_S_RUN` and non-null `ic_bss`; no forced success is emitted before radio association.
  - do not add retries, delays, polling, duplicate legacy events, getter fallbacks, or masking.
- files/functions to modify:
  - `include/Airport/apple80211_var.h`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - sending `0xd5` from `getWCL_BSS_INFO(...)`: rejected; reference producer is join-adapter/event producer-side, not a getter side effect.
  - posting `0xd5` before local `RUN`: rejected; would be forced success and could falsely advance WCL.
  - retrying/replaying `setLinkState(...)` or `0xd8`: rejected; CR-091 proves current BSS is established and the remaining wait is for connect-complete.
  - masking airportd `-3905` or fabricating current-network getter success: rejected; would not satisfy the WCL state machine.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - BootKC unresolved-symbol check
  - create CR-093 Stage 1 request with exact patch artifact and diff hash
  - after `APPROVED_FOR_AFTER_FIX_RUNTIME`, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs around `postTahoeWclConnectCompleteEvent`, `NET_MANAGER_EVENT_CONNECT_COMPLETE`, `setCIPHER_KEY`, EAPOL, DHCP/link-up-done, and data-path counters.

## ANOMALY
- id: A-DATA-SKYWALK-RX-PREPARE-021
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-093, the driver is visible and the UI shows the association icon, but `CONTROL_STA_NETWORK` gets no internet, then disconnects. AP deauths with reason 15 after EAPOL M1 retransmissions.
- first visible manifestation: `2026-04-25 12:51:37..12:51:58 +0300` after CR-093 after-fix runtime: WCL receives connect-complete and moves to `WAITING_FOR_IP`, DHCP starts, but EAPOL never progresses and the AP deauths with `reason 15`.
- expected system behavior: inbound EAPOL frames from the associated AP must be copied into a prepared Skywalk RX packet with a valid packet-buffer array and enqueued into `IOSkywalkRxCompletionQueue`, allowing the userspace supplicant to send EAPOL M2.
- actual behavior: runtime diagnostics count `eapol_rx=11`, `rx_drop=11`, `eapol_tx=0`, `tx=0`, and `lastRxResult=0xc`. The RX path sees eleven 113-byte EAPOL frames but locally drops all of them before userspace can respond.
- divergence point: `skywalkRxInput(...)` calls `rxPkt->getPacketBuffers(...)` immediately after `fRxPool->allocatePacket(...)`, before `rxPkt->prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, ...)` has populated the packet-buffer array.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-093-control_sta_network-partial-nointernet-20260425-131029.log` (`sha256 dee3a9ee5f7668c2af31a83ffd98a654274871faa214ae9166cf833e3de0db51`) captures the user-reported partial connection/no-internet/disconnect window.
  - runtime diagnostics: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-094-runtime-rx-eapol-drop-20260425.txt` (`sha256 2b5ba80b04386f37bc1eec30973319f65ab8a97f3f7db56ab40527badc3cd34d`) records `mode=0x1d`, `block=0x0`, `current_ssid=CONTROL_STA_NETWORK`, `counts ... tx=0 rx=11 eapol_tx=0 eapol_rx=11 tx_drop=0 rx_drop=11`, and trace rows `kind=rx result=0xc arg0=1 arg1=0x71`.
  - runtime logs: no `skywalkRxInput: allocatePacket failed` and no `skywalkRxInput: enqueuePackets failed` lines appear in the focused CR-093/CR-094 windows; with `lastRxResult=0xc`, the remaining local drop branch is the `getPacketBuffers(...) == 0` branch.
  - decomp: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-094-IOSkywalk-prepare-getbuffers-evidence-20260425.txt` (`sha256 a8bf11c53c0b012ae8da0a4178a59ae42554e09fc823eb87002fa59528fa6c8d`) shows `IOSkywalkPacketBufferPool::allocatePacket(UInt32,...)` calls packet `acquireWithPacketHandle(...)` and returns without preparing buffers.
  - decomp: the same CR-094 evidence shows `IOSkywalkPacket::acquireWithPacketHandle(...)` only records the packet handle/state fields; it does not populate `mPacketBuffers` or the count consumed by `getPacketBuffers(...)`.
  - decomp: the same CR-094 evidence shows `IOSkywalkPacket::prepareWithQueue(...)` walks kernel buflets, maps them through the pool, writes packet-buffer pointers, and stores the actual buffer count before completion.
  - decomp/disasm: the same CR-094 evidence shows `IOSkywalkPacket::getPacketBuffers(...)` reads the actual buffer count at object offset `0x64`, returns zero when that count is zero, and only then copies pointers from the `mPacketBuffers` array.
  - source: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` allocates the packet, immediately calls `getPacketBuffers(...)`, and drops with `ENOMEM` if no buffers are returned.
  - source/header: `include/Airport/IOSkywalkPacketBufferPool.h` still declares Tahoe `allocatePacket(...)`, `deallocatePacket(...)`, and related packet-buffer pool methods with legacy `bool`/`void` return types, while the Tahoe/KDK header and BootKC ABI return `IOReturn`; this can hide exact Skywalk allocation errors and must be aligned in the same RX contract fix.
- candidate causes:
  - confirmed: missing `prepareWithQueue(...)` before `getPacketBuffers(...)` leaves the newly allocated RX packet with no visible packet buffers, so every inbound EAPOL frame drops before enqueue.
  - confirmed deviation: local `IOSkywalkPacketBufferPool` declarations use stale return types for methods whose Tahoe ABI returns `IOReturn`.
  - insufficient data for this fix: downstream EAPOL key install, DHCP, and data TX/RX may still contain later blockers after RX delivery starts; they are outside this root-cause claim.
- rejected causes:
  - WCL connect-complete still missing: rejected by CR-093 runtime; `postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4` fires, WCL consumes it, and state advances to `WAITING_FOR_IP`.
  - diagnostic layer blocking EAPOL: rejected by snapshot `block=0x0` and trace `block=0`.
  - AP/password failure as the first blocker: rejected for this layer; AP sends eleven EAPOL M1 frames and deauths with reason 15 only after no M2 is returned.
  - allocate failure as the active branch: rejected by absence of the local `allocatePacket failed` log in the same window; the source logs the first five allocation failures.
  - enqueue failure as the active branch: rejected by absence of the local `enqueuePackets failed` log and by `lastRxResult=0xc` instead of an `IOReturn` enqueue status.
- confirmed deviation: local RX code violates the Skywalk packet lifecycle contract by reading packet buffers before `prepareWithQueue(...)`; local pool method declarations also violate the Tahoe `IOReturn` ABI.
- root cause: inbound EAPOL frames are dropped inside `skywalkRxInput(...)` because the newly allocated `IOSkywalkPacket` has not been prepared against the RX completion queue, so `getPacketBuffers(...)` returns zero and userspace never receives EAPOL M1.
- fix: implemented locally by aligning `IOSkywalkPacketBufferPool` method return types to `IOReturn` while preserving vtable slot order, then inserting `rxPkt->prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)` immediately after successful allocation and before `getPacketBuffers(...)`.
- verification: `git diff --check` passed; Tahoe build passed with BootKC symbol verification (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-094-build-20260425-skywalk-rx-prepare.txt`, sha256 `0345abec173c385088fef65a1f6d445f96deba4c485763bf966807292558f9f3`, built binary UUID `3841FB3F-31C8-3158-9C2A-C8450A987FF7`, sha256 `96f5981cec3127013ae49852b6d23ecdae929b515ba1d1a2b8a25ed9e499b790`); built `skywalkRxInput(...)` disassembly (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-094-skywalkRxInput-disasm-20260425.txt`, sha256 `9913ce61f11b57df553c54b946829a53075e40a50c1aa1c92e4fcb35eb963c4c`) shows full `IOReturn` allocation handling, `prepareWithQueue(..., Rx, 0)` before `getPacketBuffers(...)`, and no bool truncation after `allocatePacket(...)`. CR-094 Stage 1 request pending. After approval, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then verify `rx_drop` no longer increments on EAPOL M1, `eapol_tx`/TX path sees M2 or the failure moves to a later key/DHCP/data point.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-PREPARE-021
- symptom: after CR-093, association reaches UI link-up/`WAITING_FOR_IP`, but `CONTROL_STA_NETWORK` has no internet and disconnects after AP reason 15.
- expected system behavior: RX mbufs entering `skywalkRxInput(...)` must be converted into prepared `IOSkywalkPacket` objects with valid packet buffers, copied, length-marked, and enqueued into the RX completion queue.
- actual behavior: local RX allocates a packet and calls `getPacketBuffers(...)` before packet preparation; runtime shows all eleven inbound 113-byte EAPOL frames dropped locally with `rx=0xc`, while no TX/EAPOL response is emitted.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`, between `fRxPool->allocatePacket(1, &rxPkt, 0)` and `rxPkt->getPacketBuffers(bufs, 1)`.
- evidence from runtime:
  - `CR-094-runtime-rx-eapol-drop-20260425.txt`: `block=0x0`, `current_ssid=CONTROL_STA_NETWORK`, `eapol_rx=11`, `eapol_tx=0`, `rx_drop=11`, trace rows `rx result=0xc len=0x71`.
  - `CR-093-control_sta_network-partial-nointernet-20260425-131029.log`: WCL connect-complete has already passed, DHCP starts, then AP deauths with reason 15.
- evidence from decomp:
  - `IOSkywalkPacketBufferPool::allocatePacket(UInt32,...)` returns after `acquireWithPacketHandle(...)`; it does not prepare packet buffers.
  - `IOSkywalkPacket::acquireWithPacketHandle(...)` writes handle/state fields only.
  - `IOSkywalkPacket::prepareWithQueue(...)` is the function that walks buflets and fills the packet buffer pointer array/count.
  - `IOSkywalkPacket::getPacketBuffers(...)` returns zero when the actual buffer count is zero and copies pointers only after that count is nonzero.
- exact semantic mismatch between reference and our code: the Skywalk object contract requires queue preparation before buffer extraction; local code extracts buffers directly after allocation. Local packet-buffer pool declarations also treat `IOReturn` methods as `bool`/`void`.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkPacketBufferPool::allocatePacket(UInt32, IOSkywalkPacket **, IOOptionBits)`
    - `IOSkywalkPacket::prepareWithQueue(IOSkywalkPacketQueue *, IOSkywalkPacketDirection, IOOptionBits)`
    - `IOSkywalkPacket::getPacketBuffers(IOSkywalkPacketBuffer **, UInt32)`
    - `IOSkywalkPacket::setDataLength(UInt32)` and buflet data offset/length fields
    - `IOSkywalkRxCompletionQueue::enqueuePackets(const IOSkywalkPacket **, UInt32, IOOptionBits)`
  - expected contract at each touchpoint:
    - `allocatePacket(...)` returns `IOReturn` and provides an acquired packet handle, not a prepared packet-buffer array.
    - `prepareWithQueue(..., Rx, ...)` binds the packet to the queue/direction and populates the packet-buffer array/count needed by `getPacketBuffers(...)`.
    - `getPacketBuffers(...)` may only be used after the packet has a nonzero actual buffer count; otherwise zero is a valid no-buffer result.
    - data offset/length and packet length are set only after a valid buffer object is available.
    - `enqueuePackets(...)` receives an already prepared packet and transfers it to the RX completion path; it is not the missing buffer-preparation step for the local producer.
  - why no relevant touchpoints are missing:
    - the runtime drop happens before data copy and before enqueue, so TX, key install, DHCP, and userspace supplicant paths cannot be the first failing touchpoint.
    - decomp covers every Skywalk method invoked between local mbuf input and the observed pre-enqueue drop branch.
    - header ABI alignment covers the pool method return-value contract used at the first RX touchpoint.
  - why proposed path adds no extra system-visible side effects:
    - no new events, retries, timers, polling, forced success, WCL state changes, or packet fabrication are added.
    - the same inbound mbuf is copied once into the same allocated packet and enqueued once; only the required preparation step is inserted before buffer access.
    - if preparation fails, the function drops the frame through the existing local error/drop shape and records the real `IOReturn`.
- why this is root cause and not just correlation:
  - runtime proves the AP sends EAPOL M1 and the local RX path drops it before any TX response.
  - source branch analysis leaves only the `getPacketBuffers(...) == 0` branch for `lastRxResult=0xc` in this window.
  - decomp proves `getPacketBuffers(...)` depends on state populated by `prepareWithQueue(...)`, and local code never calls that before the drop.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it follows the recovered Skywalk packet lifecycle: allocate/acquire -> prepare with queue/direction -> get buffers -> set data -> enqueue.
  - it aligns local method declarations to the Tahoe BootKC/KDK `IOReturn` ABI without changing vtable slot order.
- files/functions to modify:
  - `include/Airport/IOSkywalkPacketBufferPool.h`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - retrying EAPOL, delaying join, or extending WCL timers: rejected; EAPOL is already received and deterministically dropped locally.
  - fabricating EAPOL success/key state: rejected; would mask the RX delivery contract.
  - bypassing Skywalk and calling legacy input paths: rejected; Tahoe UI/link path is already on the Skywalk interface and this would add a second data-plane architecture.
  - adding broad diagnostic logging before fixing the lifecycle order: rejected; runtime plus decomp already identify the exact lifecycle violation.
  - changing scan list, WCL BSS-info, or connect-complete again: rejected; those earlier blockers have moved and are not the first failing point in this runtime.
- verification plan:
  - `git diff --check`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect the built binary for `prepareWithQueue` before `getPacketBuffers` in `skywalkRxInput(...)`
  - create CR-094 Stage 1 request with diff artifact and hashes
  - after approval, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs plus `airport_itlwm_regdiag get snapshot/trace` to confirm EAPOL RX frames are no longer dropped before userspace response.

## ANOMALY
- id: A-DATA-SKYWALK-RX-PREPARE-NOTFOUND-022
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-095, the driver remains visible and the UI scan list is alive, but repeated `CONTROL_STA_NETWORK` join attempts still fail; the UI shows no associated AirPort network after the attempt.
- first visible manifestation: `2026-04-25 16:17..16:18 +0300` on the CR-095 installed driver: `en0` is `UP,RUNNING` but `status: inactive`, `networksetup -getairportnetwork en0` reports no association, and diagnostics show EAPOL RX frames dropped before any TX response.
- expected system behavior: after WCL connect-complete and local association, inbound 113-byte EAPOL frames must be delivered through the Skywalk RX completion path so the userspace supplicant can generate EAPOL M2.
- actual behavior: CR-095 changes the failure from the old `getPacketBuffers(...) == 0` branch (`rx=0xc`) to `IOSkywalkPacket::prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)` returning `0xe00002f0` (`kIOReturnNotFound`). Runtime counters show `eapol_rx=12`, `eapol_tx=0`, `rx_drop=12`, `tx=0`, `block=0x0`.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` lines `1420..1428`, immediately after successful `fRxPool->allocatePacket(1, &rxPkt, 0)` and before `getPacketBuffers(...)`.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-095-afterfix-control_sta_network-repeat-20260425-160323.log` (`sha256 c1e488165fee4affb0a447719eba9e6fc4f1e3c4fc492107fff96156ff7614af`) captures the repeated failed CONTROL_STA_NETWORK attempt on the installed CR-095 binary.
  - runtime diagnostics: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-095-afterfix-control_sta_network-repeat2-20260425-1617.txt` (`sha256 43356e01d2d3b73a5f7331e85e316affcba37571d59369c09e224c9950c532a0`) records `mode=0x1d`, `block=0x0`, `current_ssid=CONTROL_STA_NETWORK`, `last_assoc_ssid=CONTROL_STA_NETWORK`, `eapol_rx=12`, `eapol_tx=0`, `rx_drop=12`, `tx=0`, `last_result rx=0xe00002f0`, and trace rows `kind=rx path=rx result=0xe00002f0 arg0=1 arg1=0x71`.
  - runtime state: the same artifact records `ifconfig en0` as active at the interface layer but `status: inactive`, and `networksetup -getairportnetwork en0` as not associated.
  - source: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` records `prepRet` directly when `prepareWithQueue(...)` fails; the observed `0xe00002f0` therefore originates from that exact pre-`getPacketBuffers(...)` branch.
  - decomp: `IOSkywalkFamily_decompiled.c` shows `IOSkywalkPacketBufferPool::allocatePacket(UInt32,...)` acquires a packet handle and returns without packet-buffer preparation; `IOSkywalkPacket::prepareWithQueue(...)` walks kernel buflets, maps them through the pool, calls packet-buffer preparation, stores buffer pointers/count, and then invokes the packet queue preparation vtable path.
  - decomp: `IOSkywalkPacketBuffer::setDataLength(...)`, `setDataOffset(...)`, and related buffer methods return `0xe00002f0` when required buflet/buffer state is not found, showing that `kIOReturnNotFound` is a Skywalk object-state failure, not an association/UI failure.
  - vtable evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-096-IOSkywalkPacket-vtable-slots-20260425.txt` (`sha256 c095ddcc5c7012a4be2034a28086311c0b44b0227cc5b5b6d5dde9ba29a311d8`) dumps the real Tahoe `IOSkywalkPacket` vtable. It shows `getPacketBuffers(...)` at slot `0x120`, `prepareWithQueue(...)` at slot `0x188`, and slot `0x160` as `FUN_ffffff8002a58a2e`.
  - disasm evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-094-skywalkRxInput-disasm-20260425.txt` (`sha256 9913ce61f11b57df553c54b946829a53075e40a50c1aa1c92e4fcb35eb963c4c`) shows the CR-095 binary calling `rxPkt` vtable slot `0x160` at the source line that names `prepareWithQueue(...)`, not the real Tahoe `0x188` slot.
  - decomp/disasm: `FUN_ffffff8002a58a2e` returns `0xe00002f0` when the packet has no actual packet buffers. That exactly matches the CR-095 runtime result and explains why the call fails before `getPacketBuffers(...)`.
  - header evidence: local `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkPacket.h` lacks five Tahoe virtual slots between `setDataOffsetAndLength(...)` and `prepareWithQueue(...)`. The real Tahoe vtable has these slots at `0x160..0x180`, shifting `prepareWithQueue(...)` from the local compile-time `0x160` to the real BootKC `0x188`.
- candidate causes:
  - confirmed: the Tahoe `IOSkywalkPacket` ABI has five virtual slots missing from the local MacKernelSDK header, so the source call to `prepareWithQueue(...)` is compiled to the stale slot `0x160`; BootKC executes the real function at `0x160`, which returns `kIOReturnNotFound` for an unprepared/no-buffer packet.
  - insufficient for this fix: queue direction, packet type, base/custom pool, and pool options remain possible later data-plane mismatches, but they are not the first failing call in CR-095 because the named call never reaches real `prepareWithQueue(...)`.
- rejected causes:
  - scan/UI publication as the current blocker: rejected for this runtime; UI remains alive, scan list is present, and association proceeds far enough to receive EAPOL M1 frames.
  - diagnostic blocking: rejected by runtime `block=0x0` and trace rows without block records.
  - the old `getPacketBuffers(...) == 0` branch as the current blocker: rejected by CR-095 diagnostics; the first failing branch is now `prepareWithQueue(...)` with `0xe00002f0`.
  - password/AP absence as the first blocker: rejected for this layer; the AP sends EAPOL M1 frames and the driver records them as inbound EAPOL before dropping.
  - wrong queue/direction as the first CR-095 blocker: rejected for this fix scope; the binary does not call the real `prepareWithQueue(...)` slot, so queue/direction cannot yet be evaluated.
  - packet type/pool options as the first CR-095 blocker: rejected for this fix scope; the observed `0xe00002f0` is explained by the stale vtable slot before any proven pool-option contract is reached.
- confirmed deviation: local build headers place `IOSkywalkPacket::prepareWithQueue(...)` at compile-time vtable slot `0x160`, while the Tahoe BootKC vtable places it at `0x188`. Slot `0x160` in Tahoe is a different packet data/buffer method that returns `0xe00002f0` when actual buffers are zero.
- root cause: the CR-095 source call named `prepareWithQueue(...)` never reaches the BootKC `prepareWithQueue(...)` implementation. It dispatches to the stale `0x160` slot and returns `kIOReturnNotFound`, so every inbound EAPOL frame is dropped before the packet-buffer array can be prepared.
- fix: implemented locally. `scripts/build_tahoe.sh::patch_mackernelsdk` now aligns the local MacKernelSDK `IOSkywalkPacket` class layout during Tahoe builds by inserting the five missing Tahoe virtual packet-data slots before `prepareWithQueue(...)`, so the same source call dispatches to real slot `0x188`.
- verification: `git diff --check` passed; Tahoe build passed with BootKC symbol verification (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-096-build-20260425-skywalk-packet-vtable-abi.txt`, sha256 `c02a22528a6f4799cdfdd36aabc15dcdd02ca514784389c2351e2b0f7e257509`, built binary sha256 `a3d35ac64b9b89ee6c207e4180a7563df13597f556fd36f271210257ad1e391d`). Built `skywalkRxInput(...)` disassembly (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-096-skywalkRxInput-disasm-20260425.txt`, sha256 `05fb962d6d1fb1f2373e98af9ce5f8cd9b43d2725e611b604435d70183190140`) shows allocation through `*0x130(%rax)`, the source `prepareWithQueue(...)` call through `*0x188(%rax)`, and `getPacketBuffers(...)` through `*0x120(%rax)`. After approved runtime, one `CONTROL_STA_NETWORK` join must show that EAPOL RX no longer drops at `prepareWithQueue(...) result=0xe00002f0`; if it moves to a later Skywalk, EAPOL TX/key, DHCP, or data stage, that is a new later blocker.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-PREPARE-NOTFOUND-022
- symptom: CR-095 keeps the driver visible and scan/UI alive, but `CONTROL_STA_NETWORK` join fails because every inbound EAPOL M1 is dropped before userspace can answer.
- expected system behavior: the source call `rxPkt->prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)` must dispatch to Tahoe `IOSkywalkPacket` vtable slot `0x188`, populate the packet-buffer array, then allow `getPacketBuffers(...)` at slot `0x120` to return the buffer.
- actual behavior: the CR-095 binary dispatches the source `prepareWithQueue(...)` call to vtable slot `0x160`. In Tahoe slot `0x160` is `FUN_ffffff8002a58a2e`, not `prepareWithQueue(...)`, and it returns `0xe00002f0` for the current unprepared/no-buffer packet state.
- exact divergence point: local `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkPacket.h` omits five Tahoe virtual methods between `setDataOffsetAndLength(...)` and `prepareWithQueue(...)`, shifting all later packet virtual calls in local code by `-0x28`.
- evidence from runtime:
  - `CR-095-afterfix-control_sta_network-repeat2-20260425-1617.txt`: `block=0x0`, `eapol_rx=12`, `eapol_tx=0`, `rx_drop=12`, `tx=0`, and `last_result rx=0xe00002f0`.
  - trace rows: `kind=rx path=rx result=0xe00002f0 arg0=1 arg1=0x71`, matching 113-byte EAPOL M1 frames.
- evidence from decomp:
  - `CR-096-IOSkywalkPacket-vtable-slots-20260425.txt`: real Tahoe `IOSkywalkPacket` has `getPacketBuffers(...)` at `0x120`, missing packet-data slots at `0x160..0x180`, and `prepareWithQueue(...)` at `0x188`.
  - `FUN_ffffff8002a58a2e` returns `0xe00002f0` when actual packet-buffer count is zero; this is the exact value seen in CR-095 runtime.
  - `FUN_ffffff8002a33526` is real `IOSkywalkPacket::prepareWithQueue(...)` and lives at real slot `0x188`.
- exact semantic mismatch between reference and our code: the source expresses the right Skywalk lifecycle step, but the local build ABI is stale, so BootKC receives a different virtual method call. This is a system-facing ABI mismatch, not a queue/pool behavior choice.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkPacket::getPacketBuffers(...)` vtable slot `0x120`
    - five Tahoe packet-data virtual slots at `0x160..0x180`
    - `IOSkywalkPacket::prepareWithQueue(...)` vtable slot `0x188`
    - `IOSkywalkPacket::completeWithQueue(...)` vtable slot `0x198`
    - local Tahoe build script patching of the MacKernelSDK header before compilation
  - expected contract at each touchpoint:
    - `getPacketBuffers(...)` remains at the already-correct slot `0x120`.
    - the five Tahoe packet-data virtual slots must be declared so later virtual offsets match BootKC.
    - `prepareWithQueue(...)` must compile to and dispatch through slot `0x188`.
    - later packet lifecycle methods must no longer be shifted by the stale header.
    - the build script must make this SDK header alignment reproducible before every Tahoe build because `MacKernelSDK` itself is not tracked in this repository.
  - why no relevant touchpoints are missing:
    - the runtime failure is the returned value from the pre-`getPacketBuffers(...)` source call.
    - disassembly proves the wrong virtual slot for that exact source call.
    - vtable evidence identifies both the wrong slot and the correct target slot.
  - why proposed path adds no extra system-visible side effects:
    - no driver logic, packet payload, event, queue, timing, retry, filter, or state transition is changed.
    - the only behavior change is that the existing source call dispatches to the Tahoe ABI-correct virtual method.
- why this is root cause and not just correlation:
  - the wrong compiled slot (`0x160`) resolves to a Tahoe method whose failure return (`0xe00002f0`) exactly matches runtime.
  - the correct `prepareWithQueue(...)` slot is `0x188`, so the current binary cannot execute the method that the source intended.
  - runtime shows the failure occurs before buffer extraction/enqueue, exactly at the mis-dispatched call.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it aligns the local compile-time Skywalk class ABI to the Tahoe BootKC vtable recovered from decomp/disassembly.
  - it does not invent a new RX path or alter queue/pool semantics; it makes the already-approved lifecycle call reach the real system implementation.
- files/functions to modify:
  - `scripts/build_tahoe.sh::patch_mackernelsdk`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - retrying `prepareWithQueue(...)`: rejected because the call currently targets the wrong method.
  - changing queue direction or pool type first: rejected because the real `prepareWithQueue(...)` is not reached yet.
  - manually calling a hard-coded vtable slot from driver code: rejected as a brittle ABI bypass when the header can be aligned.
  - suppressing `0xe00002f0` or treating it as success: rejected as masking and would leave the packet unprepared.
  - adding diagnostics instead of fixing the header ABI: rejected because runtime plus vtable proof already identify the exact mismatch.
- verification plan:
  - `git diff --check`
  - run `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and require the source `prepareWithQueue(...)` call to dispatch through `*0x188(%rax)`
  - create CR-096 Stage 1 request with exact diff artifact and hashes
  - after `APPROVED_FOR_AFTER_FIX_RUNTIME`, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs and `airport_itlwm_regdiag get snapshot/trace` to verify the `0xe00002f0` prepare-stage drop is gone.

## ANOMALY

- id: A-DATA-SKYWALK-RX-BUFLET-PACKING-023
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-096, the driver loads and networks are visible, but an `CONTROL_STA_NETWORK` connection attempt panics the kernel.
- first visible manifestation: `2026-04-25 17:19 +0300` crash report for the installed CR-096 binary `D0CA0B65-4B62-37DE-A63C-4655F5AC2110` / sha256 `a3d35ac64b9b89ee6c207e4180a7563df13597f556fd36f271210257ad1e391d`.
- expected system behavior: after `getPacketBuffers(...)` returns an `IOSkywalkPacketBuffer`, local inline buflet accessors must read the Tahoe `struct __kern_buflet` fields at the same offsets as the kernel C KPI helpers.
- actual behavior: `_buflet_get_object_address(kern_buflet_t)` reads `buf_ctl` from offset `0x28` and then dereferences a non-canonical pointer (`RAX=0xffa4f46809700000`), causing a general protection panic before RX enqueue can complete.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp` local `struct __kern_buflet` definition omits `__attribute__((packed))`, while the source SDK contract in `MacKernelSDK/Headers/skywalk/packet/packet_var.h` declares `struct __kern_buflet` as packed and asserts `sizeof(struct __kern_buflet) == 44`.
- evidence:
  - crash report: `/Users/bob/Projects/itlwm/crash.txt` (`sha256 f1247c3ee7873b2d17258c312625480468a30229e5b22888da92ca55d5391e14`) shows `Kernel trap ... type = 13=general protection`, `RIP ... _buflet_get_object_address + 0x11`, caller `skywalkRxInput + 0x378`, and kext UUID `D0CA0B65-4B62-37DE-A63C-4655F5AC2110`.
  - focused evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-buflet-packing-crash-evidence-20260425.txt` (`sha256 bc565c00cdfcb4ca5d3703a8c5cad18884100f02fcf9580990d1bda85cb5eb19`) records the crash excerpt, loaded/on-disk CR-096 identity, local source layout, official SDK layout, and built helper disassembly.
  - sudo log artifact: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-096-afterfix-crash-log-20260425.txt` (`sha256 ef5b690806b564be68de4b327e72caa2c1719f6e3fe51b4ecf0ead0e81a930fd`) records the post-CR-096 runtime window; current post-reboot diagnostic snapshot/trace attempts report diagnostics are not enabled (`sha256 8e6087332143add782230e14d2de25ec93c6a101a16ff8d27ef5a7715c1153e4`, `sha256 2240416673519a96cb64b01bab8aa7f6cc0c1fd929bff2d03a267d952d87b5e6`).
  - source: `AirportItlwm/AirportItlwmV2.cpp` local `struct __buflet` is packed, but containing `struct __kern_buflet` is not packed; on LP64 this aligns `buf_ctl` to offset `0x28`.
  - official SDK: `MacKernelSDK/Headers/skywalk/packet/packet_var.h` lines `47..63` define `struct __kern_buflet { struct __buflet buf_com; const struct skmem_bufctl *buf_ctl; } __attribute((packed));`; lines `174..181` assert `sizeof(struct __kern_buflet) == 44`.
  - disassembly: CR-096 helper `_buflet_get_object_address` loads `buf_ctl` with `movq 0x28(%rax), %rax`, then loads `bc_addr` with `movq 0x8(%rax), %rax`; the panic happens at that second load with the invalid pointer from the wrong `buf_ctl` offset.
- candidate causes:
  - confirmed: local `struct __kern_buflet` packing differs from Tahoe `packet_var.h`, shifting `buf_ctl` from the real packed offset `0x24` to local compile-time offset `0x28`.
  - insufficient for this fix: after correct packed buflet access, later packet data length, enqueue, EAPOL TX, key install, DHCP, and data transfer may reveal later blockers; they cannot be evaluated while the first buflet dereference panics.
- rejected causes:
  - CR-096 packet vtable ABI fix as wrong: rejected as the first explanation; CR-096 moved execution past `prepareWithQueue(...)` and into buflet object access, proving slot `0x188` is reached enough to populate a packet buffer.
  - null packet buffer: rejected by control flow; `skywalkRxInput(...)` checks `nBufs == 0 || !bufs[0]` before line `1443`.
  - diagnostic layer causing the panic: rejected; crash is a direct general-protection fault in inline `_buflet_get_object_address` with a register value consistent with an offset/layout error.
  - queue direction or pool type as first crash cause: rejected for this fix scope; the panic is at local direct struct access before enqueue and before any later queue/pool behavioral return can be inspected.
  - masking by adding null checks around `buf_ctl`: rejected; the value read from the wrong offset can be non-null but invalid, and a null check would not make the layout contract correct.
- confirmed deviation: local direct-access replacement for `kern_buflet_get_object_address` uses a compile-time `__kern_buflet` layout that does not match the Tahoe packed kernel layout.
- root cause: CR-096 reaches the RX packet buffer, but the local inline `__kern_buflet` definition reads `buf_ctl` from offset `0x28` instead of the real packed offset `0x24`, producing an invalid `skmem_bufctl *` and panicking on `bc_addr`.
- fix: implemented locally. Local `struct __kern_buflet` is now packed and has compile-time assertions that `sizeof(struct __kern_buflet) == 44` and `offsetof(__kern_buflet, buf_ctl) == 36`, matching `packet_var.h`.
- verification: `git diff --check` passed; `bash -n scripts/build_tahoe.sh` passed; Tahoe build passed with BootKC symbol verification (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-build-20260425-buflet-packing.txt`, sha256 `d30c5a5ac2b78211df1dc9cdda1f3d0ea94211e9d15b10f1219b074f1a33e4a7`, built binary UUID `CEEB2EC1-072B-385B-9BBF-191E77C470C0`, sha256 `2f7e60b15f399c8952258a0c37c154bcbf8eac2ab233de7d1cb3c3127aebb3bb`). Built helper disassembly (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-buflet-helper-disasm-20260425.txt`, sha256 `29706e83a9415a5a402e825dca39916e8c3cc871d279694b6c5a2801f31e991a`) shows `_buflet_get_object_address(...)` and `_buflet_get_object_limit(...)` load `buf_ctl` from `0x24(%rax)`, not `0x28(%rax)`. Built `skywalkRxInput(...)` disassembly (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-skywalkRxInput-disasm-20260425.txt`, sha256 `05fb962d6d1fb1f2373e98af9ce5f8cd9b43d2725e611b604435d70183190140`) keeps `allocatePacket` at `*0x130(%rax)`, `prepareWithQueue` at `*0x188(%rax)`, and `getPacketBuffers` at `*0x120(%rax)`. After approved runtime, one `CONTROL_STA_NETWORK` join must not panic at `_buflet_get_object_address`; if failure moves to object limit/length, enqueue, EAPOL TX, key install, DHCP, or data transfer, that is a new later blocker.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-BUFLET-PACKING-023
- symptom: CR-096 loads and shows networks, but the first connection attempt panics in `_buflet_get_object_address(...)` from `skywalkRxInput(...)`.
- expected system behavior: local inline replacements for `kern_buflet_*` must use the exact packed Tahoe `struct __kern_buflet` layout from `packet_var.h`, where the packed structure size is `44` bytes and `buf_ctl` starts at offset `36` (`0x24`).
- actual behavior: the local source copies the field list but omits packing on `struct __kern_buflet`; the compiler aligns `buf_ctl` to offset `40` (`0x28`). CR-096 disassembly confirms `_buflet_get_object_address(...)` reads `0x28(%rax)`.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp` local `struct __kern_buflet` definition at lines `84..90`.
- evidence from runtime:
  - `/Users/bob/Projects/itlwm/crash.txt`: `RIP ... _buflet_get_object_address + 0x11`, caller `skywalkRxInput + 0x378`, kext UUID `D0CA0B65-4B62-37DE-A63C-4655F5AC2110`, `RAX=0xffa4f46809700000`.
  - `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-buflet-packing-crash-evidence-20260425.txt`: focused crash/layout/disassembly proof.
- evidence from system headers:
  - `MacKernelSDK/Headers/skywalk/packet/packet_var.h`: `struct __kern_buflet` is `__attribute((packed))`.
  - the same header asserts `sizeof(struct __kern_buflet) == 44`, which requires `buf_ctl` at offset `0x24` on LP64 because packed `struct __buflet` is 36 bytes.
- exact semantic mismatch between system and local code: local inline accessors are intended to be ABI-identical stand-ins for `kern_buflet_get_object_address`, but they are compiled with a different containing-structure layout than the kernel.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `struct __buflet` common layout copied from Skywalk packet headers
    - `struct __kern_buflet` packed kernel layout from `packet_var.h`
    - `skmem_bufctl::bc_addr` at offset `0x08`
    - `skmem_bufctl::bc_lim` at offset `0x20`
    - local `_buflet_get_object_address`, `_buflet_get_object_limit`, `_buflet_get_data_offset`, `_buflet_get_data_length`, `_buflet_set_data_offset`, and `_buflet_set_data_length`
  - expected contract at each touchpoint:
    - `struct __buflet` remains packed.
    - `struct __kern_buflet` must also be packed and size `44`.
    - `buf_ctl` must be read from offset `0x24`, then `bc_addr` from `buf_ctl + 0x08` and `bc_lim` from `buf_ctl + 0x20`.
    - data offset/length fields remain in the packed common `__buflet` region.
  - why no relevant touchpoints are missing:
    - the panic happens before data copy, length set, enqueue, EAPOL TX, key install, DHCP, or data transfer.
    - crash RIP, caller offset, register state, source line, official header, and disassembly all point to the first `buf_ctl` field load.
  - why proposed path adds no extra system-visible side effects:
    - no packet flow logic, queue/pool choice, event, retry, timer, fallback, or state transition changes.
    - only local compile-time layout is corrected to match the system header contract.
- why this is root cause and not just correlation:
  - the crash instruction is exactly the second load after reading `buf_ctl`; CR-096 disassembly proves the first load used `0x28`.
  - official header requires the field to be at `0x24`; using `0x28` reads bytes inside the real pointer/adjacent field, matching the non-canonical panic pointer.
  - the crash occurs on the first RX packet after CR-096 moved past the previous `prepareWithQueue` failure, matching the new first dereference point.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it mirrors the exact `__attribute((packed))` declaration and size assertion from Apple's Skywalk packet header.
  - it keeps the existing direct-access architecture but makes its layout identical to the kernel structure it is replacing.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp` local `struct __kern_buflet` declaration and layout assertions.
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - add only pointer/null checks around `buf_ctl`: rejected because the wrong-offset value can be non-null and non-canonical; this would mask the ABI error and may still panic.
  - change queue direction or packet/pool type first: rejected because the first failure is local struct dereference before enqueue.
  - revert CR-096: rejected because CR-096 exposed the next blocker and the previous binary could not call the real `prepareWithQueue`.
  - use hard-coded byte offsets manually in every accessor: rejected because packed struct plus static assertions is the direct header-equivalent representation.
  - switch to exported `kern_buflet_*` calls without proving AuxKC linkage: rejected for this fix; the current direct-access approach is already used, and the exact bug is its missing packing.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - Tahoe build via `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `_buflet_get_object_address(...)` and `_buflet_get_object_limit(...)`; require `buf_ctl` load from `0x24(%rax)` and no remaining `0x28(%rax)` load in these helpers.
  - create CR-097 Stage 1 request with exact diff artifact and hashes
  - after approval, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo crash/log evidence plus regdiag snapshot/trace if diagnostics are enabled.

## ANOMALY

- id: A-DATA-SKYWALK-RX-PACKET-DATA-API-024
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-097, the driver loads and networks are visible, but the first connection attempt panics the kernel in `skywalkRxInput(...)`.
- first visible manifestation: `2026-04-25 18:07 +0300` crash report for the installed CR-097 binary `CEEB2EC1-072B-385B-9BBF-191E77C470C0` / sha256 `2f7e60b15f399c8952258a0c37c154bcbf8eac2ab233de7d1cb3c3127aebb3bb`.
- expected system behavior: driver-level packet data access must use the Tahoe `IOSkywalkPacket` packet-data methods after the packet has been prepared: `getDataVirtualAddress()`, `getDataLength()`, `getDataOffset()`, and `setDataOffsetAndLength(...)`. These methods operate through packet-buffer memory segment state and packet-buffer data length/offset state.
- actual behavior: local `skywalkRxInput(...)` and `skywalkTxAction(...)` fetch `IOSkywalkPacketBuffer::mBufletHandle` and dereference private `kern_buflet_t` internals directly through local `_buflet_*` helpers. After CR-097 fixed the `buf_ctl` offset to `0x24`, the helper still panics because the `buf_ctl` value read from the live buflet handle is non-canonical (`RAX=0x0fa0000000000000`).
- divergence point: `AirportItlwm/AirportItlwmV2.cpp` local packet data path bypasses `IOSkywalkPacket` data accessors and directly reads/writes `kern_buflet_t` private fields at lines `1318..1325` and `1448..1462`.
- evidence:
  - panic logs: `/Users/bob/Projects/itlwm/crash.txt` (`sha256 4efba9e9f59b6cff45f659e1f6b408b1eb4a2eb1425b2985e45fe37e3ca8bea9`) shows `Kernel trap ... type = 13=general protection`, `RIP ... _buflet_get_object_address + 0x11`, caller `skywalkRxInput + 0x378`, `RAX=0x0fa0000000000000`, and kext UUID `CEEB2EC1-072B-385B-9BBF-191E77C470C0`.
  - runtime logs: loaded kext and on-disk binary are both CR-097 UUID `CEEB2EC1-072B-385B-9BBF-191E77C470C0`; installed binary sha256 is `2f7e60b15f399c8952258a0c37c154bcbf8eac2ab233de7d1cb3c3127aebb3bb`.
  - disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-buflet-helper-disasm-20260425.txt` (`sha256 29706e83a9415a5a402e825dca39916e8c3cc871d279694b6c5a2801f31e991a`) shows CR-097 helper now loads `buf_ctl` from `0x24(%rax)` and then dereferences `bc_addr` at `0x8(%rax)`. The new panic at the same `+0x11` is therefore the second load from an invalid `buf_ctl`, not the old wrong-offset load.
  - disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-097-skywalkRxInput-disasm-20260425.txt` (`sha256 05fb962d6d1fb1f2373e98af9ce5f8cd9b43d2725e611b604435d70183190140`) shows the RX path calls `prepareWithQueue` at `*0x188`, `getPacketBuffers` at `*0x120`, then reads `mBufletHandle` from `0x30(%rax)` and calls local `_buflet_get_object_address(...)`.
  - decomp: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-skywalk-packet-data-api-evidence-20260425.txt` (`sha256 b972a3e98ad8b43d805c27c1fc9bc5785c88ab527e7a7c567b9236e7d71662cf`) records the Tahoe `IOSkywalkPacket` vtable: `getDataVirtualAddress` slot `0x178`, `getDataIOVirtualAddress` slot `0x180`, `prepareWithQueue` slot `0x188`.
  - decomp: the same CR-098 evidence records `IOSkywalkPacket::getDataVirtualAddress()` (`FUN_ffffff8002a33468`) deriving the host data pointer from the first `IOSkywalkPacketBuffer` memory segment plus `mMemorySegmentOffset`; it does not dereference `kern_buflet_t::buf_ctl`.
  - decomp: the same CR-098 evidence records packet data setters (`FUN_ffffff8002a58bbe`, `FUN_ffffff8002a58c06`, `FUN_ffffff8002a58c60`) validating against pool buffer size and writing packet-buffer `mDataLength`/`mDataOffset`, not driver-side private buflet fields.
  - decomp: AppleBCMWLAN driver reference in `docs/reference/AppleBCMWLAN_Bus_decompiled.c` uses packet virtual address/data length/data offset calls around packet copy and RX completion (`+0x1f0`, `+0x140`, `+0x150` in that IO80211NetworkPacket-derived class), not direct `kern_buflet_t` field dereferences in driver code.
- candidate causes:
  - confirmed: local driver bypasses the Tahoe packet data API and dereferences private buflet internals; live CR-097 proves this private dereference is the crash instruction.
  - insufficient for this fix: the later association failure, EAPOL exchange, key install, DHCP, and data transfer remain unverified until RX/TX packet data access no longer panics.
- rejected causes:
  - stale CR-096 vtable slot for `prepareWithQueue`: rejected for this crash; CR-097 disassembly shows the call dispatches through `*0x188`.
  - old `__kern_buflet` packed-layout bug: rejected as the current first failure; CR-097 disassembly loads `buf_ctl` from the correct `0x24` offset, and the new invalid value appears after that corrected load.
  - null `IOSkywalkPacketBuffer`: rejected by control flow; `skywalkRxInput(...)` checks `nBufs == 0 || !bufs[0]` before the direct `mBufletHandle` read.
  - adding pointer guards around `buf_ctl`: rejected as masking; the reference path does not make driver code validate private buflet internals, and non-canonical pointer checks would not establish packet data semantics.
  - retrying allocation/prepare/enqueue: rejected because the panic occurs in a local data-address accessor, not in a recoverable return path.
- confirmed deviation: local TX/RX packet data access uses private `kern_buflet_t` internals where Tahoe exposes and Apple driver code uses packet-level virtual data accessors and packet-buffer data state setters.
- root cause: the local CR-097 RX path reaches a valid prepared `IOSkywalkPacket`, but then leaves the packet API and dereferences `mBufletHandle->buf_ctl` directly. In the live CR-097 crash this private `buf_ctl` is non-canonical, causing a general-protection fault before the RX packet can be copied and enqueued.
- fix: implemented. Local TX/RX now use Tahoe packet-data methods. RX uses `rxPkt->getDataVirtualAddress()` and `rxPkt->setDataOffsetAndLength(0, len)` after `prepareWithQueue(...)`; TX uses `pkt->getDataVirtualAddress()`, `pkt->getDataOffset()`, and `pkt->getDataLength()`. The local copied `__buflet`/`__kern_buflet`/`skmem_bufctl` definitions and `_buflet_*` helpers were removed from driver-side data flow.
- verification: structural verification complete, after-fix runtime pending reviewer approval. `git diff --check` passed; `bash -n scripts/build_tahoe.sh` passed; source scan has no `mBufletHandle`, `_buflet_get`, `_buflet_set`, `__kern_buflet`, `skmem_bufctl`, or `__buflet` references in `AirportItlwmV2.cpp`. Tahoe build succeeded and BootKC symbol verification passed (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-build-20260425-packet-data-api.txt`, sha256 `d30c5a5ac2b78211df1dc9cdda1f3d0ea94211e9d15b10f1219b074f1a33e4a7`). Built binary UUID is `0F1CAB30-C052-3D64-9579-3972E7BD7117`, sha256 `e5c6ca9e17175e369f004caddf40b2db3719bae8251fd26d38171f1e0d0bfa36` (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-build-identity-20260425.txt`, sha256 `f99391c035d93e5f932d0b9310fb45e18c6df8ac7cdf9b72a5407cf20f490828`). Built TX/RX disassembly (`/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-skywalk-tx-rx-disasm-20260425.txt`, sha256 `61e0b151f908ecbe1ae65ccf2bfc69131c6a6de15272d886d888be2b261365fd`) shows TX `getDataVirtualAddress` at `*0x178`, TX `getDataOffset` at `*0x150`, TX `getDataLength` at `*0x140`, RX `getDataVirtualAddress` at `*0x178`, RX `setDataOffsetAndLength` at `*0x158`, and no `_buflet_*` helper calls. Verification checklist artifact: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-verification-checks-20260425.txt`, sha256 `8bac2a60f130898968c8a4bc4d33351a4bcc554e1eebf7833212e6216083d8df`.
- notes: this fix scope is the current crash and the symmetric TX/RX packet data access mismatch. It does not claim that association, key install, DHCP, or data transfer are fixed; those are later scopes after packet data access stops panicking.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-PACKET-DATA-API-024
- symptom: CR-097 loads and shows networks, but a connection attempt panics in `_buflet_get_object_address(...)` from `skywalkRxInput(...)`.
- expected system behavior: after `IOSkywalkPacket::prepareWithQueue(...)`, driver packet payload access must go through the Tahoe packet data methods and packet-buffer state: `getDataVirtualAddress()`, `getDataLength()`, `getDataOffset()`, and `setDataOffsetAndLength(...)`.
- actual behavior: local code reads `IOSkywalkPacketBuffer::mBufletHandle`, dereferences `kern_buflet_t::buf_ctl`, copies through `bc_addr`, and writes `__buflet.__doff/__dlen` directly.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` lines `1448..1462` and `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction(...)` lines `1318..1325`.
- evidence from runtime:
  - `/Users/bob/Projects/itlwm/crash.txt`: CR-097 UUID `CEEB2EC1-072B-385B-9BBF-191E77C470C0`, `RIP ... _buflet_get_object_address + 0x11`, caller `skywalkRxInput + 0x378`, `RAX=0x0fa0000000000000`.
  - `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-skywalk-packet-data-api-evidence-20260425.txt`: loaded/on-disk CR-097 identity, current helper disassembly, current RX disassembly, Tahoe packet vtable, and AppleBCMWLAN packet data reference snippets.
- evidence from decomp:
  - Tahoe `IOSkywalkPacket` vtable places packet data methods before `prepareWithQueue`: `getDataVirtualAddress` at slot `0x178`, `getDataIOVirtualAddress` at `0x180`, `prepareWithQueue` at `0x188`.
  - `IOSkywalkPacket::getDataVirtualAddress()` derives the data pointer from `IOSkywalkPacketBuffer::mMemorySegment` and `mMemorySegmentOffset`, not from `kern_buflet_t::buf_ctl`.
  - Tahoe packet data setters validate requested offset/length against the packet buffer pool buffer size and update packet-buffer `mDataOffset`/`mDataLength`.
  - AppleBCMWLAN driver packet copy/RX completion paths use packet virtual address, data length, and data offset methods; no driver-side `kern_buflet_t` field dereference is present in the reference snippets for these data-flow operations.
- exact semantic mismatch between reference and our code: reference packet data flow treats `kern_buflet_t` as owned by Skywalk internals and exposes data access through packet methods; local code treats `mBufletHandle` as a stable public driver data pointer source and dereferences private `buf_ctl` directly.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - packet preparation with `IOSkywalkPacket::prepareWithQueue(queue, direction, 0)`
    - packet-buffer discovery with `IOSkywalkPacket::getPacketBuffers(...)`
    - RX data pointer with `IOSkywalkPacket::getDataVirtualAddress()`
    - RX packet data state with `IOSkywalkPacket::setDataOffsetAndLength(0, len)`
    - TX data pointer with `IOSkywalkPacket::getDataVirtualAddress()`
    - TX data offset and length with `IOSkywalkPacket::getDataOffset()` and `IOSkywalkPacket::getDataLength()`
    - RX buffer size bound from local pool contract `SKYWALK_BUF_SIZE`, which is the value assigned to `poolOpts.bufferSize`
    - enqueue of prepared RX packets through `IOSkywalkRxCompletionQueue::enqueuePackets(...)`
    - TX delivery through existing `outputPacket(...)`
  - expected contract at each touchpoint:
    - `prepareWithQueue(...)` must run before packet buffer/data access and remain at Tahoe slot `0x188`.
    - `getPacketBuffers(...)` must confirm at least one actual buffer before packet data operations.
    - `getDataVirtualAddress()` must provide the base host pointer for the first packet buffer.
    - `setDataOffsetAndLength(0, len)` must publish packet-buffer data offset and length and return success only when the requested span fits the pool buffer.
    - TX reads must use packet-published offset/length rather than private buflet fields.
    - RX enqueue remains unchanged after the packet data state is set.
  - why no relevant touchpoints are missing:
    - the panic occurs before RX copy and before enqueue, exactly at the private data pointer source.
    - TX has the same private data pointer source and is in the same connection data-plane scope, so it must be corrected with the same packet API to avoid moving the same class of failure to EAPOL TX.
    - queue creation, pool creation, WCL events, association state, key install, DHCP, and internet reachability are later scopes because they cannot be evaluated while packet data access panics.
  - why proposed path adds no extra system-visible side effects:
    - no event replay, callback, retry, timer, forced success, artificial delay, fallback, or state-machine change is added.
    - packet allocation, preparation, buffer discovery, enqueue, and TX `outputPacket(...)` flow remain unchanged.
    - the only behavior change is replacing private field dereferences with existing Tahoe packet data methods that expose the same packet payload state.
- why this is root cause and not just correlation:
  - the panic instruction is inside the local private helper and is reached only because the RX path reads `mBufletHandle` and dereferences `buf_ctl`.
  - CR-097 proves the helper now uses the correct `0x24` layout offset, so the remaining panic is not the previous packing bug.
  - Tahoe decomp provides a packet data API that supplies the data pointer without dereferencing `buf_ctl`, and AppleBCMWLAN driver snippets use packet-level accessors for comparable packet copy/RX operations.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it keeps Skywalk packet ownership inside Skywalk objects and uses their virtual data interface, as the reference driver does.
  - it removes local driver dependence on private `__kern_buflet` and `skmem_bufctl` internals for data movement.
  - it preserves the already proven Tahoe vtable alignment from CR-096.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`: remove local private buflet struct/helper block, update `skywalkTxAction(...)`, update `skywalkRxInput(...)`.
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`: record this anomaly and candidate.
- forbidden alternative fixes considered and rejected:
  - add non-canonical/null guards around `buf_ctl`: rejected as masking private-state misuse.
  - keep direct buflet access but use `kern_buflet_*` external functions: rejected because it still uses the same private buflet handle as the data pointer source and does not match Apple driver packet-data semantics.
  - retry `prepareWithQueue(...)` or `getPacketBuffers(...)`: rejected because both already succeed before the private helper panic.
  - bypass Skywalk RX back to legacy `_if_input(...)`: rejected because it would undo the Tahoe Skywalk path instead of aligning it.
  - synthesize success or suppress RX drops: rejected because the failure is a kernel panic in data access, not a status-reporting issue.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and require no calls to `_buflet_get_object_address`, `_buflet_get_object_limit`, `_buflet_set_data_offset`, or `_buflet_set_data_length`
  - inspect built TX/RX data path and require packet vtable calls to `getDataVirtualAddress` (`*0x178`), RX `setDataOffsetAndLength` (`*0x158`), and no direct `mBufletHandle` read in `skywalkRxInput(...)`
  - create CR-098 Stage 1 request with exact diff artifact and hashes
  - after `APPROVED_FOR_AFTER_FIX_RUNTIME`, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo panic/log/regdiag evidence. Pass criterion for this claim: no panic at `_buflet_get_object_address(...)` and no built reference to the removed local helper path.

## ANOMALY

- id: A-DATA-SKYWALK-RX-ENQUEUE-VTABLE-025
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-098 structurally removes the private `_buflet_*` crash path, but its built RX enqueue call dispatches to the wrong Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(...)` overload and would send an `IOSkywalkPacket **` array through the raw/queue-entry enqueue path.
- first visible manifestation: CR-098 built disassembly of `skywalkRxInput(...)` shows the RX enqueue call as `callq *0x2a0(%rax)`.
- expected system behavior: `skywalkRxInput(...)` passes `const IOSkywalkPacket *pktArray[]` to the Tahoe overload that walks `IOSkywalkPacket` objects, calls packet `completeWithQueue(queue, Rx, 0)`, disposes/completes packet state, builds the packet chain, and enqueues it to Skywalk. Tahoe vtable slot for that overload is `0x2a8`.
- actual behavior: the local SDK declares `enqueuePackets(const IOSkywalkPacket **, ...)` before `enqueuePackets(const queue_entry *, ...)`, so the source call compiles to slot `0x2a0`. Tahoe slot `0x2a0` accepts the raw queue-entry/raw packet path and does not call packet `completeWithQueue(...)` for the supplied packet object array.
- divergence point: `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h` local declaration order at lines `62..64` is `requestEnqueue`, `IOSkywalkPacket ** enqueuePackets`, `queue_entry * enqueuePackets`; Tahoe vtable/decomp shows `requestEnqueue` at `0x298`, raw/queue-entry enqueue at `0x2a0`, and `IOSkywalkPacket **` enqueue at `0x2a8`.
- evidence:
  - disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-098-skywalk-tx-rx-disasm-20260425.txt` (`sha256 61e0b151f908ecbe1ae65ccf2bfc69131c6a6de15272d886d888be2b261365fd`) shows `skywalkRxInput(...)` calls `*0x2a0(%rax)` for enqueue.
  - focused evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-rx-enqueue-vtable-abi-evidence-20260425.txt` (`sha256 d3153c112e3122a139a3b2aafa773676ce3c328aea02c22928d16bb4a9870c1a`) records the CR-098 call site, local header order, Tahoe vtable slots `0x298/0x2a0/0x2a8`, and decomp of the two overloads.
  - decomp: Tahoe `FUN_ffffff8002a59cda` at vtable slot `0x2a0` validates count/capacity and calls raw enqueue helpers `FUN_ffffff8002a3c35e` / `FUN_ffffff8002a3c21c`; it does not call packet virtual `completeWithQueue(...)`.
  - decomp: Tahoe `FUN_ffffff8002a59d84` at vtable slot `0x2a8` treats `param_2` as a packet-object chain/array, calls packet `getDataLength()` via `*0x140`, packet `completeWithQueue(..., 2, 0)` via `*0x198`, packet dispose/completion via `*0x1e0`, and finally enqueues the built chain with `FUN_ffffff80009ca3f0(..., chain, count, 1)`.
- candidate causes:
  - confirmed: local `IOSkywalkRxCompletionQueue.h` overload order is stale relative to Tahoe BootKC and compiles the packet-array enqueue call to slot `0x2a0` instead of `0x2a8`.
- rejected causes:
  - CR-098 packet data API itself: rejected as the first issue; packet data slots `0x178` and `0x158` are correct, but the later RX queue enqueue slot is wrong.
  - changing the source call to a raw queue-entry path: rejected because local source has an `IOSkywalkPacket *` object, and Tahoe packet-object enqueue performs required `completeWithQueue(..., Rx, ...)` ownership transfer.
  - hard-coded vtable call to `0x2a8`: rejected because the header ABI can be aligned and hard-coded calls are brittle.
  - installing CR-098 anyway: rejected because structural evidence already proves the built enqueue call targets the wrong overload before runtime.
- confirmed deviation: local RX completion queue vtable declaration order differs from Tahoe BootKC for the two `enqueuePackets(...)` overloads.
- root cause: CR-098 would remove the buflet panic but then enqueue the prepared RX packet through the raw enqueue overload. That overload is not semantically compatible with `IOSkywalkPacket **` and skips the packet-object `completeWithQueue(..., Rx, ...)` lifecycle path required by Tahoe.
- fix: implemented locally. `scripts/build_tahoe.sh::patch_mackernelsdk` now reorders the local `IOSkywalkRxCompletionQueue.h` overload declarations so `enqueuePackets(const queue_entry *, ...)` precedes `enqueuePackets(const IOSkywalkPacket **, ...)`. The existing `skywalkRxInput(...)` source call now compiles to Tahoe slot `0x2a8`.
- verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - build evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-build-20260425-rx-enqueue-vtable.txt` (`sha256 21b19b45851d57a8893abb78fd1dc547815d75d9959da160122157a42070dad3`) shows `** BUILD SUCCEEDED **` and `OK: all 851 undefined symbols resolve against BootKC`.
  - built binary: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`, UUID `21E25E8B-0BB4-3946-9F0F-F002C5548D3D`, `sha256 9485035f6ced333f203438de63d4614dc74e22d8e7bec5eca1ef27347a55b327`.
  - header check: patched `IOSkywalkRxCompletionQueue.h` lines `62..64` are `requestEnqueue`, `queue_entry * enqueuePackets`, `IOSkywalkPacket ** enqueuePackets`.
  - disassembly evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-skywalk-tx-rx-disasm-20260425.txt` shows `skywalkRxInput(...)` dispatching packet-object RX enqueue through `callq *0x2a8(%rax)` and contains no RX enqueue dispatch through `*0x2a0(%rax)`.
- notes: CR-098 Stage 1 request is superseded by this finding and must not be used for install/runtime. CR-099 must include both CR-098 packet-data API changes and this RX enqueue ABI correction.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-ENQUEUE-VTABLE-025
- symptom: built CR-098 RX path calls `IOSkywalkRxCompletionQueue` vtable slot `0x2a0` for `enqueuePackets(pktArray, 1, 0)`, but Tahoe slot `0x2a0` is the raw/queue-entry overload, not the `IOSkywalkPacket **` overload.
- expected system behavior: RX packet-object enqueue must dispatch to Tahoe slot `0x2a8`, where the queue walks the packet object chain, calls packet `completeWithQueue(queue, Rx, 0)`, disposes/completes packet state, builds the kernel packet chain, and enqueues it.
- actual behavior: local header order maps the `IOSkywalkPacket **` overload to slot `0x2a0`.
- exact divergence point: `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h` declaration order for the two `enqueuePackets(...)` overloads.
- evidence from runtime/build:
  - CR-098 built disassembly shows `skywalkRxInput(...)` enqueue dispatch as `callq *0x2a0(%rax)`.
  - No after-fix runtime is needed or allowed for this finding because the wrong vtable slot is proven structurally before install.
- evidence from decomp:
  - Tahoe `IOSkywalkRxCompletionQueue_vptr`: `0x298` = request enqueue, `0x2a0` = raw/queue-entry enqueue, `0x2a8` = packet-object enqueue.
  - Tahoe slot `0x2a8` function calls packet `getDataLength()`, packet `completeWithQueue(..., 2, 0)`, and packet completion/dispose before enqueuing the built packet chain.
  - Tahoe slot `0x2a0` calls raw enqueue helpers directly and does not perform packet-object completion.
- exact semantic mismatch between reference and our code: our source passes a packet object array but the stale header compiles that source call to the raw enqueue ABI slot, bypassing required packet-object lifecycle semantics.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkRxCompletionQueue::requestEnqueue(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(const queue_entry *, UInt32, IOOptionBits)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(const IOSkywalkPacket **, UInt32, IOOptionBits)`
    - packet `completeWithQueue(..., kIOSkywalkPacketDirectionRx, 0)` invoked by the packet-object enqueue overload
    - packet `getDataLength()` and packet completion/dispose invoked before final Skywalk chain enqueue
  - expected contract at each touchpoint:
    - request enqueue remains at `0x298`.
    - raw/queue-entry enqueue remains at `0x2a0`.
    - packet-object enqueue must be at `0x2a8`.
    - local `skywalkRxInput(...)` packet-array call must dispatch to `0x2a8`.
  - why no relevant touchpoints are missing:
    - the only local source call to RX completion queue `enqueuePackets(...)` passes `const IOSkywalkPacket *pktArray[]`.
    - decomp identifies exactly which overload performs the required packet-object lifecycle transfer.
    - TX submission and packet data slots are separate vtables and are already covered by CR-096/CR-098.
  - why proposed path adds no extra system-visible side effects:
    - no runtime logic, retry, event, timer, payload, forced state, or fallback is added.
    - the source call stays identical; only the local SDK declaration order is aligned so the virtual dispatch reaches the Tahoe implementation for that source type.
- why this is root cause and not just correlation:
  - the built binary already proves the wrong call target (`*0x2a0`).
  - Tahoe decomp proves the semantics of `0x2a0` and `0x2a8`.
  - the local header order directly determines the compiled virtual slot.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it makes the local header vtable order match Tahoe BootKC and sends the packet object array to the reference packet-object enqueue implementation.
- files/functions to modify:
  - `scripts/build_tahoe.sh::patch_mackernelsdk`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - hard-code vtable slot `0x2a8` in driver code: rejected; header ABI alignment is the correct structural fix.
  - cast the packet array to `queue_entry *`: rejected; it would explicitly select the wrong raw path.
  - ignore the issue and install CR-098: rejected; structural evidence already proves the wrong target.
  - add retries or fallback enqueue calls: rejected as masking/guessing and would risk duplicate packet delivery.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - run `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect patched `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h` and require raw/queue-entry overload before packet-object overload
  - inspect built `skywalkRxInput(...)` and require RX enqueue dispatch through `*0x2a8(%rax)`, not `*0x2a0(%rax)`
  - create CR-099 Stage 1 request superseding CR-098
  - only after approval, install without unloading, reboot, and collect one connection-attempt runtime.
- implementation verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - build evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-build-20260425-rx-enqueue-vtable.txt` (`sha256 21b19b45851d57a8893abb78fd1dc547815d75d9959da160122157a42070dad3`).
  - built binary UUID: `21E25E8B-0BB4-3946-9F0F-F002C5548D3D`.
  - built binary sha256: `9485035f6ced333f203438de63d4614dc74e22d8e7bec5eca1ef27347a55b327`.
  - disassembly evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-skywalk-tx-rx-disasm-20260425.txt` confirms RX enqueue uses `callq *0x2a8(%rax)`.

## ANOMALY

- id: A-DATA-SKYWALK-RX-ENQUEUE-OVERLOAD-SHAPE-026
- status: CONFIRMED_DEVIATION
- symptom: CR-099 corrected the RX enqueue call away from slot `0x2a0`, but further structural review shows that this was the wrong correction: the built CR-099 binary passes a stack array of packet pointers to Tahoe slot `0x2a8`, while Tahoe slot `0x2a8` expects a direct packet/chain head pointer.
- first visible manifestation: CR-099 built `skywalkRxInput(...)` disassembly shows the source call `fRxQueue->enqueuePackets(pktArray, 1, 0)` dispatching through `callq *0x2a8(%rax)`.
- expected system behavior: when local source passes an array of one packet pointer, the virtual dispatch must target Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, UInt32, IOOptionBits)` at slot `0x2a0`.
- actual behavior: CR-099 dispatches the same array argument to Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket *, UInt32, IOOptionBits)` at slot `0x2a8`.
- divergence point: `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h` local declarations and `scripts/build_tahoe.sh::patch_mackernelsdk` CR-099 patch model the second overload as `queue_entry *` and reorder it ahead of the packet-array overload; this makes the packet-array source call compile to the chain/head overload slot.
- evidence:
  - structural evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-100-rx-enqueue-overload-shape-evidence-20260425.txt` (`sha256 b49840b174fffc3082507583ccee73984ad91770510bc040b129ae24fc651074`).
  - Tahoe symbols: `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket* const*, unsigned int, unsigned int)` is at `0xffffff8002a59cda`; `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket*, unsigned int, unsigned int)` is at `0xffffff8002a59d84`.
  - vtable evidence from CR-099: request enqueue is slot `0x298`; slot `0x2a0` maps to `0xffffff8002a59cda`; slot `0x2a8` maps to `0xffffff8002a59d84`.
  - CR-099 disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-099-skywalk-tx-rx-disasm-20260425.txt` (`sha256 7d7b0ae6ff4c4876267f6a3db765015e21c5cdb2af883c52ae40af3008598ec1`) shows `callq *0x2a8(%rax)`.
  - decomp: Tahoe slot `0x2a0` helper walks `*(param_2 + index * 8)` as packet objects, calls packet `getDataLength()` via `*0x140`, packet `completeWithQueue(..., 2, 0)` via `*0x198`, packet dispose via `*0x1e0`, then enqueues the built chain.
  - decomp: Tahoe slot `0x2a8` treats `param_2` itself as a packet object/chain head, reads packet fields such as `param_2[5]`, and invokes packet vtable methods on `param_2`; it is not compatible with a stack `IOSkywalkPacket *[1]` array.
- candidate causes:
  - confirmed: CR-099 mixed up overload order with overload shape. It reached a real Tahoe slot, but not the slot matching the source argument shape.
- rejected causes:
  - `0x2a0` as an incompatible raw/queue-entry path: rejected by Tahoe symbols and helper decomp; for the packet-array overload, `0x2a0` is the correct array path.
  - keeping CR-099 and changing only the source array constness: rejected because the header would still route the source to the chain/head slot.
  - passing `rxPkt` directly to `0x2a8`: rejected for this batch because the local code currently constructs an array and the array overload has direct Tahoe symbol/decomp support for `IOSkywalkPacket * const *`.
- confirmed deviation: local RX completion queue header must declare Tahoe overloads as packet-array at `0x2a0` and packet-chain/head at `0x2a8`; CR-099 declares/reorders them incorrectly.
- root cause: CR-099 would call the direct packet/head overload with the address of a stack packet-pointer array. The Tahoe implementation would interpret that stack address as an `IOSkywalkPacket` object and dereference object fields/vtable through the wrong base pointer.
- fix: implemented locally for CR-100. `scripts/build_tahoe.sh::patch_mackernelsdk` now rewrites local `IOSkywalkRxCompletionQueue.h` declarations to:
  - `enqueuePackets(IOSkywalkPacket * const * packets, UInt32 packetCount, IOOptionBits options)` at slot `0x2a0`
  - `enqueuePackets(IOSkywalkPacket * packet, UInt32 packetCount, IOOptionBits options)` at slot `0x2a8`
  and `skywalkRxInput(...)` now constructs the local packet array as `IOSkywalkPacket * const pktArray[]`.
- verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - build evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-100-build-20260425-rx-enqueue-overload-shape.txt` (`sha256 6a87b89c574523897911c6558c5ac8ead23d0fc93527b154112efdd7e709677a`) shows `** BUILD SUCCEEDED **` and `OK: all 851 undefined symbols resolve against BootKC`.
  - built binary: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`, UUID `0F1CAB30-C052-3D64-9579-3972E7BD7117`, `sha256 e5c6ca9e17175e369f004caddf40b2db3719bae8251fd26d38171f1e0d0bfa36`.
  - header check: patched `IOSkywalkRxCompletionQueue.h` lines `62..64` are `requestEnqueue`, `IOSkywalkPacket * const * enqueuePackets`, `IOSkywalkPacket * enqueuePackets`.
  - disassembly evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-100-skywalk-tx-rx-disasm-20260425.txt` (`sha256 b19fa69c45d74d64671693f5235943118efbda38e95d931cb521b5a9465cee5c`) confirms RX packet-array enqueue dispatches through `callq *0x2a0(%rax)`.
- notes: CR-099 Stage 1 request is superseded by this finding and must not be used for install/runtime.
- notes: CR-099 Stage 1 request is superseded by this finding and must not be used for install/runtime.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-ENQUEUE-OVERLOAD-SHAPE-026
- symptom: CR-099 builds `skywalkRxInput(...)` so the RX packet array dispatches to Tahoe slot `0x2a8`, whose implementation expects a direct packet/chain head pointer.
- expected system behavior: `IOSkywalkPacket * const *` array source calls must dispatch to Tahoe slot `0x2a0`; direct `IOSkywalkPacket *` chain/head source calls must dispatch to Tahoe slot `0x2a8`.
- actual behavior: CR-099 source passes `const IOSkywalkPacket *pktArray[]`, but the patched header order maps this call to slot `0x2a8`.
- exact divergence point: `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h` overload declarations generated by `scripts/build_tahoe.sh::patch_mackernelsdk`.
- evidence from runtime: no after-fix runtime is allowed for CR-099 because structural ABI evidence already proves the argument-shape mismatch before install.
- evidence from decomp:
  - Tahoe symbol table names `0xffffff8002a59cda` as `enqueuePackets(IOSkywalkPacket* const*, UInt32, IOOptionBits)` and `0xffffff8002a59d84` as `enqueuePackets(IOSkywalkPacket*, UInt32, IOOptionBits)`.
  - Tahoe slot `0x2a0` dereferences `param_2 + index * 8`, consistent with an array of packet pointers.
  - Tahoe slot `0x2a8` treats `param_2` itself as a packet object, reads `param_2[5]`, and calls packet vtable methods on `param_2`.
- exact semantic mismatch between reference and our code: the reference has two packet overloads, array and direct chain/head; CR-099 models them as raw/queue-entry and packet-array, causing the array argument to select the direct chain/head overload.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkRxCompletionQueue::requestEnqueue(...)` slot `0x298`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, UInt32, IOOptionBits)` slot `0x2a0`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket *, UInt32, IOOptionBits)` slot `0x2a8`
    - local `skywalkRxInput(...)` packet array argument
  - expected contract at each touchpoint:
    - request enqueue remains at `0x298`.
    - packet-array enqueue must remain at `0x2a0`.
    - packet-chain/head enqueue must remain at `0x2a8`.
    - local array source call must have type `IOSkywalkPacket * const *`.
  - why no relevant touchpoints are missing:
    - the only local source call to RX completion queue `enqueuePackets(...)` constructs a one-element packet pointer array.
    - decomp and symbols identify both overloads and their distinct argument shapes.
    - successful RX payload preparation, packet data slots, and pool allocation slots are separate already-covered touchpoints and are not changed by this fix.
  - why proposed path adds no extra system-visible side effects:
    - no runtime logic, retry, fallback, timer, event, state, payload, or ownership transfer is added.
    - the source continues to enqueue exactly one prepared RX packet; only the header ABI and local array constness are aligned so the virtual dispatch reaches the matching Tahoe overload.
- why this is root cause and not just correlation:
  - CR-099 disassembly proves the wrong slot.
  - Tahoe symbols prove the overload shape of both slots.
  - Tahoe decomp proves slot `0x2a8` would dereference the stack array pointer as an object pointer.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it declares the same two Tahoe overload shapes and routes the existing packet-array enqueue through the reference packet-array implementation.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `scripts/build_tahoe.sh::patch_mackernelsdk`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - keep CR-099 and install: rejected because it would pass a stack array to the direct packet/head overload.
  - hard-code vtable slot `0x2a0`: rejected because header ABI alignment is the correct structural fix.
  - cast the array to the wrong type: rejected because it would hide the ABI mismatch.
  - switch to direct `rxPkt` chain/head enqueue in this batch: rejected because the current source architecture is packet-array enqueue and the array overload is confirmed.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - run `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect patched `IOSkywalkRxCompletionQueue.h` and require array overload at `0x2a0` before direct packet/head overload at `0x2a8`
  - inspect built `skywalkRxInput(...)` and require RX enqueue dispatch through `*0x2a0(%rax)`, not `*0x2a8(%rax)`
  - create CR-100 Stage 1 request superseding CR-099
  - only after approval, install without unloading, reboot, and collect one connection-attempt runtime.
- implementation verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - build evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-100-build-20260425-rx-enqueue-overload-shape.txt` (`sha256 6a87b89c574523897911c6558c5ac8ead23d0fc93527b154112efdd7e709677a`).
  - built binary UUID: `0F1CAB30-C052-3D64-9579-3972E7BD7117`.
  - built binary sha256: `e5c6ca9e17175e369f004caddf40b2db3719bae8251fd26d38171f1e0d0bfa36`.
  - disassembly evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-100-skywalk-tx-rx-disasm-20260425.txt` confirms RX packet-array enqueue uses `callq *0x2a0(%rax)`.

## ANOMALY

- id: A-DATA-SKYWALK-TX-CALLBACK-CONSUMED-027
- status: CONFIRMED_DEVIATION
- symptom: the Tahoe TX dequeue path treats the TX action return value as the number of Skywalk packets consumed from the submission queue, but local `skywalkTxAction(...)` returns only the number that reached `outputPacket(...)`. If every packet in a non-empty callback is locally dropped before `outputPacket(...)`, the callback returns `0` and Tahoe enters a non-returning trap path.
- first visible manifestation: structural audit of Tahoe `IOSkywalkTxSubmissionQueue` after CR-100 found the callback return is used to advance the queue read index and that `uVar4 == 0` from the action enters `FUN_ffffff80004c1af0()`.
- expected system behavior: for each packet entry consumed from the Skywalk TX submission callback, the driver must return that consumed count even when the packet is locally dropped because data extraction or mbuf allocation failed.
- actual behavior: local `skywalkTxAction(...)` increments `sent` only after `outputPacket(m, NULL)`, so packet-buffer failures, invalid data spans, `mbuf_allocpacket(...)` failures, and `mbuf_copyback(...)` failures are dropped without contributing to the callback return.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction(...)`, return value and `sent++` placement.
- evidence:
  - decomp: Tahoe `FUN_ffffff8002a58dc8` / `FUN_ffffff8002a58eec` call the registered TX action with `(owner, queue, packetArray, availableCount, refCon)`, add the returned value to internal packet counters, and advance the ring by that returned value.
  - decomp: the same functions enter `FUN_ffffff80004c1af0()` when the action returns `0` for a non-empty available packet span.
  - local source: `skywalkTxAction(...)` returns `sent`, and `sent` increments only after `outputPacket(...)`, not after consuming a Skywalk packet entry.
  - local source: pre-output failure branches all `continue` after `sRT.txPktDrop++`, so a non-empty callback can return `0`.
- candidate causes:
  - confirmed: local callback return semantics are "delivered to `outputPacket`", while Tahoe's queue contract requires "consumed from Skywalk submission queue".
- rejected causes:
  - forcing output success: rejected because `outputPacket(...)` owns mbuf delivery/drop and must still report its real result.
  - retrying failed mbuf allocation/copy: rejected as retry/fallback not present in the system contract.
  - returning `count` without processing packet data: rejected for normal packets because it would drop valid traffic; the fix must still try to deliver every packet and only consume/drop when local extraction fails.
- confirmed deviation: the callback result is a queue-consumption contract, not a hardware-delivery success count.
- root cause: a future non-empty TX callback where every local packet extraction/copy path fails would deterministically return `0` and trigger Tahoe's non-returning queue assertion path.
- fix: planned for CR-101. Track `delivered` separately for telemetry, but return the number of packet entries consumed from the callback. Count local extraction/copy/output failures as consumed drops, not as unconsumed queue entries.
- verification:
  - inspect source to confirm every non-empty callback with a valid packet array returns the number of consumed entries, not delivered entries.
  - inspect built `skywalkTxAction(...)` for callback return fed by consumed/count accounting.
  - build Tahoe binary and verify BootKC symbol resolution.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-TX-CALLBACK-CONSUMED-027
- symptom: local TX callback can return `0` for a non-empty Skywalk submission span when all packets are dropped before `outputPacket(...)`.
- expected system behavior: `IOSkywalkTxSubmissionQueue` action return is the number of packet entries consumed from the queue.
- actual behavior: `skywalkTxAction(...)` returns the number of packets that reached `outputPacket(...)`.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction(...)`.
- evidence from runtime/build:
  - CR-100 built disassembly proves the current TX callback uses the packet data API and then returns local `sent` accounting.
  - no after-fix runtime is required before this structural correction because Tahoe decomp proves the zero-return path enters a non-returning trap.
- evidence from decomp:
  - Tahoe `IOSkywalkTxSubmissionQueue` dequeue helpers call the action pointer and use its return value to advance the packet ring/read index.
  - if the action returns `0` while packet entries were available, Tahoe calls `FUN_ffffff80004c1af0()`.
- exact semantic mismatch between reference and our code: Tahoe requires callback return to mean consumed queue entries; local code makes it mean successfully handed to the legacy `outputPacket(...)` path.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkTxSubmissionQueueAction` callback return value
    - Skywalk packet data extraction through `getPacketBuffers(...)`, `getDataVirtualAddress()`, `getDataOffset()`, `getDataLength()`
    - mbuf allocation/copy from Skywalk packet data
    - legacy `outputPacket(...)` delivery/drop ownership of the mbuf
  - expected contract at each touchpoint:
    - callback return is consumed packet entries, not lower-layer delivery success.
    - invalid packet data and local allocation/copy failures are local drops after the entry has been inspected/consumed.
    - `outputPacket(...)` continues to own and free or enqueue the mbuf it receives.
  - why no relevant touchpoints are missing:
    - the TX callback receives only a packet array and count and returns only a count; there is no separate per-packet nack/retry surface in the action ABI.
    - all local pre-output drop branches are inside the same callback and therefore must be represented in the same return count.
  - why proposed path adds no extra system-visible side effects:
    - no retry, delay, replay, forced link state, payload mutation, or extra callback is added.
    - valid packets still follow the same `outputPacket(...)` path.
    - dropped packets were already dropped locally; only the callback return now reports them as consumed so Skywalk does not re-enter/assert on a zero-progress callback.
- why this is root cause and not just correlation:
  - the decomp proves the exact non-returning condition on action return `0`.
  - local source proves every pre-output drop branch can keep `sent == 0`.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it satisfies the Tahoe queue consumption contract while preserving the existing driver-owned TX delivery path.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - return forced success from `outputPacket(...)`: rejected because it masks real lower-layer drops.
  - panic/abort on invalid packet entries: rejected because Tahoe already requires non-zero progress from the callback.
  - add retry/poll/backoff around mbuf failures: rejected as unproven fallback behavior.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkTxAction(...)` and confirm local drops no longer make the callback return `0` for non-empty packet spans
  - create CR-101 Stage 1 request superseding CR-100

## ANOMALY

- id: A-DATA-SKYWALK-RX-ERROR-OWNERSHIP-028
- status: CONFIRMED_DEVIATION
- symptom: if RX packet-array enqueue fails, local `skywalkRxInput(...)` frees the source mbuf but does not return the prepared `IOSkywalkPacket` to `fRxPool`, leaking a packet/pool entry on a failed ownership-transfer path.
- first visible manifestation: structural audit of the CR-100 RX path found `ret != kIOReturnSuccess` after `fRxQueue->enqueuePackets(pktArray, 1, 0)` records telemetry and returns `EIO` without `fRxPool->deallocatePacket(rxPkt)`.
- expected system behavior: the RX packet remains driver/pool-owned until Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)` accepts it and returns success; on pre-transfer failure the producer must deallocate it.
- actual behavior: local code deallocates `rxPkt` on allocation/prepare/buffer/data setup failures, but not on enqueue failure.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`, `ret != kIOReturnSuccess` branch after RX completion queue enqueue.
- evidence:
  - decomp: Tahoe array enqueue `FUN_ffffff8002a59cda` returns non-success before calling the transfer helpers when the queue is missing, disabled, blocked, or the arguments/capacity are invalid.
  - decomp: on the success path, `FUN_ffffff8002a59cda` calls helper `FUN_ffffff8002a3c21c` / `FUN_ffffff8002a3c35e`; those helpers walk the packet array, call packet `completeWithQueue(..., 2, 0)`, and perform completion/dispose/queue enqueue operations before the top-level function returns `0`.
  - local source: all earlier RX failure branches call `fRxPool->deallocatePacket(rxPkt)`; the enqueue-failure branch does not.
- candidate causes:
  - confirmed: local code treats enqueue failure as if ownership might have transferred, but Tahoe's non-success returns happen before transfer helpers run.
- rejected causes:
  - deallocate after successful enqueue: rejected because Tahoe success path has consumed/completed the packet.
  - retry enqueue on failure: rejected as unproven fallback and potential duplicate delivery.
  - ignore the leak because the path is rare: rejected because queue disabled/race paths are part of RX lifecycle coverage.
- confirmed deviation: RX ownership transfer is not balanced on the enqueue failure branch.
- root cause: an enqueue failure leaves a prepared packet allocated in the RX pool, which can exhaust pool capacity over repeated failures and move the data path to later allocation drops.
- fix: planned for CR-101. On `ret != kIOReturnSuccess`, deallocate `rxPkt` before returning the error.
- verification:
  - source check that every post-allocation pre-success failure branch deallocates `rxPkt`.
  - disassembly check that the enqueue-failure branch contains the pool `deallocatePacket(...)` call after RX enqueue returns non-success.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-ERROR-OWNERSHIP-028
- symptom: `skywalkRxInput(...)` leaks a prepared RX packet when `IOSkywalkRxCompletionQueue::enqueuePackets(...)` returns non-success.
- expected system behavior: packet ownership transfers to Skywalk only on successful enqueue; otherwise the driver must return the packet to the pool.
- actual behavior: enqueue failure records diagnostics and returns `EIO` without deallocating `rxPkt`.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`, `ret != kIOReturnSuccess` branch.
- evidence from runtime/build:
  - CR-100 source/disassembly establishes the current branch shape and that RX enqueue dispatches to the Tahoe packet-array overload.
  - after-fix runtime is not required before this structural ownership correction because the non-success ownership path is proven by Tahoe decomp.
- evidence from decomp:
  - Tahoe `FUN_ffffff8002a59cda` returns `0xe00002c2`, `0xe00002d8`, `0xe00002d7`, or `0xe00002d5` before transfer helpers run.
  - only the success path calls helpers that complete/dispose/enqueue the packet array and then returns `0`.
- exact semantic mismatch between reference and our code: local code does not reclaim a packet on a failed ownership-transfer call, while Tahoe only consumes the packet on success.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkPacketBufferPool::allocatePacket(...)`
    - `IOSkywalkPacket::prepareWithQueue(...)`
    - `IOSkywalkPacket::setDataOffsetAndLength(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)`
    - `IOSkywalkPacketBufferPool::deallocatePacket(...)`
  - expected contract at each touchpoint:
    - allocation gives the driver a pool packet.
    - preparation/data setup keeps the driver responsible until enqueue accepts the packet.
    - successful enqueue consumes/transfers the packet.
    - failed enqueue leaves the packet with the driver/pool and requires deallocation.
  - why no relevant touchpoints are missing:
    - all RX post-allocation branches either fail before enqueue or succeed at enqueue; this fix covers the only missing pre-success deallocation branch.
  - why proposed path adds no extra system-visible side effects:
    - no retry, extra enqueue, event, payload change, forced state, or callback is added.
    - the packet is reclaimed only on a path where Tahoe returned non-success before ownership transfer.
- why this is root cause and not just correlation:
  - decomp proves non-success returns precede transfer helper invocation.
  - source proves the local non-success branch omits deallocation.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it keeps ownership with the producer until the reference enqueue implementation accepts the packet.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - deallocate unconditionally after enqueue: rejected because success path transfers ownership.
  - retry enqueue after failure: rejected as unproven fallback and duplicate-delivery risk.
  - suppress the enqueue error: rejected because it masks a real queue state failure.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and confirm failed enqueue branches to pool `deallocatePacket(...)`
  - create CR-101 Stage 1 request superseding CR-100

## ANOMALY

- id: A-DATA-SKYWALK-RX-COPYDATA-029
- status: CONFIRMED_DEVIATION
- symptom: RX can publish a Skywalk packet whose payload copy from the source mbuf failed, because `skywalkRxInput(...)` ignores the `errno_t` returned by `mbuf_copydata(...)`.
- first visible manifestation: source audit of the CR-100 RX path found `mbuf_copydata(m, 0, len, objAddr);` followed unconditionally by `rxPkt->setDataOffsetAndLength(...)` and RX enqueue.
- expected system behavior: RX packet data must be copied successfully into the Skywalk packet buffer before data length is published and the packet is enqueued.
- actual behavior: local code publishes length/enqueues even if `mbuf_copydata(...)` returns an error.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`, mbuf-to-Skywalk buffer copy step.
- evidence:
  - SDK header: `MacKernelSDK/Headers/sys/kpi_mbuf.h` declares `extern errno_t mbuf_copydata(const mbuf_t mbuf, size_t offset, size_t length, void *out_data);`.
  - local source: the return value is currently discarded.
  - local RX sequence: `setDataOffsetAndLength(0, len)` and `enqueuePackets(...)` run after the discarded result, so a failed copy can expose invalid payload bytes to the system.
- candidate causes:
  - confirmed: RX data publication lacks the required local copy success gate.
- rejected causes:
  - retry copy: rejected as unproven fallback.
  - enqueue zero-length/partial data on copy failure: rejected because the source frame has already failed the payload copy contract.
  - suppress diagnostics: rejected because the existing regdiag data result should record the failure.
- confirmed deviation: local RX path does not check a failing system KPI before publishing packet data.
- root cause: a copy failure would turn a local RX input error into corrupted data delivery instead of a clean drop and packet deallocation.
- fix: planned for CR-101. Check `mbuf_copydata(...)` return, deallocate `rxPkt`, record diagnostics, free the source mbuf, and return `EIO` before setting data length/enqueue.
- verification:
  - source check that RX data length/enqueue are reachable only after `mbuf_copydata(...) == 0`.
  - build/disassembly check that the copy result is tested before `setDataOffsetAndLength(...)`.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-COPYDATA-029
- symptom: `skywalkRxInput(...)` ignores `mbuf_copydata(...)` failure and can publish invalid RX payload.
- expected system behavior: only successfully copied mbuf payload is published via `IOSkywalkPacket::setDataOffsetAndLength(...)` and RX enqueue.
- actual behavior: copy result is ignored.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`.
- evidence from runtime/build:
  - CR-100 source/disassembly establishes the current RX copy/publish sequence.
  - this is a structural RX contract correction; after-fix runtime is only needed after Stage 1 approval with the combined CR-101 build.
- evidence from decomp:
  - Tahoe Skywalk packet enqueue consumes the data length already present on the packet; it does not validate that the producer successfully copied the mbuf payload.
- exact semantic mismatch between reference and our code: the local producer marks/publishes packet data even when the local mbuf copy API reports failure.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `mbuf_copydata(...)`
    - `IOSkywalkPacket::setDataOffsetAndLength(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(...)`
    - `IOSkywalkPacketBufferPool::deallocatePacket(...)`
  - expected contract at each touchpoint:
    - copy success is required before data length publication.
    - failed copy leaves the packet driver-owned and must deallocate it.
    - enqueue is only reached with valid copied data.
  - why no relevant touchpoints are missing:
    - the RX payload enters Skywalk only through this copy, length publication, and enqueue sequence.
  - why proposed path adds no extra system-visible side effects:
    - no retry, fallback, duplicate enqueue, event, or state change is added.
    - only an error path that already cannot deliver valid payload is converted into a clean drop.
- why this is root cause and not just correlation:
  - the source and SDK signature prove a failure result is possible and currently ignored before data publication.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it preserves the same RX producer path and enforces the required local copy-before-publish ordering.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - retry copy: rejected as fallback.
  - publish empty data: rejected because it would mutate packet payload semantics.
  - ignore the return because failures are rare: rejected because this is the only gate before publishing bytes to Skywalk.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and confirm `setDataOffsetAndLength(...)` is reachable only after copy success
  - include this check in CR-101 Stage 1 request

## IMPLEMENTATION VERIFICATION

- id: CR-101-SKYWALK-RX-TX-LIFECYCLE
- status: FIX_IMPLEMENTED
- anomalies covered:
  - `A-DATA-SKYWALK-TX-CALLBACK-CONSUMED-027`
  - `A-DATA-SKYWALK-RX-ERROR-OWNERSHIP-028`
  - `A-DATA-SKYWALK-RX-COPYDATA-029`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction(...)` now tracks `consumed` separately from `delivered` and returns `consumed`.
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` now checks `mbuf_copydata(...)` before `setDataOffsetAndLength(...)`.
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` now deallocates `rxPkt` on `enqueuePackets(...)` failure.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-build-20260425-skywalk-rx-tx-lifecycle.txt`
  - build log sha256: `7df1a7ada641e489146d8b4ca2715753c1ed4115ff27996abce085393ba1b182`
  - built binary UUID: `90488F96-8149-393F-B04A-0E8204F6461B`
  - built binary sha256: `c5d31c455edc43a1f5ec64454b3351c6b4ea96ca8fb213b8c096b8ef615e4333`
- disassembly verification:
  - TX disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-skywalkTxAction-disasm-20260425.txt`
  - TX disassembly sha256: `4d279e0cb0fcd400ffb6aec3ad5ec410b887ab1edaa710b09fca581b519efa91`
  - TX disassembly shows `getPacketBuffers` at `*0x120`, `getDataVirtualAddress` at `*0x178`, `getDataOffset` at `*0x150`, `getDataLength` at `*0x140`, `mbuf_allocpacket`, `mbuf_copyback`, and final return from the consumed counter.
  - RX disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-skywalkRxInput-disasm-20260425.txt`
  - RX disassembly sha256: `a23774ece2b5d697a16430a51d690357e9f7363ddd3beefa19b7d10d909412d9`
  - RX disassembly shows `allocatePacket` at `*0x130`, `prepareWithQueue` at `*0x188`, `getDataVirtualAddress` at `*0x178`, `mbuf_copydata` checked before `setDataOffsetAndLength` at `*0x158`, packet-array enqueue at `*0x2a0`, and failed enqueue branch calling pool `deallocatePacket` at `*0x140`.
- diff artifact:
  - `/Users/bob/Projects/itlwm/commit-approval/artifacts/CR-101-skywalk-rx-tx-lifecycle.diff`
  - final sha256 and line count are recorded in the CR-101 Stage 1 request to avoid self-referential diff hash churn.
- notes:
  - CR-101 supersedes CR-100 Stage 1 request for install/runtime.
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-DATA-SKYWALK-RX-PREPARED-DEALLOC-PANIC-030
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-101 panics during an association attempt in the RX Skywalk path with `"default packetCompletion()" @IOSkywalkPacketQueue.cpp:321`.
- first visible manifestation: user-installed CR-101 binary UUID `90488F96-8149-393F-B04A-0E8204F6461B` showed networks, then the first connection attempt crashed; panic file `/Users/bob/Projects/itlwm/crash.txt` reports `IOSkywalkPacketBufferPool::deallocatePacket(...) + 0x4a` called from `skywalkRxInput + 0x4ee`.
- expected system behavior: once an `IOSkywalkPacket` has successfully passed `prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)`, failure cleanup must first complete the packet with the RX queue semantics before returning it to the pool.
- actual behavior: CR-101 calls `fRxPool->deallocatePacket(rxPkt)` directly after the packet has entered or attempted to enter the prepared RX queue lifecycle.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)`, all branches after the `prepareWithQueue(...)` call returns and before successful `fRxQueue->enqueuePackets(...)` that directly call `fRxPool->deallocatePacket(rxPkt)`.
- evidence:
  - panic logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-aftercrash-panic-20260425.txt`, sha256 `52578ef850b14580bb6b29a2f994cf258a07985c6d5a7a09c5e2f395417db4a5`, shows `IOSkywalkPacketQueue::bufferCompletion(...)` -> `IOSkywalkPacketBufferPool::deallocatePacket(...)` -> `skywalkRxInput + 0x4ee`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-aftercrash-log-20260425.txt`, sha256 `84ae50586719c8e9e50a81fd1e9981fa045d200748f4de263c81026473483e4d`; loaded kext UUID in panic is the CR-101 reviewed runtime UUID.
  - disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-101-skywalkRxInput-disasm-20260425.txt`, sha256 `a23774ece2b5d697a16430a51d690357e9f7363ddd3beefa19b7d10d909412d9`; `skywalkRxInput + 0x4ee` is immediately after the failed-enqueue branch call to pool vtable slot `*0x140`.
  - decomp: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-prepared-rx-packet-lifecycle-evidence-20260425.txt`, sha256 `f57b98d42c7460b739474f709f66a7d87c1f9a4619388cd1f0a65bb89c43c2ee`.
  - decomp: `IOSkywalkPacket::prepareWithQueue` stores the source queue and sets prepared state bit `2`.
  - decomp: Tahoe RX completion helpers call packet `completeWithQueue(queue, 2, 0)` before packet disposal on the success path.
  - decomp: `IOSkywalkPacketBufferPool::deallocatePackets` has a special prepared-packet path and then disposes/returns only after the packet state is no longer prepared; direct deallocation of the CR-101 prepared packet reached the queue default completion panic.
- candidate causes:
  - confirmed: CR-101 reclaimed a queue-prepared RX packet through the pool without first running the RX `completeWithQueue(...)` transition.
  - insufficient data: whether association would otherwise reach EAPOL/key/DHCP after the enqueue failure is no longer observable because the cleanup panic terminates the system first.
- rejected causes:
  - RX enqueue overload regression: rejected for this panic because CR-101 disassembly still calls packet-array enqueue through `*0x2a0`.
  - buflet accessor regression: rejected for this panic because CR-101 no longer uses local `_buflet_*` accessors in `skywalkRxInput(...)`.
  - diagnostics side effect: rejected for this panic because regdiag was not enabled, and the crashing call is unconditional CR-101 cleanup code.
- confirmed deviation: local CR-101 code treats `deallocatePacket(...)` as valid for a prepared RX packet, while the Tahoe packet lifecycle requires a queue completion transition for prepared RX packets before pool return.
- root cause: on failed RX enqueue, direct pool deallocation of the prepared packet invokes Skywalk's default completion path and panics before telemetry/logging can run.
- fix: planned for CR-102. Add a narrow helper used only after the `prepareWithQueue(...)` call has returned that calls `rxPkt->completeWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)` and then `fRxPool->deallocatePacket(rxPkt)`. Replace every post-prepare/pre-success direct `deallocatePacket(rxPkt)` branch with that helper. Leave allocation failure ownership unchanged.
- verification:
  - source check: every failure branch after the `prepareWithQueue(...)` call uses the prepared-packet cleanup helper.
  - build check: Tahoe build succeeds and BootKC unresolved-symbol validation passes.
  - disassembly check: post-prepare failure branches call packet slot `*0x198` with direction `2` before pool slot `*0x140`.
  - runtime check after Stage 1 approval: reboot, attempt one join, and verify no `"default packetCompletion()"` panic; if association still fails, collect sudo logs plus regdiag snapshot/trace for the next blocker.
- notes:
  - This supersedes the CR-101 failed-enqueue ownership conclusion. Ownership remains local on enqueue failure, but a prepared packet cannot be returned to the pool through direct deallocation.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-PREPARED-DEALLOC-PANIC-030
- symptom: CR-101 crashes in `IOSkywalkPacketBufferPool::deallocatePacket(...)` from `skywalkRxInput(...)` after a failed RX enqueue.
- expected system behavior: a packet prepared with `prepareWithQueue(fRxQueue, Rx, 0)` must be completed through the RX queue lifecycle before it is returned to the pool.
- actual behavior: failure branches after the `prepareWithQueue(...)` call call `fRxPool->deallocatePacket(rxPkt)` directly.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` after the `prepareWithQueue(...)` call, including prepare non-success, no-buffer, invalid data address/size, copy failure, data-length publish failure, and enqueue failure branches.
- evidence from runtime:
  - panic file `CR-101-aftercrash-panic-20260425.txt`, sha256 `52578ef850b14580bb6b29a2f994cf258a07985c6d5a7a09c5e2f395417db4a5`, shows `default packetCompletion()` reached through `IOSkywalkPacketBufferPool::deallocatePacket(...)` from `skywalkRxInput + 0x4ee`.
  - loaded crashing AirportItlwm UUID is `90488F96-8149-393F-B04A-0E8204F6461B`, matching the CR-101 reviewed runtime binary.
  - CR-101 RX disassembly maps `+0x4ee` to the failed-enqueue cleanup call to pool `deallocatePacket`.
- evidence from decomp:
  - `IOSkywalkPacket::prepareWithQueue` records the source queue and sets prepared state bit `2`.
  - `IOSkywalkPacket::completeWithQueue` walks packet buffers, performs RX queue completion for direction `2`, and clears prepared state bit `2`.
  - Tahoe RX enqueue success helpers call packet `completeWithQueue(queue, 2, 0)` before dispose/enqueue accounting.
  - `IOSkywalkPacketBufferPool::deallocatePackets` is the pool return path after the packet is no longer in prepared queue state; direct prepared deallocation in CR-101 reached the default completion panic.
- exact semantic mismatch between reference and our code: CR-101 returns a queue-prepared packet to the pool without the RX complete transition that Tahoe uses to unwind prepared RX packet state.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkPacketBufferPool::allocatePacket(...)`
    - `IOSkywalkPacket::prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)`
    - packet data inspection/copy/publication through `getPacketBuffers(...)`, `getDataVirtualAddress()`, `mbuf_copydata(...)`, `setDataOffsetAndLength(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)`
    - `IOSkywalkPacket::completeWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)`
    - `IOSkywalkPacketBufferPool::deallocatePacket(...)`
  - expected contract at each touchpoint:
    - allocation gives the driver a pool packet.
    - successful preparation attaches packet/buffers to the RX queue and marks the packet prepared.
    - all local data setup failures before enqueue leave ownership with the driver but still require prepared-state unwind.
    - failed enqueue leaves ownership with the driver and still requires prepared-state unwind.
    - `completeWithQueue(..., Rx, 0)` clears the prepared queue lifecycle state.
    - pool deallocation returns the now-unprepared packet to the pool.
  - why no relevant touchpoints are missing:
    - `skywalkRxInput(...)` has exactly one `prepareWithQueue(...)` call and a finite set of pre-success failure exits after it; the patch covers every exit after that call and does not touch allocation failures before the call.
  - why proposed path adds no extra system-visible side effects:
    - no retry, delay, duplicate enqueue, event replay, forced success, state fabrication, payload mutation, or diagnostic intervention is added.
    - the helper runs only on paths that already drop the mbuf and return an error; it changes only local packet lifecycle unwinding required to avoid the Skywalk default completion panic.
- why this is root cause and not just correlation:
  - runtime panic stack lands exactly on the new CR-101 direct deallocation branch.
  - decomp proves the missing prepared-packet completion transition and the success-path completion sequence.
  - inserting completion before pool return directly removes the invalid lifecycle edge that calls default completion.
- why proposed fix is 1:1 with reference architecture and semantics:
  - the prepared packet is unwound with the same RX `completeWithQueue(queue, 2, 0)` transition used by Tahoe RX completion helpers, then returned to the pool only after prepared state has been cleared.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - remove deallocation on enqueue failure: rejected because it reintroduces the CR-100 packet leak.
  - retry enqueue: rejected as unproven fallback and duplicate-delivery risk.
  - suppress or ignore the enqueue error: rejected because it masks a real queue state failure.
  - call `disposePacket()` directly without pool return: rejected because it may drop the packet handle without returning it to the pool.
  - deallocate after successful enqueue: rejected because success transfers ownership to Tahoe.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and confirm failure branches after `prepareWithQueue(...)` call `completeWithQueue` slot `*0x198` with `edx=2` before pool `deallocatePacket` slot `*0x140`
  - create CR-102 Stage 1 request; do not install or commit until reviewer allows after-fix runtime

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: `prepareWithQueue`, `completeWithQueue`, RX enqueue success helpers, pool deallocation path, and default completion panic sites are in the IOSkywalkFamily decomp evidence artifact.
- Есть ли прямое подтверждение по runtime-данным? Да: CR-101 panic stack lands in `IOSkywalkPacketBufferPool::deallocatePacket(...)` from `skywalkRxInput + 0x4ee` on the CR-101 binary UUID.
- Доказал ли я причинность, а не просто корреляцию? Да: the disassembly offset maps the panic return address to the direct deallocation call that was newly added by CR-101; decomp explains why direct deallocation of prepared packet reaches invalid completion.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Да within the system contract: prepared RX packet state is unwound through the Tahoe RX `completeWithQueue(queue, 2, 0)` lifecycle before pool return.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Нет.
- Не закрываю ли я симптом вместо причины? Нет: the exact invalid lifecycle edge is removed for every RX cleanup branch after the packet attempts to enter prepared queue lifecycle.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: all are listed above with artifact paths and sha256 values.

## IMPLEMENTATION VERIFICATION

- id: CR-102-PREPARED-RX-PACKET-CLEANUP
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-DATA-SKYWALK-RX-PREPARED-DEALLOC-PANIC-030`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxReturnPreparedPacket(...)` calls `completeWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)` before `fRxPool->deallocatePacket(rxPkt)`.
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` uses this helper for every failure branch after the `prepareWithQueue(...)` call: prepare non-success, no packet buffers, invalid data address/size, copy failure, data-length publication failure, and enqueue failure.
  - allocation failure remains unchanged because no packet entered the prepared RX queue lifecycle.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-build-20260425-prepared-rx-cleanup.txt`
  - build log sha256: `0674dd109ee6d9d6ab77b718444787d959287e8699df28b927fa0581dd0c4968`
  - built binary UUID: `84A26605-1A2A-34D0-927B-3E06707070E1`
  - built binary sha256: `1f2946562a71e429ae0b6ea239957c1926e7aab704b3a305c4a01c194504ad1a`
- disassembly verification:
  - RX disassembly: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-skywalkRxInput-disasm-20260425.txt`
  - RX disassembly sha256: `f309449724cbf6e57199e6d56b55cd22423ef3bb4d0692aedff805f491a0d636`
  - `skywalkRxInput(...)` calls `skywalkRxReturnPreparedPacket(...)` on all six failure exits after `prepareWithQueue(...)`.
  - `skywalkRxReturnPreparedPacket(...)` calls packet slot `*0x198` with `edx=2` and `ecx=0`, then pool slot `*0x140`.
  - RX enqueue remains packet-array enqueue through slot `*0x2a0`.
- diff artifact:
  - final CR-102 diff artifact sha256 and line count are recorded in the CR-102 Stage 1 request to avoid self-referential diff hash churn.
- notes:
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-ASSOC-WCL-CANDIDATE-BSSID-035
- status: FIX_IMPLEMENTED
- symptom: after CR-107 the driver is visible and networks are visible, but `CONTROL_STA_NETWORK` join is slow/partial: the connected icon appears without internet access and then the station disconnects.
- first visible manifestation: CR-108 CONTROL_STA_NETWORK runtime on `2026-04-26 08:30:52..08:35:15 +0300` after the queue-enabled driver was loaded.
- expected system behavior: a hidden WCL join must bind the local association attempt to the same BSS candidate selected by WCL; the selected candidate BSSID is carried in the `apple80211AssocCandidates` candidate list.
- actual behavior: `setWCL_ASSOCIATE(...)` reads BSSID from `raw + 0x1F4`, which is zero in the Tahoe carrier. That clears `IEEE80211_F_DESBSSID` in `associateSSID(...)`, so local net80211 can select any same-SSID BSS before WCL/key management finishes.
- divergence point: `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)` uses the context/private field at `+0x1F4` instead of the WCL candidate list at `+0x218/+0x220`.
- evidence:
  - runtime logs: `commit-approval/runtime_evidence/CR-108-current-control_sta_network-runtime-20260426.txt`, sha256 `cce93c0c0d9f864a1aa9186fae384406478efe310f7c6f572aed1bbdcb738a59`, lines `230/254/304/338` show `WCLJoinRequest Beacon Info ... BSSID=50:4F:3B:CD:DD:67 ... channel=<100:3> ... akm=0x00048080 ssid='CONTROL_STA_NETWORK'`.
  - runtime logs: the same artifact lines `233/257/307/341` show local `setWCL_ASSOCIATE BSSID=00:00:00:00:00:00` for the same join request.
  - runtime logs: the same artifact lines `271..276` and `321..326` show local selection of different same-SSID BSSs (`50:88:11:da:43:9e` channel `153`, then `e4:3a:65:44:e4:dd` channel `161`) with `akms=2` before later attempts settle on WCL's `50:4f:3b:cd:dd:67` with `akms=10`.
  - runtime logs: the same artifact lines `3023/3084` and refreshed log `commit-approval/runtime_evidence/CR-108-current-live-last30m-20260426-refresh.log`, sha256 `9ac4fb922785f346931d01abbcf047c6a73be335bf35848a95025f3f391f9f7e`, lines `270019..270098` and `301440..301519` record `NetworkName = CONTROL_STA_NETWORK`, short `WiFiAssociatedDuration`, `WiFiDisconnectReasonINVALID_AKMS = 1`, and `WiFiNetworkJoinResult = 1`.
  - decomp: remote `IO80211Family_decompiled.c` `FUN_ffffff8002220b06(...)` initializes `*(undefined4 *)(param_2 + 0x218) = 0` for the candidate count.
  - decomp: remote `IO80211Family_decompiled.c` `FUN_ffffff8002220f84(...)` reads `uVar3 = *(uint *)(param_3 + 0x218)`, writes candidate BSSID via vtable slot `+0x170` to `param_3 + uVar3 * 0x12 + 0x220`, writes the paired MAC at `+0x226`, channel at `+0x22c`, and then increments the candidate count.
  - decomp: remote `AppleBCMWLAN_Core_decompiled.c` `AppleBCMWLANCore::setWCL_ASSOCIATE(...)` sets auth/SSID context and delegates the whole `apple80211AssocCandidates` carrier to `AppleBCMWLANJoinAdapter::performJoin(...)`; it does not extract the unrelated `+0x1F4` field as the selected BSSID.
- candidate causes:
  - confirmed: local compatibility code loses WCL's selected BSS by reading the wrong carrier field, allowing net80211 same-SSID BSS selection to diverge from the WCL candidate and AKM context.
  - insufficient data after this fix: if the exact selected-BSSID association still reaches no-internet/deauth, the next blocker moves to EAPOL/key install or post-key data path; that is outside this candidate.
- rejected causes:
  - `useAppleRSNSupplicant(IO80211VirtualInterface *)`: rejected for Tahoe V3 in `A-RSN-APPLE-SUPPLICANT-V2-OVERRIDE-034`; the V3 base has no such virtual slot.
  - forcing RSN done, faking `setCIPHER_KEY`, retrying joins, delaying joins, or re-emitting connect events: rejected as forced state/retry/replay without producer-side reference proof.
  - deriving BSSID from local scan cache by SSID: rejected as heuristic; the selected BSS is already present in the WCL carrier.
  - continuing to use `+0x1F4` for WCL candidate joins: rejected by runtime zero value and reference carrier layout.
- confirmed deviation: Tahoe WCL carrier stores candidate count at `+0x218` and first candidate BSSID at `+0x220`; local code ignores that list and treats `+0x1F4` as BSSID.
- root cause: hidden association is not constrained to WCL's selected BSS. WCL chooses `50:4F:3B:CD:DD:67`, but local association initially targets other `CONTROL_STA_NETWORK` BSSs with different AKM, producing UI delay and `INVALID_AKMS`.
- fix: implemented locally for CR-109. `setWCL_ASSOCIATE(...)` now reads candidate count from `+0x218` and uses first candidate BSSID from `+0x220` when the carrier contains candidates; the no-candidate carrier branch preserves the previous context-field behavior. Logs now report selected source, candidate count, candidate BSSID, and context BSSID.
- verification:
  - source check: `associateSSID(...)` receives candidate BSSID `50:4f:3b:cd:dd:67` for the observed carrier instead of zero.
  - build check: Tahoe build must succeed and BootKC unresolved-symbol validation must pass.
  - runtime check after Stage 1 approval: reboot with exact CR-109 diff, attempt one `CONTROL_STA_NETWORK` join, verify `setWCL_ASSOCIATE ... source=candidate candidates=1 candidate=50:4f:3b:cd:dd:67`, verify no local `ieee80211_node_join_bss` to `50:88:11:da:43:9e` or `e4:3a:65:44:e4:dd` during that WCL request, then classify any remaining failure as the next EAPOL/key/data blocker with fresh evidence.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-WCL-CANDIDATE-BSSID-035
- symptom: `CONTROL_STA_NETWORK` join shows transient connected/no-internet state, then disconnects; UI prompts and failure notification are delayed.
- expected system behavior: WCL-selected candidate BSSID from the `apple80211AssocCandidates` carrier must be the BSSID used by the local association path, so WCL's candidate, auth/AKM context, and local net80211 selected BSS are identical.
- actual behavior: WCL logs selected `50:4F:3B:CD:DD:67`, but local `setWCL_ASSOCIATE(...)` logs zero BSSID and local net80211 first joins other `CONTROL_STA_NETWORK` BSSs with different AKM.
- exact divergence point: `AirportItlwm/AirportItlwmSkywalkInterface.cpp::AirportItlwmSkywalkInterface::setWCL_ASSOCIATE`.
- evidence from runtime:
  - `CR-108-current-control_sta_network-runtime-20260426.txt` lines `230/254/304/338`: WCL selected BSSID `50:4F:3B:CD:DD:67`.
  - `CR-108-current-control_sta_network-runtime-20260426.txt` lines `233/257/307/341`: local BSSID field is `00:00:00:00:00:00`.
  - `CR-108-current-control_sta_network-runtime-20260426.txt` lines `271..276` and `321..326`: local association selects other same-SSID BSSs before the correct BSSID.
  - refreshed log `CR-108-current-live-last30m-20260426-refresh.log` lines `270019..270098` and `301440..301519`: `CONTROL_STA_NETWORK` failures end with `WiFiDisconnectReasonINVALID_AKMS = 1`.
- evidence from decomp:
  - `IO80211Family_decompiled.c::FUN_ffffff8002220b06(...)`: candidate count initialized at carrier offset `+0x218`.
  - `IO80211Family_decompiled.c::FUN_ffffff8002220f84(...)`: candidate BSSID written to `+0x220 + index * 0x12`, paired MAC to `+0x226`, channel to `+0x22c`, then candidate count is incremented.
  - `AppleBCMWLANCore::setWCL_ASSOCIATE(...)`: delegates the complete carrier to `AppleBCMWLANJoinAdapter::performJoin(...)`; it does not use `+0x1F4` as selected BSSID.
- exact semantic mismatch between reference and our code: reference preserves the selected candidate list in the join carrier and joins through it; local compatibility code discards that list and clears the desired-BSSID constraint by passing a zero BSSID to `associateSSID(...)`.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: hidden WCL associate carrier parsing; `setAUTH_TYPE(...)`; `setRSN_IE(...)`; `associateSSID(...)`; net80211 `ic_des_bssid` / `IEEE80211_F_DESBSSID`; WCL join-done/current-BSS/connect-complete; later RSN/EAPOL/key/data path.
  - expected contract at each touchpoint: carrier parsing must use the WCL candidate list; auth and RSN IE payloads remain unchanged; `associateSSID(...)` must constrain local BSS selection to the same WCL candidate; later WCL and RSN/data paths must observe a single coherent BSS/AKM context.
  - why no relevant touchpoints are missing: the observed `INVALID_AKMS` scope is caused before key install/DHCP/data by a mismatch between WCL candidate identity and local selected BSS; all fields that define that identity are the candidate BSSID plus the already preserved auth/RSN fields.
  - why proposed path adds no extra system-visible side effects: no event, callback, return status, retry, delay, forced state, key payload, RSN payload, packet ownership, queue lifecycle, or scan result is changed; the patch only uses the BSSID already written by the system's carrier builder.
- why this is root cause and not just correlation:
  - runtime shows the exact same WCL request selects BSSID `50:4F:3B:CD:DD:67` while local code passes zero BSSID.
  - runtime then shows local same-SSID BSS choices with different AKM before the correct BSS, followed by `INVALID_AKMS`.
  - decomp proves the selected BSSID source exists at `+0x220`; source audit proves local code reads `+0x1F4` instead.
- why proposed fix is 1:1 with reference architecture and semantics:
  - the local driver cannot call Apple's private `AppleBCMWLANJoinAdapter`, so it must translate the public system-facing carrier into local net80211 state.
  - using `+0x218/+0x220` mirrors the exact IO80211Family carrier layout and makes the local `ic_des_bssid` constraint represent the same candidate that the reference join adapter receives.
  - the no-candidate branch is a carrier-shape split, not a recovery fallback: when the system did not provide candidates, the previous context-field behavior is preserved rather than guessing a BSSID.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp::setWCL_ASSOCIATE`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - use local scan cache to find BSSID by SSID: rejected as heuristic and can pick the same wrong BSS.
  - continue with zero BSSID and wait for later retries: rejected as retry/timing-dependent behavior and already causes wrong-BSS selection.
  - force AKM/RSN values to match the selected AP: rejected because auth/RSN payloads are already present and should not be rewritten without reference proof.
  - force join success, RSN done, or connect-complete: rejected as state fabrication.
  - add sleeps/polls/retries around association: rejected as timing workaround.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect source/diff artifact for the exact `+0x218/+0x220` offset use and absence of forced state/retry/delay.
  - create CR-109 Stage 1 request with exact current diff and do not install or collect after-fix runtime until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: IO80211Family writes candidate count at `+0x218` and first candidate BSSID at `+0x220`.
- Есть ли прямое подтверждение по runtime-данным? Да: WCL selected `50:4F:3B:CD:DD:67`, local code passed zero BSSID, local net80211 selected other same-SSID BSSs, and the system recorded `INVALID_AKMS`.
- Доказал ли я причинность, а не просто корреляцию? Да: zero BSSID directly disables the local desired-BSSID constraint, and the log shows the resulting wrong-BSS selections before failure.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Да within `SYSTEM_CONTRACT_FIX`: local code now maps the same WCL candidate list consumed by the reference join adapter into the local `associateSSID(...)` BSSID constraint.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Нет.
- Не закрываю ли я симптом вместо причины? Нет: исправляется источник selected-BSSID identity mismatch before RSN/key/data.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: paths and line references are listed in the anomaly and fix candidate above.

## IMPLEMENTATION VERIFICATION

- id: CR-109-WCL-CANDIDATE-BSSID
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-ASSOC-WCL-CANDIDATE-BSSID-035`
- source verification:
  - `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)` reads `candidate_count` from `raw + 0x218`.
  - when `candidate_count > 0`, `associateSSID(...)` and diagnostic recording receive first candidate BSSID from `raw + 0x220`.
  - when `candidate_count == 0`, the previous context field at `raw + 0x1F4` is preserved for a no-candidate carrier shape.
  - the patch does not alter auth type, RSN IE, connect-complete, link-up, key install, queue lifecycle, packet ownership, or return status.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-109-build-20260426-wcl-candidate-bssid.txt`
  - build log sha256: `69c68a8a287d64900098b5a8f910d2cc7ad3d58c3c4f310784cb6eda8b20e165`
  - built binary UUID: `66030ADC-0CDF-359F-BCA8-2F8FBEFF51ED`
  - built binary sha256: `91f302f95560ea42626a574944b31112f6d989d93c5d745c4f9fcebaf69c4c18`
- disassembly verification:
  - focus artifact: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-109-setWCL_ASSOCIATE-disasm-focus-20260426.txt`
  - focus artifact sha256: `97c6981ca9d603b521a647d2a5ec1d27175238ece9385eaf2a1dc7f492a950c2`
  - disassembly lines `75..82` show load from `0x218`, context pointer `+0x1f4`, and candidate pointer `+0x220`.
  - disassembly lines `83..90` show `candidate_count > 0` selects the `+0x220` pointer; otherwise it keeps the `+0x1f4` pointer.
  - disassembly line `212` shows the runtime log string with selected BSSID, source, candidate count, candidate BSSID, and context BSSID.
- diff artifact:
  - `/Users/bob/Projects/itlwm/commit-approval/artifacts/CR-109-wcl-candidate-bssid.diff`
  - final diff sha256 and line count are recorded in the CR-109 Stage 1 request after this implementation-verification section is included.
- notes:
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-DATA-SKYWALK-RX-QUEUE-ENABLE-033
- status: FIX_IMPLEMENTED
- symptom: after CR-105 the driver is visible in UI and association reaches the post-connect RX window, but `skywalkRxInput(...)` drops every RX packet with `IOSkywalkRxCompletionQueue::enqueuePackets(...) == 0xe00002d7`; RSN remains incomplete and the AP disconnects the station.
- first visible manifestation: CR-106 CONTROL_STA_NETWORK runtime logs show `skywalkRxInput: enqueuePackets failed 0xe00002d7` on the first five post-association RX packets while `IO80211RSNDone = No`, `current_ssid = CONTROL_STA_NETWORK`, and `IOLinkStatus = 1`.
- expected system behavior: Skywalk packet queues used by the datapath must be workloop-attached, queue-ready, and enabled before RX/TX queue operations are used after association.
- actual behavior: CR-105 attaches `fTxQueue` and `fRxQueue` to `_fWorkloop`, but the local Tahoe path never calls `fTxQueue->enable()` or `fRxQueue->enable()`.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::enableAdapter(...)` powers the HAL and watchdog without enabling the registered Skywalk queues; `disableAdapterCore(...)` likewise lacks symmetric queue disable.
- evidence:
  - runtime evidence: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-107-queue-enable-root-cause-evidence-20260426.txt`, sha256 `4c24608540900f06db1fd7c61ad0c71eea074ac9577cf80dfd408c1a3a43a9f5`, records CR-106 `0xe00002d7` drops and local absence of queue enable calls.
  - BootKC symbols: Tahoe exports `IOSkywalkRxCompletionQueue::enable/disable`, `IOSkywalkTxSubmissionQueue::enable/disable`, and const `IOSkywalkPacketQueue::isQueueReady`.
  - IOSkywalk decomp: `IOSkywalkPacketQueue::initWithPool(...)` initializes the inherited enabled byte at `this+0x28` to `0`; `IOSkywalkPacketQueue::isQueueReady()` returns `this+0xcc & 1`; `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)` returns default `0xe00002d7` unless both `this+0x28 & 1` and `this+0xcc & 1` are set.
  - reference decomp: `AppleBCMWLANSkywalkInterface::enableDatapath()` calls queue virtual slot `0x150` on the TX/RX datapath queues before RX `requestEnqueue`; `disableDatapath()` calls the symmetric virtual slot `0x158`.
  - local binary evidence before fix: the built CR-105/CR-106 binary imports queue `getWorkLoop` and enqueue symbols but does not import `IOSkywalkRxCompletionQueue::enable` or `IOSkywalkTxSubmissionQueue::enable`.
- candidate causes:
  - confirmed: local queues remain disabled after creation/workloop attachment, so the first RX enqueue reaches the Tahoe enabled/ready gate and fails with `0xe00002d7`.
  - watch after fix: if the new runtime still reports `0xe00002d7` with `RXenabled=1` and a non-null `RXwl`, then the remaining blocker is `mQueueReady`/registration readiness by elimination from the decompiled gate.
- rejected causes:
  - missing workloop: rejected for CR-106 because CR-105 eliminated `0xe00002d8`; `0xe00002d7` is the next gate after the workloop-null check.
  - retrying `enqueuePackets(...)`: rejected as fallback/duplicate-delivery risk and cannot set the queue enabled bit.
  - forcing RSN/connect state: rejected because RX packets are dropped before supplicant traffic can complete.
  - calling contested Wi-Fi getters from user diagnostics: rejected as unrelated and previously shown to risk side effects.
- confirmed deviation: the local Tahoe datapath omits the explicit queue enable lifecycle that Tahoe exposes on the same Skywalk queue classes and that AppleBCMWLAN invokes from datapath enable.
- root cause: after CR-105, `fRxQueue` has a workloop but remains disabled; Tahoe `enqueuePackets(...)` maps that state to `0xe00002d7`, dropping EAPOL/RSN RX packets and preventing association completion.
- fix: implemented locally for CR-107. `enableAdapter(...)` enables `fTxQueue` and `fRxQueue` before HAL enable; `disableAdapterCore(...)` disables them symmetrically before HAL disable; enqueue-fail logs now include read-only `RXenabled` and `RXwl` to distinguish enabled-bit failure from queue-ready failure in one reboot by the decompiled gate.
- verification:
  - source check: `AirportItlwmV2.cpp` imports and calls `IOSkywalkTxSubmissionQueue::enable/disable` and `IOSkywalkRxCompletionQueue::enable/disable`.
  - build check: Tahoe build must succeed and BootKC unresolved-symbol validation must resolve the new queue lifecycle imports.
  - runtime check after Stage 1 approval: reboot, attempt one join to `CONTROL_STA_NETWORK`, verify no post-association `enqueuePackets failed 0xe00002d7` with `RXenabled=0`; if `0xe00002d7` remains with `RXenabled=1` and non-null `RXwl`, classify the next blocker as queue-ready/registration readiness.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-QUEUE-ENABLE-033
- symptom: UI sees networks and association starts, but post-connect RX packets are dropped with `0xe00002d7`; RSN stays incomplete and the station disconnects.
- expected system behavior: queue lifecycle must be `withPool` -> workloop attachment -> register logical link -> datapath enable -> queue operations; queue operations require the inherited enabled bit and queue-ready bit.
- actual behavior: lifecycle stops at workloop attachment/registration; the local Tahoe path never enables `fTxQueue`/`fRxQueue`.
- exact divergence point: `AirportItlwm::enableAdapter(...)` and `AirportItlwm::disableAdapterCore(...)` in `AirportItlwmV2.cpp`.
- evidence from runtime:
  - CR-106 logs show first RX drops as `skywalkRxInput: enqueuePackets failed 0xe00002d7`.
  - CR-106 ioreg/regdiag show the driver and UI path are alive, SSID is selected, but `IO80211RSNDone = No`.
- evidence from decomp:
  - Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(...)` returns `0xe00002d7` unless both `this+0x28` enabled and `this+0xcc` queue-ready are true.
  - Tahoe `IOSkywalkPacketQueue::initWithPool(...)` initializes enabled to `0`.
  - AppleBCMWLAN `enableDatapath()` explicitly enables the datapath queues; `disableDatapath()` disables them.
- exact semantic mismatch between reference and our code: Apple/Tahoe datapath enables queue event sources before datapath queue use; local code only attaches them to a workloop and then uses them disabled.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation:
  - CR-105 changed the return from workloop-null `0xe00002d8` to the next exact decompiled gate `0xe00002d7`.
  - source audit proves no queue enable call exists in the local lifecycle.
  - decomp proves disabled state is sufficient to produce the exact runtime return before any later RSN/DHCP/data stage.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it uses the queue classes' own Tahoe `enable/disable` methods at the datapath enable/disable lifecycle edge.
  - it does not retry, replay, reorder association events, force link/RSN state, or alter packet ownership.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::enableAdapter`
  - `AirportItlwm/AirportItlwmV2.cpp::disableAdapterCore`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - enabling queues in `start(...)`: rejected because Apple enables queues at datapath enable, not at construction/registration.
  - retry/drop suppression around `enqueuePackets(...)`: rejected as fallback and not a lifecycle fix.
  - forcing `IO80211RSNDone` or link active: rejected because the actual blocker is packet delivery into Skywalk before RSN completion.
  - adding sleeps/polls/wait loops: rejected as unproven synchronization.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - verify new imports/disassembly for queue `enable/disable`
  - create CR-107 Stage 1 request; do not install or commit until reviewer allows after-fix runtime

## IMPLEMENTATION VERIFICATION

- id: CR-107-QUEUE-ENABLE
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-DATA-SKYWALK-RX-QUEUE-ENABLE-033`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::enableAdapter(...)` now calls `fTxQueue->enable()` and `fRxQueue->enable()` before `fHalService->enable(...)`.
  - `AirportItlwm/AirportItlwmV2.cpp::disableAdapterCore(...)` now calls `fTxQueue->disable()` and `fRxQueue->disable()` before `fHalService->disable(...)`.
  - `skywalkRxInput(...)` failure logging includes `RXenabled` and `RXwl`; no direct `isQueueReady()` call is emitted because Tahoe exports only the const symbol and the local SDK declaration is stale.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-107-build-20260426-queue-enable.txt`
  - build log sha256: `05cbcb577b6cef0e4e3fae4b4d7ceecc2eca56b25df0a0ca20233e1423a62585`
  - built binary UUID: `59229FCD-B203-3DDC-8518-10F617BCCF66`
  - built binary sha256: `741510e52c02c84b1552d5c10b8376467c5c306cb35075c0541c2350e584a2ca`
- disassembly verification:
  - focus artifact: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-107-queue-enable-disasm-imports-20260426.txt`
  - focus artifact sha256: `c3c1fd49a22e16254b8c00694dcb9ec2df8807b3afa49203848cf0beabfa091e`
  - `enableAdapter(...)` shows queue virtual slot `0x150` calls before HAL enable, matching queue `enable()`.
  - `disableAdapterCore(...)` shows queue virtual slot `0x158` calls before HAL disable, matching queue `disable()`.
  - both functions use slot `0x160` only for inherited `isEnabled()` verification logging.
- diff artifact:
  - `/Users/bob/Projects/itlwm/commit-approval/artifacts/CR-107-queue-enable.diff`
  - final diff sha256 and line count are recorded in the CR-107 Stage 1 request.
- notes:
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-RSN-APPLE-SUPPLICANT-V2-OVERRIDE-034
- status: REJECTED
- symptom: after CR-107 the driver remains visible and WCL accepts the associated BSS, but the join does not complete RSN; the UI shows a transient connected icon/no internet and then the AP deauths the station.
- first visible manifestation: CR-108 CONTROL_STA_NETWORK runtime, `2026-04-26 08:31:25..08:31:29 +0300`, after the CR-107 queue-enabled driver was loaded.
- expected system behavior: Tahoe V2 controller must advertise the Apple RSN supplicant contract through the V2 `IO80211Controller::useAppleRSNSupplicant(IO80211VirtualInterface *)` virtual seam; then CoreWiFi/WCL can run Apple-managed RSN/EAPOL, deliver EAPOL through the Apple path, and call the existing `setCIPHER_KEY(...)` producer for PTK/GTK.
- actual behavior: CR-037 enabled `USE_APPLE_SUPPLICANT` for the Tahoe target, but the only local `useAppleRSNSupplicant(...)` implementation remains in legacy `AirportItlwm.cpp`. Tahoe builds `AirportItlwmV2.cpp` instead, and the built Tahoe binary has no `AirportItlwm::useAppleRSNSupplicant` symbol.
- divergence point: `AirportItlwm/AirportItlwmV2.hpp` lacks the V2 `useAppleRSNSupplicant(IO80211VirtualInterface *)` override declared by `include/Airport/IO80211ControllerV2.h`; `AirportItlwm/AirportItlwmV2.cpp` therefore never returns the `USE_APPLE_SUPPLICANT` state on that system-facing virtual.
- evidence:
  - runtime logs: `commit-approval/runtime_evidence/CR-108-current-control_sta_network-runtime-20260426.txt` shows `ASSOC -> RUN`, `postTahoeWclLinkUpInd msg=0xd8`, `WCL Joined Bss ... BSSID=50:4F:3B:CD:DD:67`, `sendWCLJoinDone ... lastStatusCode=0`, and `postTahoeWclConnectCompleteEvent ... status=0`, followed by repeated `CWEAPOLClient ... failed to retrieve 8021X state (2)` and later `ieee80211_recv_deauth` / `RUN -> AUTH`.
  - ioreg: `commit-approval/runtime_evidence/CR-108-current-ioreg-io80211-20260426.txt` / focused IOService dump show the driver is loaded on `en0` and `IO80211RSNDone = No` after the failed attempt.
  - negative runtime markers: the CR-108 focused artifacts contain no local `setCIPHER_KEY`, no `output EAPOL packet`, and no `input EAPOL packet` marker before deauth, so the first missing producer is before key install/DHCP/data.
  - source: `include/Airport/IO80211ControllerV2.h` declares `virtual bool useAppleRSNSupplicant(IO80211VirtualInterface *);`; `AirportItlwm/AirportItlwmV2.hpp` has no override; legacy `AirportItlwm.cpp` implements only `useAppleRSNSupplicant(IO80211Interface *)`.
  - build/source list: `itlwm.xcodeproj/project.pbxproj` uses `AirportItlwmV2.cpp in Sources` for the Tahoe target and the Tahoe build settings contain `USE_APPLE_SUPPLICANT`.
  - binary: `nm -a -C Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm` shows `vtable for AirportItlwm` but no `AirportItlwm::useAppleRSNSupplicant`.
  - reference: BootKC exports `AppleBCMWLANCore::useAppleRSNSupplicant()` and `IO80211InfraInterface::handleKeyDone(bool,bool)`, proving that Apple’s reference driver has an explicit RSN-supplicant producer seam and later key-done consumer path on Tahoe.
  - consolidated evidence artifact: `commit-approval/runtime_evidence/CR-108-apple-rsn-v2-override-root-cause-evidence-20260426.txt`, sha256 `dd6a20b357f0e73933ebf64ea360e7e9a4a49d8da414d5e3ef0e79cf7082164d`.
- candidate causes:
  - rejected: Tahoe V2 never overrides the Apple-supplicant virtual seam, so the system observes an Apple-facing join surface but cannot start/retrieve the expected 802.1X supplicant state for this controller.
  - moved to confirmed anomaly: `setWCL_ASSOCIATE(...)` currently reads BSSID from offset `0x1F4`, which is zero while `WCLJoinRequest` selected `50:4F:3B:CD:DD:67`; the exact carrier source is now proven as candidate count `+0x218` and first candidate BSSID `+0x220` in `A-ASSOC-WCL-CANDIDATE-BSSID-035`.
- rejected causes:
  - CR-107 queue-enabled bit as current first blocker: rejected for CR-108 scope; after CR-107 there is no observed `skywalkRxInput: enqueuePackets failed 0xe00002d7`, and WCL reaches successful current-BSS/connect-complete.
  - WCL current-BSS/connect-complete producers: rejected as current first blocker; CR-108 logs show WCL joined BSS and `sendWCLJoinDone lastStatusCode=0 extendedCode=0`.
  - forcing `IO80211RSNDone`, fabricating `setCIPHER_KEY`, or posting synthetic key completion: rejected as fake success and masking.
  - changing BSSID carrier offset now: rejected until exact reference/carrier proof identifies the correct source.
- confirmed deviation: rejected as a patchable Tahoe V3 root cause. Tahoe target builds with `IO80211FAMILY_V3`, and `include/Airport/IO80211ControllerV3.h` has no `useAppleRSNSupplicant(...)` virtual slot. The earlier V2-header seam does not apply to the loaded Tahoe class layout.
- root cause: rejected. The build failure proves adding this override would not align to a real Tahoe V3 system-facing virtual and would create a non-building diff, so the current RSN/EAPOL blocker must be below a different seam.
- fix: none. The attempted V2/V3 override candidate was removed immediately after build rejection.
- verification:
  - `git diff --check`
  - Tahoe build through `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - binary audit must show `AirportItlwm::useAppleRSNSupplicant(IO80211VirtualInterface*)`
  - after Stage 1 approval, install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, and verify whether the failure moves from `CWEAPOLClient failed to retrieve 8021X state` / no `setCIPHER_KEY` to EAPOL/key progress or a later concrete datapath blocker.

## FIX_CANDIDATE

- anomaly_id: A-RSN-APPLE-SUPPLICANT-V2-OVERRIDE-034
- candidate_status: REJECTED_AFTER_BUILD_HEADER_CHECK
- symptom: WCL current-BSS/connect-complete succeeds, but RSN/EAPOL never starts visibly; no `setCIPHER_KEY`, `IO80211RSNDone = No`, then AP deauth / link down.
- expected system behavior: Tahoe V2 controller exposes the Apple RSN supplicant contract through `useAppleRSNSupplicant(IO80211VirtualInterface *)`, coherent with the already enabled Tahoe `USE_APPLE_SUPPLICANT` build macro and the existing `getRSN_IE` / `setRSN_IE` / EAPOL handoff / `setCIPHER_KEY` touchpoints.
- actual behavior: Tahoe target compiles `AirportItlwmV2.cpp`, but the only local supplicant override is the legacy `IO80211Interface *` implementation in `AirportItlwm.cpp`; current Tahoe binary has no local override symbol.
- exact divergence point: `AirportItlwm/AirportItlwmV2.hpp` and `AirportItlwm/AirportItlwmV2.cpp` omit the V2 `IO80211VirtualInterface *` override.
- evidence from runtime:
  - CR-108 logs show WCL successful join/current-BSS/connect-complete, repeated `CWEAPOLClient ... failed to retrieve 8021X state (2)`, no `setCIPHER_KEY` / EAPOL markers, `IO80211RSNDone = No`, then AP deauth.
- evidence from decomp:
  - BootKC symbol evidence exposes `AppleBCMWLANCore::useAppleRSNSupplicant()` and `IO80211InfraInterface::handleKeyDone(bool,bool)` on the Tahoe reference path.
  - The V2 SDK header exposes the exact system-facing virtual seam as `IO80211Controller::useAppleRSNSupplicant(IO80211VirtualInterface *)`.
- exact semantic mismatch between reference and our code:
  - reference has an explicit Apple RSN supplicant producer seam; local Tahoe V2 leaves the producer inherited/default while compiling the rest of the Apple-supplicant RSN/EAPOL branches.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: V2 `useAppleRSNSupplicant(IO80211VirtualInterface *)`; Tahoe `USE_APPLE_SUPPLICANT` build setting; `getRSN_IE(...)`; `setRSN_IE(...)`; net80211 EAPOL handoff; `setCIPHER_KEY(...)`; `IO80211RSNDone` / `handleKeyDone` consumer state.
  - expected contract at each touchpoint: controller reports Apple supplicant enabled; Apple-managed RSN IE override is available; inbound EAPOL is handed to Apple userspace; PTK/GTK are installed only through `setCIPHER_KEY`; RSN done remains driven by real key install.
  - why no relevant touchpoints are missing: CR-108 has already passed scan visibility, hidden associate, WCL current-BSS, connect-complete, link-up, and queue enabled-bit; the first missing runtime producer is 802.1X/RSN start before any key/DHCP/data path.
  - why proposed path adds no extra system-visible side effects: it only returns the compile-time Apple-supplicant contract through the documented virtual; it does not emit events, modify payloads, force state, retry, delay, fabricate keys, or bypass EAPOL.
- why this is root cause and not just correlation:
  - the runtime failure is exactly at supplicant-state retrieval before EAPOL/key events; the target contains the macro and all downstream branches, but the loaded V2 class has no producer override for the system query that enables that contract.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it restores the missing controller-level supplicant producer seam instead of changing downstream EAPOL/key behavior; the return semantics match the existing legacy AirportItlwm implementation already used by non-Tahoe Airport targets.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp::AirportItlwm`
  - `AirportItlwm/AirportItlwmV2.cpp::AirportItlwm::useAppleRSNSupplicant`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - fake `APPLE80211_M_RSN_HANDSHAKE_DONE`: rejected as forced success.
  - direct key fabrication or PMK import from WCL carrier: rejected; reference keeps WCL PMK ownership outside assoc candidates and key install must come through `setCIPHER_KEY`.
  - delaying deauth / extending join timers: rejected as timing workaround.
  - changing `setWCL_ASSOCIATE` BSSID offset now: rejected until exact carrier source is proven.
- verification result:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build rejected the candidate because `useAppleRSNSupplicant` marked `override` does not override any Tahoe V3 base method.
  - `include/Airport/IO80211ControllerV3.h` confirms Tahoe V3 has no such virtual slot; `include/Airport/IO80211ControllerV2.h` has the older V2 slot, but Tahoe does not compile against it.
  - candidate removed; no Stage 1 request will be filed for this rejected path.

## POST-CR-104 WATCHLIST

- scope: possible next failures around `_fWorkloop` and Skywalk queue lifecycle after the CR-104 queue workloop attach fix.
- status: WATCHLIST_ONLY, not a `FIX_CANDIDATE`; none of these may be patched without new runtime/decomp evidence that upgrades a concrete item to `CONFIRMED_DEVIATION` or `CONFIRMED_ROOT_CAUSE`.
- W-RX-QUEUE-READY-001:
  - what to watch: if CR-104 runtime no longer shows `skywalkRxInput: enqueuePackets failed 0xe00002d8`, but starts returning `0xe00002d7`, inspect RX queue `enabled` and `mQueueReady` state.
  - evidence baseline: Tahoe `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)` first rejects null `IOEventSource::workLoop` as `0xe00002d8`; after passing that check, it returns `0xe00002d7` when the inherited enabled bit or `IOSkywalkPacketQueue::isQueueReady()` is false.
  - expected runtime check: one post-CR-104 join must record whether RX enqueue reaches success, `0xe00002d7`, `0xe00002d5`, or a later EAPOL/key/DHCP/data stage.
- W-SCAN-TIMER-NULL-002:
  - what to watch: `scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone)` is used immediately with `_fWorkloop->addEventSource(scanSource)` and `scanSource->enable()` without a local null check.
  - current risk class: boot/start robustness risk only; no current runtime evidence ties it to the join failure because the driver starts, scans, and publishes networks.
  - required evidence before fix: boot/start failure or panic/hang with `scanSource == NULL` or failed `addEventSource(scanSource)`.
- W-WRAPPER-WORKLOOP-OVERWRITE-003:
  - what to watch: `IOPCIEDeviceWrapper::start()` assigns the global `_fWorkloop = IO80211WorkQueue::workQueue()`, and `AirportItlwm::createWorkQueue()` later assigns the same global again.
  - current risk class: leak/stale-workloop risk; not current join root cause because the active controller reaches `_fCommandGate`, scanSource, HAL init, queue creation, and post-association RX.
  - required evidence before fix: logs proving wrapper-created workloop remains externally visible, receives event sources after controller overwrite, or leaks/teardown hangs on reboot/stop.
- W-SKYWALK-TEARDOWN-ORDER-004:
  - what to watch: `AirportItlwm::stop(...)` currently removes/releases `fTxQueue`, `fRxQueue`, `fTxPool`, and `fRxPool` before `detachInterface(fNetIf, true)`.
  - current risk class: stop/reboot teardown risk; not current association root cause.
  - required evidence before fix: stop/reboot panic/hang, Skywalk logical-link teardown touching released queues/pools, or decomp/reference proof that `detachInterface`/logical-link deregistration must precede queue/pool release in this local lifecycle.
- W-QUEUE-ENABLE-PRODUCER-005:
  - what to watch: CR-104 does not explicitly call `fTxQueue->enable()` or `fRxQueue->enable()`. If runtime returns `0xe00002d7`, determine whether `registerEthernetInterface`, BSDClient enable, or interface enable is expected to enable/initialize these queue event sources.
  - current risk class: unresolved contract question, not patchable yet. An explicit local `enable()` would be a forced state change unless runtime/decomp proves that the expected producer is missing in our lifecycle.
  - required evidence before fix: decomp of the reference lifecycle or runtime trace proving the queue remains disabled/not-ready because a specific producer-side enable/initialize call is absent locally.

## ANOMALY

- id: A-DATA-SKYWALK-RX-QUEUE-WORKLOOP-032
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-103 runtime no longer panics, networks remain visible, but joining `CONTROL_STA_NETWORK` only reaches transient link-up and then disconnects without internet.
- first visible manifestation: after `postTahoeWclConnectCompleteEvent msg=0xd5 status=0`, the first post-association RX frames are dropped by `skywalkRxInput(...)` because `fRxQueue->enqueuePackets(...)` returns `0xe00002d8`.
- expected system behavior: `IOSkywalkRxCompletionQueue` is an `IOEventSource`; before the driver can enqueue RX completion packets, the queue must be attached to an `IOWorkLoop` so its gate/workloop field is non-null.
- actual behavior: local code creates `fTxQueue` and `fRxQueue`, passes them to `registerEthernetInterface(...)`, but never calls `_fWorkloop->addEventSource(...)` for either Skywalk packet queue. Runtime enqueue reaches the RX queue with packet array/count valid, then fails before enabled/ready checks because the queue's inherited `IOEventSource::workLoop` field is null.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::start(...)` after `IOSkywalkTxSubmissionQueue::withPool(...)` and `IOSkywalkRxCompletionQueue::withPool(...)`; queue event sources are not added to `_fWorkloop` before `registerEthernetInterface(...)` and later RX enqueue.
- evidence:
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-runtime-control_sta_network-partial-20260425-2240-kernel-driver.txt`, sha256 `bff7b79cd5969bcf1f442874a6ed2b5b8d802a7d2d6d714bb86e12b210adcff1`, shows:
    - `22:14:46.279888` `ieee80211_node_join_bss selbs=CONTROL_STA_NETWORK mac=50:4f:3b:cd:dd:67 chan=100`
    - `22:14:46.327244` `skywalkRxInput: enqueuePackets failed 0xe00002d8 (drop #1)`
    - `22:14:46.328372` `postTahoeWclLinkUpInd`
    - `22:14:46.329221` `postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4 status=0 reason=0`
    - `22:14:47..22:14:49` more `0xe00002d8` RX enqueue drops
    - `22:14:50.328515` `Deauth received, reason 15`
  - airportd/configd logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-runtime-control_sta_network-partial-20260425-2240-wifi.txt`, sha256 `8f44da1bee613eff258d5afd0a588dceb83f375fd10cb97707d735bf576cbe45`, shows `FaultReasonDhcpFailure = 0`, `JoinIpConfigurationLatencyFromDriverAvailability = 0`, `WiFiDisconnectReasonINVALID_AKMS = 1`, `WiFiNetworkDisconnectReason = Internal`, and `WiFiAssociatedDuration = 5`.
  - ioreg: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-runtime-control_sta_network-partial-20260425-2240-ioreg.txt`, sha256 `dec768d4521712e6b8cdf6d39ca498ad411b75c9c029667d2bd1fa7edec000ff`, shows driver active, `IOInterfaceName = en0`, `IO80211RSNDone = No`, `IOLinkSpeed = 0`, `IOLinkActiveCount = 0`.
  - decomp: `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, UInt32, IOOptionBits)` at `0xffffff8002a59cda` returns `0xe00002d8` only after valid array/count/capacity checks when `*(this + 0x30) == 0`.
  - header/system contract: `IOEventSource` declares `workLoop` immediately after `enabled`; with the kernel object layout this is `this + 0x30`. `IOWorkLoop::addEventSource(IOEventSource*)` is the documented API for adding an event source to a workloop and retaining it there.
  - source: `AirportItlwmV2.cpp::start(...)` adds `_fCommandGate` and `scanSource` to `_fWorkloop`, but does not add `fTxQueue` or `fRxQueue`. `AirportItlwmV2.cpp::stop(...)` releases the queues without removing them from a workloop because they were never attached.
- candidate causes:
  - confirmed: `fRxQueue` lacks an `IOEventSource::workLoop` at RX enqueue time; decomp maps the exact runtime return `0xe00002d8` to this null field.
  - confirmed: the source code never attaches either Skywalk packet queue to `_fWorkloop`.
  - insufficient data: after this workloop contract is fixed, association may still fail later in EAPOL key install, DHCP, or data; those are beyond this anomaly's claim scope.
- rejected causes:
  - CR-103 source-queue panic: rejected for this runtime because no panic occurred and logs reached `enqueuePackets failed 0xe00002d8`.
  - DHCP/data transfer as first blocker: rejected for this runtime because `IO80211RSNDone = No`, no `setCIPHER_KEY` is observed, and `FaultReasonDhcpFailure = 0`.
  - bad packet-array overload: rejected for this runtime because the return comes from the packet-array enqueue implementation after valid array/count/capacity checks.
  - diagnostic side effect: rejected for this edge because the failing code is unconditional RX queue enqueue and the runtime failure code maps to `IOEventSource::workLoop == null`, not to regdiag gating.
- confirmed deviation: the local driver creates Skywalk packet queues, but omits the required `IOWorkLoop::addEventSource(...)` lifecycle step for those `IOEventSource` objects.
- root cause: `IOSkywalkRxCompletionQueue` is used for RX delivery while not attached to a workloop, so `enqueuePackets(...)` rejects every post-association RX packet with `0xe00002d8`; this prevents EAPOL/RSN traffic from reaching the Apple supplicant, leaving `IO80211RSNDone = No` and causing AP deauth reason 15.
- fix: attach `fTxQueue` and `fRxQueue` to `_fWorkloop` immediately after creation and before `registerEthernetInterface(...)`; on stop, remove attached queue event sources from `_fWorkloop` before releasing the queues.
- verification:
  - source check: both queues are added with `_fWorkloop->addEventSource(...)` and removed before `OSSafeReleaseNULL(...)`.
  - build check: Tahoe build succeeds and BootKC unresolved-symbol validation passes.
  - runtime check after Stage 1 approval: reboot, attempt one join to `CONTROL_STA_NETWORK`, verify no `skywalkRxInput: enqueuePackets failed 0xe00002d8` during the first post-association RX window, and then inspect whether `setCIPHER_KEY`, `IO80211RSNDone`, DHCP/IP, and data progress.
- notes:
  - This is a later blocker after CR-103. It does not alter WCL association payloads, link-up reporting, key material, RSN events, scan cache, or diagnostic defaults.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-QUEUE-WORKLOOP-032
- symptom: join reaches link-up/connect-complete, then RX packets drop with `0xe00002d8`, no `setCIPHER_KEY`, `IO80211RSNDone = No`, AP deauth reason 15.
- expected system behavior: every `IOSkywalkPacketQueue` used as a runtime queue must be an attached `IOEventSource` with a non-null `IOEventSource::workLoop` before queue operations such as `enqueuePackets(...)`.
- actual behavior: `fTxQueue` and `fRxQueue` are created and registered but are never added to `_fWorkloop`; `fRxQueue->enqueuePackets(...)` returns `0xe00002d8`.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::start(...)` between queue creation and `registerEthernetInterface(...)`.
- evidence from runtime:
  - CR-104 kernel evidence sha256 `bff7b79cd5969bcf1f442874a6ed2b5b8d802a7d2d6d714bb86e12b210adcff1` shows post-association `skywalkRxInput: enqueuePackets failed 0xe00002d8`.
  - CR-104 airportd/configd evidence sha256 `8f44da1bee613eff258d5afd0a588dceb83f375fd10cb97707d735bf576cbe45` shows no DHCP failure, `WiFiDisconnectReasonINVALID_AKMS = 1`, and no IP configuration latency.
  - CR-104 ioreg evidence sha256 `dec768d4521712e6b8cdf6d39ca498ad411b75c9c029667d2bd1fa7edec000ff` shows `IO80211RSNDone = No`.
- evidence from decomp:
  - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, UInt32, IOOptionBits)` returns `0xe00002d8` exactly when `*(this + 0x30) == 0`.
  - BootKC symbols map the inherited gate methods to `IOEventSource::closeGate/openGate/signalWorkAvailable`; the field at `+0x30` is the `IOEventSource::workLoop` pointer by kernel header layout.
  - `IOWorkLoop::addEventSource(IOEventSource*)` is the documented operation that makes an event source part of a workloop and retains it until `removeEventSource(...)`.
- exact semantic mismatch between reference and our code: our Skywalk packet queues are used as active runtime `IOEventSource` queues without the required workloop membership; the system queue implementation refuses RX enqueue before any packet-content or ready-state handling can occur.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkTxSubmissionQueue::withPool(...)`
    - `IOSkywalkRxCompletionQueue::withPool(...)`
    - `IOWorkLoop::addEventSource(IOEventSource*)`
    - `IOSkywalkEthernetInterface::registerEthernetInterface(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(...)`
    - stop teardown `IOWorkLoop::removeEventSource(IOEventSource*)`
  - expected contract at each touchpoint:
    - queue factories return `IOEventSource` subclasses with owner/action initialized but no workloop membership.
    - `addEventSource(...)` binds each queue to `_fWorkloop` before registration/enable/runtime queue operations.
    - `registerEthernetInterface(...)` receives queues that already satisfy `IOEventSource` workloop membership.
    - RX enqueue sees non-null `IOEventSource::workLoop` and can proceed to enabled/ready checks and packet handoff.
    - stop removes queue event sources from `_fWorkloop` before releasing queue objects.
  - why no relevant touchpoints are missing:
    - the observed failure is entirely at the queue's inherited `IOEventSource::workLoop` precondition in `enqueuePackets(...)`; creation, workloop attachment, registration, enqueue, and teardown are the complete lifecycle edges for that precondition.
  - why proposed path adds no extra system-visible side effects:
    - no retry, delay, poll, duplicate enqueue, forced callback, forced state, fake key, fake success, packet payload change, WCL replay, or diagnostic intervention is added.
    - attaching queue event sources is the normal lifecycle operation for the existing objects; it does not alter association payloads, scan results, link events, or RSN state.
- why this is root cause and not just correlation:
  - runtime return `0xe00002d8` is produced by one exact branch in the decompiled packet-array enqueue implementation.
  - that branch tests only `this+0x30 == 0` after packet array/count/capacity have already passed.
  - kernel header layout identifies `this+0x30` as `IOEventSource::workLoop`.
  - source audit proves no `addEventSource(...)` call exists for `fRxQueue`.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it satisfies the base `IOEventSource`/`IOWorkLoop` contract that Skywalk queues inherit, using the same kernel API already used by the driver for `_fCommandGate` and `scanSource`.
  - it preserves the existing one-TX/one-RX queue registration architecture and only completes the missing lifecycle binding.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::start`
  - `AirportItlwm/AirportItlwmV2.cpp::stop`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - retry `enqueuePackets(...)`: rejected as fallback and duplicate-delivery risk; it cannot make `workLoop` non-null.
  - force success after enqueue failure: rejected as masking and packet loss.
  - bypass Skywalk RX queue and call legacy input path: rejected as architecture change and not reference-aligned for Tahoe.
  - synthesize `setCIPHER_KEY`/RSN done: rejected as fake state and not producer-side proof.
  - add delays around link-up/connect-complete: rejected because the failure is a static queue lifecycle precondition, not timing.
  - attach only `fRxQueue`: rejected because `fTxQueue` is the matching TX `IOEventSource` and later data transmission uses the same workloop contract class family.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - create CR-104 Stage 1 request with diff artifact and CR-104 runtime evidence; do not install or commit until reviewer allows after-fix runtime

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: `IOSkywalkRxCompletionQueue::enqueuePackets(...)` maps `0xe00002d8` to `this+0x30 == 0`.
- Есть ли прямое подтверждение по runtime-данным? Да: CR-104 logs show the exact `0xe00002d8` return immediately after association and before deauth reason 15.
- Доказал ли я причинность, а не просто корреляцию? Да: the exact runtime return is produced only by the null-workloop precondition branch; source audit proves the missing `addEventSource(...)`.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Это SYSTEM_CONTRACT_FIX: it uses the documented `IOWorkLoop::addEventSource` lifecycle required by the base `IOEventSource` class inherited by the Skywalk queues.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Нет.
- Не закрываю ли я симптом вместо причины? Нет: the missing queue workloop membership that directly produces the runtime error is added.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: all evidence paths and sha256 values are listed above.

## IMPLEMENTATION VERIFICATION

- id: CR-104-QUEUE-WORKLOOP
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-DATA-SKYWALK-RX-QUEUE-WORKLOOP-032`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::start(...)` now calls `_fWorkloop->addEventSource(fTxQueue)` and `_fWorkloop->addEventSource(fRxQueue)` immediately after queue creation and before `registerEthernetInterface(...)`.
  - the start path logs both return codes and both queue `getWorkLoop()` values as `[STEP 8c-wl]`.
  - if either queue workloop attach fails, the already attached queue is removed and start aborts; no fallback, retry, or forced success is introduced.
  - `AirportItlwm/AirportItlwmV2.cpp::stop(...)` checks each queue's `getWorkLoop()` and removes it from `_fWorkloop` before `OSSafeReleaseNULL(...)`.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-build-20260425-queue-workloop.txt`
  - build log sha256: `b484fae5f651446dcbab6588964e1c251035eaa2e73a1f1370c7ae9121deeeea`
  - built binary UUID: `79C5C1BA-FBE3-3630-B6D1-D92234055077`
  - built binary sha256: `e78dc6e7253baae630d796f15379fd47f957b48556eaf4ec481db5e0912cb513`
- disassembly verification:
  - start focus: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-start-queue-workloop-disasm-focus-20260425.txt`
  - start focus sha256: `e80a8ccbe0be19452684f478974ee48ee720e2f75342691588c9b073882cbd94`
  - start focus shows two `_fWorkloop` virtual calls through slot `0x140` with `fTxQueue` and `fRxQueue`, matching `IOWorkLoop::addEventSource(IOEventSource*)`.
  - start focus shows queue `getWorkLoop()` virtual calls through slot `0x168` for both queues before logging.
  - stop focus: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-104-stop-queue-workloop-disasm-focus-20260425.txt`
  - stop focus sha256: `1eb7e3db6a0591423dbb6dea79ebb89703f216337bef66e6bae5026aed6bb4f6`
  - stop focus shows `getWorkLoop()` checks through slot `0x168`, comparison with `_fWorkloop`, and removal calls through `_fWorkloop` slot `0x148`, matching `IOWorkLoop::removeEventSource(IOEventSource*)`, before queue release.
- diff artifact:
  - `/Users/bob/Projects/itlwm/commit-approval/artifacts/CR-104-queue-workloop.diff`
  - diff artifact will be regenerated in the Stage 1 request after this implementation-verification section is included.
- notes:
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-DATA-SKYWALK-RX-SOURCEQUEUE-PANIC-031
- status: CONFIRMED_ROOT_CAUSE
- symptom: CR-102 panics during an association attempt in the RX Skywalk path with `"default packetCompletion()" @IOSkywalkPacketQueue.cpp:321`.
- first visible manifestation: user-installed CR-102 binary UUID `84A26605-1A2A-34D0-927B-3E06707070E1` showed networks, then a connection attempt crashed; panic file `/Users/bob/Projects/itlwm/crash.txt` reports `skywalkRxReturnPreparedPacket(AirportItlwm*, IOSkywalkPacket*) + 0x2f` called from `skywalkRxInput + 0x4ac`.
- expected system behavior: a packet created locally by the RX producer and enqueued to `IOSkywalkRxCompletionQueue` must not record that same completion queue as its source queue, because completion queues do not implement source packet completion.
- actual behavior: CR-102 calls `rxPkt->prepareWithQueue(fRxQueue, kIOSkywalkPacketDirectionRx, 0)`, which stores `fRxQueue` as the packet source queue; any later RX `completeWithQueue(...)` calls `fRxQueue->packetCompletion(...)`, and that slot resolves to the base default panic.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` at the `prepareWithQueue(fRxQueue, ...)` call, and `skywalkRxReturnPreparedPacket(...)` at `completeWithQueue(fRxQueue, ...)`.
- evidence:
  - panic logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-aftercrash-panic-20260425.txt`, sha256 `812d84b2b2027b92a4fbaf62d87c24feecfa11980fe1b7616270aeb4e3dc8627`, shows `default packetCompletion()` reached from `skywalkRxReturnPreparedPacket(...)`.
  - runtime logs: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-aftercrash-log-20260425.txt`, sha256 `4360340032a34200575c607fae2219b03efc39b4d72782440b6c979a0823e213`; loaded kext UUID matches the CR-102 reviewed runtime binary.
  - runtime diag: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-102-aftercrash-regdiag-snapshot-20260425.txt`, sha256 `8e6087332143add782230e14d2de25ec93c6a101a16ff8d27ef5a7715c1153e4`, and trace sha256 `2240416673519a96cb64b01bab8aa7f6cc0c1fd929bff2d03a267d952d87b5e6`, show diagnostics were not enabled, so the panic is not a diagnostic side effect.
  - disassembly: CR-102 `skywalkRxInput + 0x4ac` maps to the failed-enqueue branch call to `skywalkRxReturnPreparedPacket(...)`; helper disassembly calls packet slot `*0x198` with direction `2`, then pool slot `*0x140`.
  - decomp: `IOSkywalkPacket::prepareWithQueue(queue, direction, options)` stores `param_1[7] = queue`, sets prepared bit `2`, and populates packet buffers.
  - decomp: `IOSkywalkPacket::completeWithQueue(queue, 2, options)` loads `param_1[7]` and, if non-null, calls `sourceQueue->packetCompletion(packet, queue, 0)` through queue vtable slot `+0x220`.
  - symbol table: BootKC exports `IOSkywalkRxCompletionQueue` methods but no `IOSkywalkRxCompletionQueue::packetCompletion(...)`; only `IOSkywalkRxSubmissionQueue::packetCompletion(...)`, `IOSkywalkTxSubmissionQueue::packetCompletion(...)`, and user-network submission queues override that source-completion slot. `IOSkywalkPacketQueue::packetCompletion(...)` is the default panic implementation.
  - decomp: `IOSkywalkRxCompletionQueue::enqueuePacketsForNetworking(...)` and `enqueuePacketsForKPipe(...)` call packet `completeWithQueue(fRxQueue, 2, 0)` during successful ownership transfer; if packet source queue is the same RX completion queue, the same default source-completion panic is reachable on success as well.
- candidate causes:
  - confirmed: CR-102 misuses destination `fRxQueue` as packet source queue during preparation.
  - confirmed: CR-102 cleanup also passes `fRxQueue` to `completeWithQueue(...)`; with the current poisoned source queue this deterministically reaches default `packetCompletion()`.
  - insufficient data: the next association blocker after RX enqueue is not observable until this source-queue lifecycle panic is removed.
- rejected causes:
  - CR-102 cleanup ordering alone: rejected because cleanup now calls `completeWithQueue(...)`, but the panic happens inside the source-queue callback recorded earlier by `prepareWithQueue(fRxQueue, ...)`.
  - RX pool direct-deallocate alone: rejected for CR-102 because the stack enters packet `completeWithQueue(...)` before pool deallocation.
  - diagnostics side effect: rejected because regdiag snapshot/trace properties were absent and the crashing path is unconditional RX packet lifecycle code.
- confirmed deviation: a locally allocated RX packet has no submission/source queue; CR-102 records the RX completion queue as source queue even though that class inherits the default panic `packetCompletion(...)`.
- root cause: destination queue and source queue are conflated. Tahoe `completeWithQueue(..., Rx, ...)` treats packet `sourceQueue` as an optional producer-side callback target; `IOSkywalkRxCompletionQueue` is a destination queue and cannot be used there.
- fix: planned for CR-103. Prepare locally allocated RX packets with `sourceQueue=nullptr` while preserving direction `kIOSkywalkPacketDirectionRx`, and unwind failure paths with `completeWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0)` before pool return.
- verification:
  - source check: RX `prepareWithQueue(...)` and prepared cleanup no longer pass `fRxQueue` as source queue.
  - build check: Tahoe build succeeds and BootKC unresolved-symbol validation passes.
  - disassembly check: RX prepare call has `rsi=0`, `edx=2`; cleanup helper has `rsi=0`, `edx=2` before pool deallocation.
  - runtime check after Stage 1 approval: reboot, attempt one join, and verify no `"default packetCompletion()"` panic; then collect sudo logs/regdiag for the next association blocker if join still fails.
- notes:
  - This supersedes the CR-102 lifecycle conclusion: completing before pool return is necessary, but the packet source queue must be nullable for locally created RX packets.

## FIX_CANDIDATE

- anomaly_id: A-DATA-SKYWALK-RX-SOURCEQUEUE-PANIC-031
- symptom: CR-102 crashes with `default packetCompletion()` from `skywalkRxReturnPreparedPacket(...)` during RX path association traffic.
- expected system behavior: a locally allocated RX packet uses RX direction state for buffer population/completion, but records no producer/source queue unless it actually originated from a queue class that implements `packetCompletion(...)`.
- actual behavior: local RX packet preparation records `fRxQueue` as source queue, and completion calls the default `IOSkywalkPacketQueue::packetCompletion(...)` panic.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` `rxPkt->prepareWithQueue(that->fRxQueue, kIOSkywalkPacketDirectionRx, 0)` and `skywalkRxReturnPreparedPacket(...)` `completeWithQueue(that->fRxQueue, kIOSkywalkPacketDirectionRx, 0)`.
- evidence from runtime:
  - CR-102 panic file sha256 `812d84b2b2027b92a4fbaf62d87c24feecfa11980fe1b7616270aeb4e3dc8627` shows the exact panic and helper stack.
  - loaded crashing UUID is `84A26605-1A2A-34D0-927B-3E06707070E1`, matching the CR-102 reviewed runtime binary.
  - CR-102 after-crash log sha256 `4360340032a34200575c607fae2219b03efc39b4d72782440b6c979a0823e213` confirms the same installed/loaded identity.
- evidence from decomp:
  - `IOSkywalkPacket::prepareWithQueue` stores the `queue` argument as packet source queue and sets prepared state bit `2`.
  - `IOSkywalkPacket::completeWithQueue(..., direction=2, ...)` invokes `sourceQueue->packetCompletion(...)` only when the stored source queue is non-null.
  - `IOSkywalkPacketBuffer::prepareWithQueue` and `completeWithQueue` also guard source callbacks on nullable source queue, proving nullable source queue is a first-class contract.
  - `IOSkywalkRxCompletionQueue` has no `packetCompletion(...)` override in the BootKC symbol table; the base implementation is the panic site.
  - `IOSkywalkRxSubmissionQueue` and `IOSkywalkTxSubmissionQueue` do override `packetCompletion(...)`, which proves the callback is reserved for submission/source queues, not completion/destination queues.
  - `IOSkywalkRxCompletionQueue::enqueuePacketsForNetworking/KPipe` complete packets with the destination queue as the completion queue argument; this is separate from the packet's stored source queue.
- exact semantic mismatch between reference and our code: local code writes the destination RX completion queue into the packet's source-queue field, causing Tahoe's completion path to call a default-panic source callback on the wrong queue class.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
    - `IOSkywalkPacketBufferPool::allocatePacket(...)`
    - `IOSkywalkPacket::prepareWithQueue(sourceQueue, kIOSkywalkPacketDirectionRx, 0)`
    - packet data setup through `getPacketBuffers(...)`, `getDataVirtualAddress()`, `mbuf_copydata(...)`, `setDataOffsetAndLength(...)`
    - `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket * const *, ...)`
    - `IOSkywalkPacket::completeWithQueue(completionQueue, kIOSkywalkPacketDirectionRx, 0)`
    - `IOSkywalkPacketBufferPool::deallocatePacket(...)`
  - expected contract at each touchpoint:
    - allocation gives the driver a local pool packet with no producer/source queue.
    - preparation with RX direction populates packet buffers and marks the packet prepared.
    - nullable source queue suppresses source-queue completion callbacks; this is explicitly guarded in packet and packet-buffer completion decomp.
    - enqueue success lets the RX completion queue run the destination-side `completeWithQueue(fRxQueue, 2, 0)` transfer.
    - enqueue/setup failure leaves ownership local and requires a local prepared-state unwind without source callback before pool return.
  - why no relevant touchpoints are missing:
    - the panic occurs entirely within the prepared packet lifecycle; all producer-side entry, payload setup, destination enqueue, completion, and pool-return edges are covered.
  - why proposed path adds no extra system-visible side effects:
    - no retry, duplicate enqueue, forced state, event replay, fallback, delay, or diagnostic intervention is added.
    - packet direction and payload publication remain unchanged; only the invalid source-queue callback target is removed.
- why this is root cause and not just correlation:
  - runtime lands exactly in `packetCompletion()` invoked from CR-102 helper.
  - decomp proves the only way for `completeWithQueue(..., Rx, ...)` to call `packetCompletion()` is a non-null stored source queue.
  - source code proves that stored source queue is `fRxQueue`.
  - symbol table proves `fRxQueue`'s class lacks the required override and therefore resolves to the panic implementation.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it preserves Tahoe's destination RX completion queue path and RX direction semantics while respecting the optional source-queue contract for locally produced packets.
  - it avoids inventing a fake source queue and avoids calling contested Wi-Fi methods or changing association/control state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxReturnPreparedPacket`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - keep `fRxQueue` and suppress the panic: rejected because it masks a wrong queue-class callback.
  - use `fTxQueue` as source queue: rejected because it is also unrelated to the local RX producer and would create a false completion callback.
  - create an `IOSkywalkRxSubmissionQueue`: rejected because the local registered interface path currently uses one TX submission queue and one RX completion queue; adding a third queue changes the system-visible queue set.
  - direct pool deallocate without completing prepared state: rejected because CR-101 already proved prepared direct deallocation can panic.
  - retry enqueue on failure: rejected as unproven fallback and duplicate-delivery risk.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - build with `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - inspect built `skywalkRxInput(...)` and helper disassembly to confirm null source queue and RX direction
  - create CR-103 Stage 1 request; do not install or commit until reviewer allows after-fix runtime

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: `IOSkywalkPacket::prepareWithQueue` stores the source queue, `IOSkywalkPacket::completeWithQueue(..., direction=2, ...)` calls stored `sourceQueue->packetCompletion(...)` only when non-null, and packet/buffer completion both guard nullable source queues.
- Есть ли прямое подтверждение по runtime-данным? Да: CR-102 panic stack enters `skywalkRxReturnPreparedPacket(...)` and dies in the default `packetCompletion()` path on binary UUID `84A26605-1A2A-34D0-927B-3E06707070E1`.
- Доказал ли я причинность, а не просто корреляцию? Да: source code writes `fRxQueue` into the packet source queue, decomp proves that exact field is later called through slot `+0x220`, and BootKC symbols prove `IOSkywalkRxCompletionQueue` has no override for that slot.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Да within the system contract: locally allocated RX packets have no producer/source queue, but still use RX direction and the existing RX completion destination queue for enqueue.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Нет.
- Не закрываю ли я симптом вместо причины? Нет: the invalid source-queue callback target is removed before both failure cleanup and successful RX completion can reach it.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: all are listed above with artifact paths and sha256 values.

## IMPLEMENTATION VERIFICATION

- id: CR-103-RX-NULL-SOURCEQUEUE
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-DATA-SKYWALK-RX-SOURCEQUEUE-PANIC-031`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` now calls `rxPkt->prepareWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0)`.
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxReturnPreparedPacket(...)` now calls `rxPkt->completeWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0)` before `fRxPool->deallocatePacket(rxPkt)`.
  - RX enqueue still uses `fRxQueue->enqueuePackets(...)`; destination queue semantics are unchanged.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-103-build-20260425-rx-null-sourcequeue.txt`
  - build log sha256: `7df1a7ada641e489146d8b4ca2715753c1ed4115ff27996abce085393ba1b182`
  - built binary UUID: `EED02602-ECF0-3C65-92D2-95F2E3EE0468`
  - built binary sha256: `45a8230fdd6a06fc900d17e34cc7356f2901484af939534c0a93fca84bf3a27d`
- disassembly verification:
  - RX disassembly focus: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-103-skywalkRxInput-disasm-focus-20260425.txt`
  - RX disassembly focus sha256: `04871376c2a607a3529427fd75992ba623f5fcd92886c2de838875b84b8375c3`
  - RX prepare call shows `xorl %ecx, %ecx; movl %ecx, %esi` (`sourceQueue=nullptr`), `movl $0x2, %edx` (`direction=Rx`), and packet slot `*0x188`.
  - RX enqueue remains packet-array enqueue through `fRxQueue` and slot `*0x2a0`.
  - cleanup helper disassembly focus: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-103-skywalkRxReturnPreparedPacket-disasm-focus-20260425.txt`
  - cleanup helper disassembly focus sha256: `b661208d1ed39b3a400b5c6a6e663042cedcc4b065c61cc22accd948ddd1a453`
  - cleanup helper shows `sourceQueue/completionQueue=nullptr` in `esi`, `edx=2`, packet slot `*0x198`, then pool slot `*0x140`.
- diff artifact:
  - `/Users/bob/Projects/itlwm/commit-approval/artifacts/CR-103-rx-null-sourcequeue.diff`
  - final diff sha256 and line count are recorded in the CR-103 Stage 1 request to avoid self-referential diff hash churn.
- notes:
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-ASSOC-TAHOE-LEGACY-ZERO-ASSOC-DONE-036
- status: CONFIRMED_ROOT_CAUSE
- symptom: after CR-109, the driver and scan list are visible, local association to `CONTROL_STA_NETWORK` reaches the selected WCL BSSID and briefly enters link-up/no-internet, then no EAPOL/key install occurs and the AP deauths with reason 15.
- first visible manifestation: `2026-04-26 16:54:56 +0300` on loaded CR-109 binary UUID `66030ADC-0CDF-359F-BCA8-2F8FBEFF51ED`.
- expected system behavior: on Tahoe, infrastructure association completion is owned by the WCL join path and is delivered to `IO80211SkywalkInterface::reportDataPathEvents(type=9, data=<apple join payload>, len=468, gated=false)`. The payload carries the join status consumed by the WCL/IO80211 join manager.
- actual behavior: local net80211 `IEEE80211_EVT_STA_ASSOC_DONE` also posts legacy `APPLE80211_M_ASSOC_DONE` with `data=NULL,len=0,gated=true` before the WCL payloaded `type=9` completion.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp::eventHandler(...)` maps `IEEE80211_EVT_STA_ASSOC_DONE` to `APPLE80211_M_ASSOC_DONE` unconditionally for Tahoe.
- evidence:
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-110-current-latest-control_sta_network-focused-20260426.log`, sha256 `69bf3a35247782fb1884b10086bff0bef5e7eba9bd593cab213f928c129b980a`.
    - lines `18385..18802`: `setWCL_ASSOCIATE` now selects `BSSID=50:4f:3b:cd:dd:67 source=candidate candidates=1`, proving the CR-109 BSSID identity blocker is gone.
    - lines `18921..18922`: local `ieee80211_node_join_bss` selects the same BSSID `50:4f:3b:cd:dd:67`.
    - lines `18967..18996`: net80211 `eventHandler msgCode=1` posts `postMessageGated msg=9 dataLen=0`, which becomes `reportDataPathEvents type=0x9 data=0x0 dataLen=0 gated=1 ic_state=3`.
    - lines `19059..19149`: WCL then publishes the real current BSS/link state and `reportDataPathEvents type=0x9 data=<private> dataLen=468 gated=0 ic_state=4`.
    - counts in the same artifact: `type=0x9 data=0x0` appears 16 times; payloaded `type=0x9 dataLen=468` appears only 4 times; no `setCIPHER_KEY`, no logged EAPOL TX/RX, 118 `CWEAPOLClient ... failed to retrieve 8021X state (2)` lines, and 14 `Deauth received, reason 15` lines.
    - `commit-approval/runtime_evidence/CR-110-currentboot-control_sta_network-bssinfo-20260426.log` proves `syncTahoeCurrentApAddress` and `getWCL_BSS_INFO` return the same selected BSSID, so current-BSS publication is not the current blocker.
  - decomp:
    - remote `/srv/project/ghidra_output/IO80211Family_decompiled.c`, `IO80211SkywalkInterface::reportDataPathEvents(...)`, case `9`, around lines `47305..47323`: payloaded `Infra assoc DONE` calls the WCL join manager status update through `FUN_ffffff800226f7b6(..., param_3[0x6e])`.
    - the same case skips that status update when `param_3 == NULL`; the null legacy event only takes the common message path and lacks the WCL join status.
    - remote `WCLJoinManager::sendWCLJoinDone(...)`, around lines `125760..125940`, builds and sends the WCL join-done bulletin; the CR-110 runtime observes this as the payloaded `type=9,len=468` event.
    - remote `WCLNetManager::handleSupplicantEvent(...)`, around lines `8753..8765`, only accepts real supplicant payloads with `data != NULL` and `len == 0x28`, confirming that WCL-side state machines are payload-sensitive and not legacy zero-payload driven.
  - docs:
    - Tahoe event-payload notes in `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/86_concrete_event_payload_maps_checked*.yaml` classify extended WCL association/connect events as payloaded Tahoe events and old link/assoc events as legacy.
- candidate causes:
  - confirmed: local Tahoe publishes a non-reference, zero-payload legacy association completion before the WCL payloaded association completion.
  - rejected: CR-109 selected-BSSID mismatch remains active; current logs prove candidate BSSID and local `join_bss` now match.
  - rejected: current-BSS BSSID layout remains active; current logs prove `getWCL_BSS_INFO` returns BSSID `50:4f:3b:cd:dd:67` and valid channel/IE data.
  - insufficient data after this fix: once zero-payload assoc-done is removed, RX EAPOL, TX EAPOL, key install, DHCP, and steady-state data may expose the next blocker.
- rejected causes:
  - replay/duplicate `0x42` READY: rejected because CR-110 already has `type=0x42 dataLen=24`; replay would be forbidden and non-reference.
  - force `setCIPHER_KEY` / `RSN_HANDSHAKE_DONE`: rejected because no real EAPOL/key material has arrived through the Apple producer path.
  - delay/retry association or WCL events: rejected as timing heuristic; the divergence is a deterministic extra producer.
  - modify selected-BSSID again: rejected because CR-110 proves selected BSSID is now coherent.
- confirmed deviation: Tahoe WCL association completion has a payloaded WCL join-done producer; local code additionally sends a legacy null-payload association-complete producer from net80211.
- root cause: the first system-visible association-complete event on Tahoe is the local zero-payload legacy `APPLE80211_M_ASSOC_DONE`, so IO80211/WCL consumers observe an association completion without the WCL join-status payload before the real WCL join completion. The subsequent supplicant/key path never starts (`setCIPHER_KEY=0`, EAPOL TX/RX markers absent, repeated `CWEAPOLClient` state failures) and the AP deauths with reason 15.
- fix: on Tahoe only, do not translate `IEEE80211_EVT_STA_ASSOC_DONE` into legacy `APPLE80211_M_ASSOC_DONE`; leave WCL `sendWCLJoinDone` / payloaded `reportDataPathEvents(type=9,len=468)` as the sole association-complete producer. Non-Tahoe legacy behavior remains unchanged.
- verification:
  - source check: Tahoe branch returns from `eventHandler(...)` on `IEEE80211_EVT_STA_ASSOC_DONE` before `postMessageGated`.
  - build check: `git diff --check`, `bash -n scripts/build_tahoe.sh`, Tahoe build, BootKC unresolved-symbol validation.
  - runtime check after Stage 1 approval: install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, and verify no `postMessageGated msg=9 dataLen=0` / no `reportDataPathEvents type=0x9 data=0x0` during the join; payloaded `type=0x9 dataLen=468`, `0xd8`, `0xd5`, and `0x42` remain; then classify whether `setCIPHER_KEY`, EAPOL TX/RX, DHCP, and data progress or expose the next blocker.
- notes:
  - This is not a replay or suppression of a valid Tahoe producer. It removes a legacy producer that has no WCL payload and is not the Tahoe association-complete owner.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-TAHOE-LEGACY-ZERO-ASSOC-DONE-036
- symptom: CR-109 reaches coherent selected-BSSID association and WCL link/current-BSS publication, but RSN/EAPOL/key install does not begin and the AP deauths with reason 15.
- expected system behavior: Tahoe association completion is delivered by the WCL join-done path as payloaded `reportDataPathEvents(type=9, len=468)`, followed by WCL link/connect/ready state; legacy zero-payload `APPLE80211_M_ASSOC_DONE` must not race ahead of that owner.
- actual behavior: local `eventHandler(...)` posts `APPLE80211_M_ASSOC_DONE` with `data=NULL,len=0` for every net80211 `IEEE80211_EVT_STA_ASSOC_DONE`, including Tahoe hidden-WCL association.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::eventHandler(...)`, `case IEEE80211_EVT_STA_ASSOC_DONE`.
- evidence from runtime:
  - `CR-110-current-latest-control_sta_network-focused-20260426.log` shows CR-109 selected-BSSID alignment is fixed (`source=candidate`, `join_bss` BSSID `50:4f:3b:cd:dd:67`).
  - the same log shows `postMessageGated msg=9 dataLen=0` and `reportDataPathEvents type=0x9 data=0x0 dataLen=0 gated=1 ic_state=3` preceding the WCL `WCL Joined Bss`, `setWCL_LINK_STATE_UPDATE`, and payloaded `reportDataPathEvents type=0x9 dataLen=468`.
  - the same log shows no `setCIPHER_KEY`, no EAPOL TX/RX markers, repeated `CWEAPOLClient` failures, and AP deauth reason 15 after this malformed first assoc-complete edge.
- evidence from decomp:
  - `IO80211SkywalkInterface::reportDataPathEvents(...)` case `9` treats payloaded and null-payload paths differently; only the payloaded path calls the WCL join status update with `param_3[0x6e]`.
  - `WCLJoinManager::sendWCLJoinDone(...)` is the WCL join-done producer; runtime observes it as the payloaded `type=9,len=468` event.
  - AppleBCMWLAN has WCL/firmware join producers and no local net80211 legacy eventHandler that emits an extra null-payload `APPLE80211_M_ASSOC_DONE` before WCL join-done.
- exact semantic mismatch between reference and our code: local Tahoe publishes two association-complete producers for one association: a legacy null-payload producer first and the WCL payloaded producer second. Reference Tahoe semantics use the WCL payloaded producer for the WCL association state.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation:
  - the malformed zero-payload `type=9` is the first assoc-complete event in the failing runtime.
  - decomp proves payload is semantically significant for WCL join-status propagation.
  - runtime proves all earlier known blockers are cleared: networks visible, selected BSSID coherent, current BSS available, link-up/connect/ready events present.
  - the first missing downstream state appears immediately after this duplicate/null assoc-complete edge: Apple supplicant state cannot be retrieved and no key producer is called.
- why proposed fix is 1:1 with reference architecture and semantics:
  - it leaves the WCL join-done producer intact and removes only the extra legacy net80211 producer on Tahoe.
  - non-Tahoe legacy `IO80211Interface` behavior remains unchanged.
  - no event replay, duplicate publish, fake success, forced key, retry, delay, polling, or fallback is introduced.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::eventHandler`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - replay `0x42` READY or payloaded `0x9`: rejected because producer-side reference evidence for replay is absent and current runtime already contains these events.
  - fabricate `setCIPHER_KEY` / `RSN_HANDSHAKE_DONE`: rejected as fake RSN state.
  - delay WCL join, delay EAPOL, or extend join timeout: rejected as timing heuristic.
  - suppress AP deauth or report fake connected state: rejected as masking.
  - modify RX/TX packet lifecycles without fresh EAPOL evidence: rejected for this fix; current default diagnostics were off and the first proven divergence is the association-complete producer edge.
  - change CR-109 BSSID logic again: rejected by CR-110 runtime.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  - disassemble/grep built binary for the new Tahoe skip log string and absence of Tahoe assoc-done mapping in the affected branch
  - create CR-110 Stage 1 request; do not install, runtime-test, or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: `reportDataPathEvents(type=9)` has a payload-only WCL join-status update path; WCL join-done is a separate producer.
- Есть ли прямое подтверждение по runtime-данным? Да: CR-110 shows the local null-payload assoc-done arrives before WCL payloaded assoc-done, with all earlier selected-BSSID/current-BSS blockers cleared.
- Доказал ли я причинность, а не просто корреляцию? Да для текущего first bad system-visible edge: the malformed first assoc-complete lacks the payload required by the decompiled WCL status update, and the downstream supplicant/key path never starts after it.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Да: WCL join-done remains the sole Tahoe assoc-complete producer.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Да, не добавляю; удаляется только non-reference duplicate producer.
- Не закрываю ли я симптом вместо причины? Нет: исправляется точка расхождения в producer ownership for assoc completion.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: paths and line anchors are listed above.

## IMPLEMENTATION VERIFICATION

- id: CR-110-TAHOE-SKIP-LEGACY-ASSOCDONE
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-ASSOC-TAHOE-LEGACY-ZERO-ASSOC-DONE-036`
- source verification:
  - `AirportItlwm/AirportItlwmV2.cpp::eventHandler(...)` now handles `IEEE80211_EVT_STA_ASSOC_DONE` with a Tahoe-only early return.
  - Tahoe no longer maps this net80211 event to `APPLE80211_M_ASSOC_DONE`, so this branch cannot call `postMessageGated(...)` with `msg=9,data=NULL,len=0`.
  - Non-Tahoe behavior remains unchanged and still maps the legacy event to `APPLE80211_M_ASSOC_DONE`.
  - The retained current live diff still includes the already reviewed CR-107 queue enable lifecycle and CR-109 candidate-BSSID carrier fix because they are uncommitted and are part of the exact runtime baseline.
- build verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build: passed.
  - BootKC symbol verification: `OK: all 851 undefined symbols resolve against BootKC`.
  - build log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-110-build-20260426-tahoe-skip-legacy-assocdone.txt`
  - build log sha256: `dc2557bbedc2664fa18a52dc1640205578dc949bd6d0a139507161b90e259337`
  - built binary UUID: `CC6CA7E8-EFAA-3740-9BF1-D5F74CC93E64`
  - built binary sha256: `91b1e9c295d2b7c8220094354df720e21c6560d964853f670d1b3cca1df8e89d`
- binary verification:
  - built string evidence: `%s: DEBUG %s Tahoe: skip legacy zero-payload ASSOC_DONE; WCL JoinDone owns assoc completion`
  - eventHandler disassembly focus: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-110-eventhandler-disasm-focus-20260426.txt`
  - eventHandler disassembly focus sha256: `9a831f544df5141ada4bc6372e20f8c299b8d8e5d58b721c1751a120059c69df`
  - disassembly lines `283..298` show the assoc-done case logging branch jumping directly to `eventHandler` return at `0x51a8a`, before the shared `runAction(postMessageGated, ...)` path at lines `342..354`.
- runtime-before evidence:
  - current failing runtime log: `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-110-current-latest-control_sta_network-focused-20260426.log`
  - current failing runtime log sha256: `69bf3a35247782fb1884b10086bff0bef5e7eba9bd593cab213f928c129b980a`
  - runtime proves the selected BSSID is coherent after CR-109, WCL current-BSS/link events are present, but legacy `type=0x9,data=NULL,len=0` events race ahead of payloaded `type=0x9,len=468` WCL join-done events; no `setCIPHER_KEY` or EAPOL progress follows.
- notes:
  - This implementation intentionally does not add retries, delays, event replay, fake key state, forced RSN success, queue bypass, or data-path packet changes.
  - Do not install or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME` for the exact current diff.

## ANOMALY

- id: A-WCL-ROAM-LOCK-UNSUPPORTED-037
- status: FIX_IMPLEMENTED
- symptom: after CR-110 runtime, `CONTROL_STA_NETWORK` join reaches WCL join-done success and a transient link-up state, but RSN/key completion still does not occur; during the same join/link-up lifecycle, WCL `SET_ROAM_LOCK` commands fail with `0xe00002c7`.
- first visible manifestation: `2026-04-26 17:54:04..17:55:52 +0300` on loaded CR-110 binary UUID `CC6CA7E8-EFAA-3740-9BF1-D5F74CC93E64`.
- expected system behavior: Tahoe WCL roam manager sends `APPLE80211_IOC_WCL_SET_ROAM_LOCK` as a real one-byte IOUC command. The driver-side AppleBCMWLANCore handler rejects NULL with raw `0x16`, otherwise interprets the first byte as a boolean `roam_off` value and delegates it to `AppleBCMWLANRoamAdapter::setRoamLock(bool)`.
- actual behavior: local `AirportItlwmSkywalkInterface::setWCL_SET_ROAM_LOCK(...)` is an explicit unsupported stub returning `0xe00002c7`, and the local header comments still claim no Apple producer was recovered.
- divergence point: `AirportItlwm/AirportItlwmSkywalkInterface.cpp::setWCL_SET_ROAM_LOCK(...)` and the matching slot comment in `AirportItlwm/AirportItlwmSkywalkInterface.hpp`.
- evidence:
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-111-control_sta_network-current-focused-20260426-1814.log`, sha256 `c3bf2dc04ba2c7aa593cc1e723b707e674a740878ac012c91677cf7b1df21889`.
    - lines `53172..53173`: first WCL set fails before the first hidden associate attempt: `Set type=<APPLE80211_IOC_WCL_SET_ROAM_LOCK> ... res=<FAIL:-536870201:0xe00002c7>`.
    - lines `53288..53289`: second WCL set fails before the retry associate attempt.
    - lines `54360..54361`: WCL set fails exactly as NET_MANAGER reaches LINK_UP after `sendWCLJoinDone ... lastStatusCode=0`.
    - lines `65279..65280`: WCL set fails again during leave/key-done cleanup.
    - the same log proves CR-110 fixed its claim: `Tahoe: skip legacy` appears, `postMessageGated msg=9` is absent, `sendWCLJoinDone lastStatusCode=0` appears, and `airportd` reports `Successfully associated`.
  - ioreg:
    - current state after the failed attempt still has `IOInterfaceName=en0`, `IOLinkStatus=1`, and `IO80211RSNDone=No`, proving the failure is in the post-join WCL/RSN chain, not scan/UI publication.
  - decomp:
    - remote symbol corpus proves the producer exists: `AppleBCMWLANCore::setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock*) @ 0xffffff800160151a`, `AppleBCMWLANRoamAdapter::setRoamLock(bool) @ 0xffffff800154ee10`, `WCLRoamManager::setRoamLock(bulletinBoardMessage&) @ 0xffffff800212939e`, and `WCLRoamManager::updateRoamLockState(...) @ 0xffffff80021293cc`.
    - remote `IO80211Family_decompiled.c` around `FUN_ffffff80021293cc`: WCL updates a roam-lock source bitmask and sends `cmdIouc(0x1ac, ..., &local_41, 1, ...)`, proving the driver payload is exactly one byte.
    - remote `IO80211Family_decompiled.c` around `FUN_ffffff800212939e`: WCL bulletin handling reads source at payload `+0`, boolean at `+4`, force at `+5`, then calls `updateRoamLockState(...)`; this proves WCL owns the producer lifecycle, not a public user-space heuristic.
    - remote disassembly artifact `/srv/project/ghidra_output/CR111_roam_lock_disasm.txt`, sha256 `aa4d410a5921e3e1f9db8ee109d10dc83a3032a8b6b6b097a62af5be03bf87b7`, decodes `AppleBCMWLANCore::setWCL_SET_ROAM_LOCK`: non-NULL data reads byte `data[0]`, converts it to bool with `setne`, loads RoamAdapter from core offset `+0x15c0`, and tail-calls `AppleBCMWLANRoamAdapter::setRoamLock(bool)`.
    - remote `AppleBCMWLAN_Core_decompiled.c` / `AppleBCMWLANCoreMac_decompiled.c` around `AppleBCMWLANRoamAdapter::setRoamLock(bool)` proves the adapter programs a `"roam_off"` command with a 4-byte command payload containing the boolean argument.
  - docs:
    - local `docs/tahoe_signal_chain_audit.md` and current source comments saying no Apple producer was recovered are stale and contradicted by the decomp/runtime evidence above.
- candidate causes:
  - confirmed: local slot `[591]` returns unsupported for a real WCL command that Tahoe sends in the association/link-up lifecycle.
  - insufficient data: this deviation is not yet proven to be the sole cause of missing `setCIPHER_KEY`/EAPOL; it must be removed before the remaining RSN/key/data blocker can be attributed cleanly.
  - insufficient data: `setWCL_REASSOC(...)` also differs from recovered AppleBCMWLANCore because local code has an extra `ic_state == RUN` pre-gate; it is downstream after AP deauth in the current runtime and needs separate proof before patching.
- rejected causes:
  - reintroducing legacy assoc-done: rejected because CR-110 after-runtime proves that zero-payload assoc-done is gone while WCL join-done succeeds.
  - forcing `IO80211RSNDone` or `setCIPHER_KEY`: rejected because no real key material has been observed.
  - retrying, delaying, or replaying WCL events: rejected by protocol and no producer-side reference evidence.
- confirmed deviation: reference has a real one-byte WCL roam-lock driver command path; local code advertises the slot but returns `kIOReturnUnsupported`.
- root cause: not claimed as the sole RSN/EAPOL root cause. This is a confirmed system-facing deviation in the active join/link-up chain, and current user instruction allows proactive 1:1 removal of such divergences when the reference contract is known. The remaining no-internet/deauth symptom must still be reclassified after this unsupported WCL command is removed.
- fix: replace the unsupported stub with a narrow Tahoe-compatible handler: NULL returns raw `0x16`, non-NULL consumes only byte `data[0]` as `roam_off`, stores the exact WCL roam-lock state in driver-owned state, logs the state, and returns success without emitting events, changing join state, forcing keys, or touching packet queues.
- verification:
  - source check: `setWCL_SET_ROAM_LOCK(...)` no longer returns unsupported for non-NULL data and does not change association, key, EAPOL, RX, TX, or WCL completion code.
  - structural check: `git diff --check`; exact patch artifact for CR-111; Stage 1 request before build/install/runtime.
  - after approval runtime check: install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, verify no `APPLE80211_IOC_WCL_SET_ROAM_LOCK ... 0xe00002c7`, verify `WCL [591] ... roam_off=<0|1>` logs appear at the same lifecycle points, then reclassify the first remaining blocker by `setCIPHER_KEY`, EAPOL RX/TX, DHCP/IP, and data-path evidence.
- notes:
  - This patch intentionally does not implement Apple firmware command transport `"roam_off"` because the local port does not carry `AppleBCMWLANRoamAdapter`; it preserves the exact system-facing WCL IOUC contract and state carrier without adding a guessed firmware substitute.

## FIX_CANDIDATE

- anomaly_id: A-WCL-ROAM-LOCK-UNSUPPORTED-037
- symptom: after CR-110, WCL join-done/connect-complete succeeds, but the association still falls out before RSN/data completion; in that same lifecycle, `APPLE80211_IOC_WCL_SET_ROAM_LOCK` repeatedly fails with `0xe00002c7`.
- expected system behavior: WCL sends selector `0x1ac` with a one-byte boolean payload; AppleBCMWLANCore consumes byte `0` as `roam_off` and delegates it to the roam adapter.
- actual behavior: local Tahoe interface returns unsupported for every `setWCL_SET_ROAM_LOCK(...)` call.
- exact divergence point: `AirportItlwmSkywalkInterface::setWCL_SET_ROAM_LOCK(...)`.
- evidence from runtime:
  - `CR-111-control_sta_network-current-focused-20260426-1814.log` lines `53172..53173`, `53288..53289`, `54360..54361`, and `65279..65280` show the same WCL selector failing with `0xe00002c7` during associate, retry, link-up, and cleanup.
  - the same runtime shows networks visible, selected-BSSID coherent, no legacy zero-payload assoc-done, WCL join-done success, and `Successfully associated`, so this is not a scan/UI or CR-110 regression.
- evidence from decomp:
  - `WCLRoamManager::updateRoamLockState(...)` sends `cmdIouc(0x1ac, ..., &local_41, 1, ...)`; the payload length is exactly one byte.
  - `AppleBCMWLANCore::setWCL_SET_ROAM_LOCK(...)` disassembly consumes `data[0] != 0` and tail-calls `AppleBCMWLANRoamAdapter::setRoamLock(bool)`.
  - `AppleBCMWLANRoamAdapter::setRoamLock(bool)` builds the `"roam_off"` command with that boolean payload.
- exact semantic mismatch between reference and our code: reference accepts and consumes a real one-byte WCL roam-lock command; local code returns unsupported and discards it.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: WCL IOUC selector `0x1ac`; `apple80211_set_roam_lock` non-NULL payload byte `0`; NULL return code; local stored roam-lock state; log evidence; absence of association/key/data side effects.
  - expected contract at each touchpoint: selector `0x1ac` must not return unsupported for valid WCL payload; byte `0` is interpreted as the boolean roam-off state; NULL is rejected with raw `0x16`; no WCL join/key/data state is fabricated; no retry/replay/delay is introduced.
  - why no relevant touchpoints are missing: within this claim scope, the observed failure is the WCL command contract itself. The Apple hidden roam-adapter firmware owner is not present in itlwm, and using a guessed local firmware/scan substitute would create an unproven extra side effect. The remaining RSN/EAPOL/DHCP touchpoints are explicitly outside this claim and will be classified after this command stops failing.
  - why proposed path adds no extra system-visible side effects: the patch only stores the incoming one-byte state and returns the reference non-NULL success result; it emits no events, changes no link/assoc state, installs no keys, sends no frames, does not touch RX/TX queues, and does not alter scan results.
- why this is root cause and not just correlation:
  - root cause of the `WCL_SET_ROAM_LOCK -> 0xe00002c7` failures is confirmed by direct source/decomp/runtime evidence.
  - this candidate does not claim to be the sole cause of missing RSN/EAPOL completion; it removes a confirmed Tahoe contract deviation in the active failing lifecycle so the next runtime can expose the true remaining blocker without this unsupported-command noise.
- why proposed fix is 1:1 with reference architecture and semantics:
  - payload shape matches reference exactly: one byte, `data[0] != 0`.
  - NULL contract matches the recovered raw `0x16` path.
  - the patch does not invent a local firmware command; it preserves the recovered command state in the local owner until a proven local roam-adapter equivalent is available.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp::setWCL_SET_ROAM_LOCK`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` local payload struct and init state
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp` cached state members and slot comment
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - return success without consuming payload: rejected because reference consumes byte `0`.
  - send a guessed local reassoc/scan/roam command: rejected because no exact local equivalent to `AppleBCMWLANRoamAdapter::setRoamLock(bool)` is proven.
  - alter EAPOL/key/DHCP paths in the same patch: rejected because WCL roam-lock failure is a separate confirmed contract deviation and EAPOL root cause remains unproven after CR-110.
  - patch `setWCL_REASSOC(...)` now: rejected for this batch because the extra local RUN gate is downstream after AP deauth in current runtime and needs its own proof/safe 1:1 carrier handling.
  - replay or delay WCL events: rejected by protocol.
- verification plan:
  - `git diff --check`
  - create exact CR-111 patch artifact and Stage 1 structural request
  - do not build, install, runtime-test, or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`
  - after approval, build via Tahoe script, copy to `/L/E/` without unloading, reboot, retry `CONTROL_STA_NETWORK`, and collect sudo logs proving `WCL_SET_ROAM_LOCK` no longer fails plus next-stage RSN/EAPOL/DHCP evidence.

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Да: WCL sends selector `0x1ac` with one byte; AppleBCMWLANCore consumes byte `0` and calls `setRoamLock(bool)`.
- Есть ли прямое подтверждение по runtime-данным? Да: current CR-111 runtime shows repeated `WCL_SET_ROAM_LOCK -> 0xe00002c7` exactly in the join/link-up lifecycle.
- Доказал ли я причинность, а не просто корреляцию? Да для unsupported-command failure itself; no для полного EAPOL/DHCP blocker, поэтому claim scope ограничен confirmed deviation.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Да для system-facing selector payload/null/success contract; hidden firmware roam owner intentionally not guessed.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Да, не добавляю; нет retry/delay/replay/fake state.
- Не закрываю ли я симптом вместо причины? Нет: исправляется прямое расхождение `unsupported` vs real WCL command, а оставшийся no-internet/deauth остаётся открытым.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: artifacts and paths are listed above.

## IMPLEMENTATION VERIFICATION

- id: CR-111-WCL-ROAM-LOCK-CONTRACT
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-WCL-ROAM-LOCK-UNSUPPORTED-037`
- source verification:
  - `AirportItlwmSkywalkInterface::setWCL_SET_ROAM_LOCK(...)` now rejects NULL with raw `0x16`.
  - non-NULL payload is treated as the exact one-byte WCL carrier; byte `0` is stored as `cachedWclRoamLocked`.
  - the handler logs `WCL [591] ... roam_off=<0|1>` and returns success without posting events, forcing link/RSN state, sending frames, or touching RX/TX queues.
  - the stale slot comment now points to the recovered WCLRoamManager / AppleBCMWLANCore producer path.
- structural verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build/install/runtime not performed: Stage 1 approval is required first.
- runtime-before evidence:
  - `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-111-control_sta_network-current-focused-20260426-1814.log`
  - sha256 `c3bf2dc04ba2c7aa593cc1e723b707e674a740878ac012c91677cf7b1df21889`
  - the log shows four `APPLE80211_IOC_WCL_SET_ROAM_LOCK -> 0xe00002c7` failures in the join/link-up/cleanup lifecycle while CR-110's assoc-done fix remains effective.
- decomp evidence:
  - `/srv/project/ghidra_output/CR111_roam_lock_disasm.txt`
  - sha256 `aa4d410a5921e3e1f9db8ee109d10dc83a3032a8b6b6b097a62af5be03bf87b7`
  - the disassembly proves `data[0] != 0` is the driver-side boolean consumed by `AppleBCMWLANCore::setWCL_SET_ROAM_LOCK(...)`.
- notes:
  - This implementation is intentionally not a full local roaming engine. It removes the confirmed WCL system-contract deviation and leaves RSN/EAPOL/DHCP root-cause classification to the next runtime.

## ANOMALY

- id: A-RSN-EAPOL-PASSIVE-PROBE-038
- status: CORRELATED
- symptom: after CR-110 runtime, Tahoe WCL join-done succeeds and the UI reaches a transient associated/no-internet state, but `IO80211RSNDone` remains `No`, no `setCIPHER_KEY(...)` producer is observed, and the AP eventually deauths with reason 15.
- first visible manifestation: `2026-04-26 17:54:19..17:55:52 +0300` in `CR-111-control_sta_network-current-focused-20260426-1814.log`.
- expected system behavior: after successful WCL join-done, RSN should progress through inbound EAPOL, optional outbound EAPOL, Apple key installation via `setCIPHER_KEY(...)`, `IO80211RSNDone`, DHCP/IP, and data traffic.
- actual behavior: the current default log proves successful WCL join-done and `airportd` association, but it does not prove whether EAPOL RX reaches `skywalkRxInput(...)`, whether EAPOL TX reaches `skywalkTxAction(...)` / `outputPacket(...)`, or whether the failure is before `setCIPHER_KEY(...)`.
- divergence point: unresolved within the post-WCL-join RSN/data chain; current evidence narrows the unknown to EAPOL RX delivery, EAPOL TX delivery, Apple supplicant/key producer, or post-key data.
- evidence:
  - runtime logs:
    - `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-111-control_sta_network-current-focused-20260426-1814.log`
    - sha256 `c3bf2dc04ba2c7aa593cc1e723b707e674a740878ac012c91677cf7b1df21889`
    - lines `53710..53722`: `sendWCLJoinDone lastStatusCode=0 extendedCode=0` and payloaded `reportDataPathEvents type=0x9 dataLen=468`.
    - line `53751`: `airportd` reports `Successfully associated`.
    - lines `54360..54361` and `65279..65280`: active WCL `SET_ROAM_LOCK` failures, addressed by `A-WCL-ROAM-LOCK-UNSUPPORTED-037`.
    - lines `54524..60141`: repeated AP deauth reason 15 after transient association.
    - line `65275`: `handleKeyDone` reports `IO80211RSNDone set=<0>`.
    - focused grep shows no `setCIPHER_KEY`, no `APPLE80211_IOC_CIPHER_KEY`, no `input EAPOL`, and no `output EAPOL` markers in the default log.
  - decomp:
    - `AppleBCMWLANCore::setWCL_REASSOC(...)` delegates to `AppleBCMWLANNetAdapter::sendReassocCommand(...)`; decomp of `sendReassocCommand(...)` contains an associated-state check and returns `0xe00002bc` on not-associated, so the current `WCL_REASSOC -> 0xe00002bc` after deauth is not a safe pre-deauth root fix.
    - `AppleBCMWLANCore::setEAP_FILTER_CONFIG(...)` rejects NULL with `0xe00002bc`, stores the first dword, and enters a core vtable path; current local handling already satisfies the visible carrier contract and no runtime selector failure is observed.
    - `AppleBCMWLANInfraProtocol::setCIPHER_KEY(...)` / existing local `setCIPHER_KEY(...)` remain the real key-install producer path, but the runtime has no call to that producer.
- candidate causes:
  - confirmed for a parallel system-facing deviation: `WCL_SET_ROAM_LOCK` is a real one-byte WCL selector and local code returned unsupported; this is covered by `A-WCL-ROAM-LOCK-UNSUPPORTED-037`.
  - insufficient data: inbound EAPOL may not reach `skywalkRxInput(...)`.
  - insufficient data: inbound EAPOL may reach RX but fail before/at `fRxQueue->enqueuePackets(...)`.
  - insufficient data: Apple supplicant may not emit outbound EAPOL.
  - insufficient data: outbound EAPOL may be dropped in `skywalkTxAction(...)` before `outputPacket(...)`.
  - insufficient data: key installation may not call `setCIPHER_KEY(...)` after EAPOL.
- rejected causes:
  - patch `setWCL_REASSOC(...)` in this batch: rejected because current failure is after AP deauth and recovered reference `sendReassocCommand(...)` also has an associated-state guard with `0xe00002bc` not-associated return.
  - patch `getWCL_BSS_INFO(...)` in this batch: rejected because current runtime reaches WCL join-done success and `airportd` association; repeated BSS-info failures appear after the link is already lost.
  - add `APPLE80211_IOC_CIPHER_KEY` routing without a producer attempt: rejected because current logs show no key command attempt or route failure.
  - force `IO80211RSNDone`, fabricate keys, replay events, delay joins, or retry EAPOL: rejected by protocol and would mask the missing producer/packet evidence.
- confirmed deviation: none yet for the remaining RSN/EAPOL layer; this anomaly exists to justify passive instrumentation only.
- root cause: not claimed. The post-WCL-join symptom is correlated with a missing RSN/key transition, but current logs do not identify the first missing EAPOL/key touchpoint.
- fix: add bounded, behavior-neutral EAPOL probe logs in the existing RX/TX packet paths so the next single reboot can classify whether the blocker is RX ingress, RX enqueue, TX emission, key producer, or later data.
- verification:
  - source check: probe reads only Ethernet EtherType/length already present in the packet buffer or mbuf; no packet bytes, ordering, queues, ownership, return values, gating, or WCL state are changed.
  - structural check: `git diff --check`, `bash -n scripts/build_tahoe.sh`.
  - after Stage 1 approval: build/install without unloading, reboot, attempt one `CONTROL_STA_NETWORK` join, then collect sudo logs for `ITLWM_EAPOL`, `output EAPOL packet`, `setCIPHER_KEY`, `IO80211RSNDone`, DHCP/IP, and AP deauth.
- notes:
  - This is intentionally a batch companion to the `WCL_SET_ROAM_LOCK` contract fix. It is not a speculative RSN fix.

## FIX_CANDIDATE

- anomaly_id: A-RSN-EAPOL-PASSIVE-PROBE-038
- symptom: WCL join-done succeeds and the UI shows transient association, but RSN/key/data completion does not happen and current logs lack enough EAPOL visibility to classify the next root cause in one reboot.
- expected system behavior: post-join EAPOL RX/TX and `setCIPHER_KEY(...)` are observable enough to prove where RSN fails without changing packet behavior.
- actual behavior: with default diagnostics off, `skywalkRxInput(...)` does not log EAPOL RX success/failure, and `skywalkTxAction(...)` can drop a Skywalk packet before `outputPacket(...)` without an EAPOL-specific marker.
- exact divergence point: observability gap at `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput(...)` and `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction(...)`.
- evidence from runtime:
  - `CR-111-control_sta_network-current-focused-20260426-1814.log` shows WCL join-done success, `airportd` association, `IO80211RSNDone set=<0>`, no `setCIPHER_KEY`, no `APPLE80211_IOC_CIPHER_KEY`, and no EAPOL markers.
  - repeated reason-15 deauths prove the failure is in the RSN/key window after association, not scan/UI publication.
- evidence from decomp:
  - no decomp evidence is required for a pure diagnostic probe; decomp is used only to reject speculative fixes in nearby selectors (`WCL_REASSOC` and `EAP_FILTER_CONFIG`).
- exact semantic mismatch between reference and our code: not claimed; this is `DIAGNOSTIC_INSTRUMENTATION`, not a reference-alignment fix.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated: EAPOL RX absent; EAPOL RX present but dropped before enqueue; EAPOL RX enqueued but Apple supplicant emits no TX; EAPOL TX emitted but dropped before `outputPacket`; EAPOL exchange reaches key material but `setCIPHER_KEY` is not called; post-key data remains broken.
  - exact probe points: `skywalkRxInput(...)` after EtherType detection and at each existing RX terminal result; `skywalkTxAction(...)` after valid Skywalk packet data is available and at pre-output drop/output result; existing `outputPacket(...)` EAPOL log; existing `setCIPHER_KEY(...)` log.
  - why these probe points are sufficient: they bracket the packet boundary before userspace (RX), the userspace-to-driver Skywalk TX boundary, the legacy hardware output queue, and the existing key-install producer without adding new producers.
  - why instrumentation is behavior-neutral: it only reads EtherType/length from already-owned packet memory and emits bounded `XYLog` lines; it does not mutate packet data, queue state, link state, WCL state, return codes, ownership, ordering, or timing logic.
  - what exact runtime evidence must be collected: first-N `ITLWM_EAPOL path=rx|tx stage=... len=... result=...` lines, any existing `output EAPOL packet` line, any `setCIPHER_KEY` line, `IO80211RSNDone`, DHCP/IP evidence, and deauth reason/timing.
- why this is root cause and not just correlation: not claimed; this patch is explicitly limited to missing evidence in a localized post-join causal chain.
- why proposed fix is 1:1 with reference architecture and semantics: not applicable; no behavior is changed. It preserves all existing producers and packet paths.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::airportItlwmRegDiagMbufIsEapol`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - force `setCIPHER_KEY` / `IO80211RSNDone`: rejected as fake key state.
  - add retries, delays, polling, or event replay: rejected by protocol.
  - patch `WCL_REASSOC` now: rejected by reference associated-state guard and current after-deauth timing.
  - patch `BSS_INFO` now: rejected as downstream after current-BSS/join success.
  - route `CIPHER_KEY` speculatively: rejected because no CIPHER_KEY producer attempt is observed.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - generate a superseding batch patch artifact and Stage 1 request.
  - do not build, install, runtime-test, or commit until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## SELF-CHECK

- Есть ли прямое подтверждение по декомпилу? Для диагностической вставки оно не требуется; декомпил использован для отказа от неподтвержденных фиксов `WCL_REASSOC` и `EAP_FILTER_CONFIG`.
- Есть ли прямое подтверждение по runtime-данным? Да: текущий лог локализует сбой после WCL join-done и до `setCIPHER_KEY`/`IO80211RSNDone`, но не содержит RX/TX EAPOL markers.
- Доказал ли я причинность, а не просто корреляцию? Нет, и поэтому кодовый change class строго `DIAGNOSTIC_INSTRUMENTATION`.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Поведение не меняется; probe не добавляет producer/consumer path.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? Да, не добавляю.
- Не закрываю ли я симптом вместо причины? Нет: цель - собрать missing runtime evidence за один reboot после удаления уже доказанного WCL roam-lock deviation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, divergence point, runtime evidence? Да: paths and artifacts are listed above.

## IMPLEMENTATION VERIFICATION

- id: CR-112-POSTASSOC-WCL-RSN-BATCH
- status: FIX_IMPLEMENTED
- anomaly covered:
  - `A-WCL-ROAM-LOCK-UNSUPPORTED-037`
  - `A-RSN-EAPOL-PASSIVE-PROBE-038`
- source verification:
  - `setWCL_SET_ROAM_LOCK(...)` remains the reference-backed system-contract fix for WCL selector `0x1ac`: non-NULL payload byte `0` is consumed as `roam_off`, NULL returns raw `0x16`, and no link/key/data side effect is added.
  - `airportItlwmEthernetBufferIsEapol(...)` only reads an Ethernet header from already-owned memory and has no packet mutation side effect.
  - `skywalkRxInput(...)` now logs bounded `ITLWM_EAPOL path=rx ... stage=<terminal>` markers for EAPOL RX block/drop/success outcomes. It does not change allocation, preparation, copy, enqueue, mbuf free, return values, or queue ownership.
  - `skywalkTxAction(...)` now logs bounded `ITLWM_EAPOL path=tx ... stage=mbuf-alloc|copyback|output` markers for EAPOL TX packets that have already exposed valid packet data. It records regdiag TX drops only for pre-output EAPOL drops when data diagnostics are enabled.
  - Existing `outputPacket(...)` EAPOL and `setCIPHER_KEY(...)` logs remain the downstream markers; no forced key or RSN state was added.
- structural verification:
  - `git diff --check`: passed.
  - `bash -n scripts/build_tahoe.sh`: passed.
  - Tahoe build/install/runtime not performed: Stage 1 approval is required first.
- rejected-in-batch verification:
  - `WCL_REASSOC` was not patched: recovered reference `sendReassocCommand(...)` has an associated-state guard and current failure is after AP deauth.
  - `getWCL_BSS_INFO` was not patched: current runtime reaches WCL join-done success and `airportd` association; observed getter failures are downstream after loss of association.
  - `CIPHER_KEY` routing was not patched: current runtime shows no CIPHER_KEY producer attempt to route.
- runtime-before evidence:
  - `/Users/bob/Projects/itlwm/commit-approval/runtime_evidence/CR-111-control_sta_network-current-focused-20260426-1814.log`
  - sha256 `c3bf2dc04ba2c7aa593cc1e723b707e674a740878ac012c91677cf7b1df21889`
  - this log proves successful WCL join-done and UI association, four `WCL_SET_ROAM_LOCK -> 0xe00002c7` failures, no `setCIPHER_KEY`, no EAPOL markers, `IO80211RSNDone set=<0>`, and AP deauth reason 15.
- expected after-fix runtime evidence:
  - no `APPLE80211_IOC_WCL_SET_ROAM_LOCK ... 0xe00002c7`.
  - bounded `WCL [591] ... roam_off=<0|1>` lifecycle logs.
  - if association still fails, bounded `ITLWM_EAPOL path=rx|tx ...` markers must identify whether the next blocker is RX absence/drop, TX absence/drop, missing `setCIPHER_KEY`, or later DHCP/data.
- notes:
  - `CR-112` supersedes the narrower `CR-111` request because it keeps the exact WCL roam-lock contract fix and adds only behavior-neutral post-join telemetry needed to avoid another one-fix/one-reboot cycle.

## ANOMALY

- id: A-SKYWALK-INFRA-REGISTRATION-DIRECT-039
- status: CONFIRMED_DEVIATION
- symptom: after CR-112, UI-visible association reaches WCL join-done and `airportd` reports success, but RSN/EAPOL does not complete; inbound EAPOL reaches local `skywalkRxInput(...)` and `fRxQueue->enqueuePackets(...)` returns success, while no outbound EAPOL TX and no `setCIPHER_KEY(...)` producer are observed.
- first visible manifestation: `2026-04-26 19:22:10..19:32:54 +0300` in `CR-113-focused-live-log-20260426-194658.log`.
- expected system behavior: the Apple Wi-Fi reference starts its Skywalk interface through the infra registration shim `IO80211InfraInterface::registerInfraEthernetInterface(...)`, passing the registration info, queue array, queue count, TX pool, and RX pool.
- actual behavior: local Tahoe start path calls `IOSkywalkEthernetInterface::registerEthernetInterface(...)` directly from `AirportItlwm::start(...)`.
- divergence point: `AirportItlwm/AirportItlwmV2.cpp` STEP 8d registration call.
- evidence:
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-113-focused-live-log-20260426-194658.log`, sha256 `4a9c1ac912cc870746e38e5e9df5edd107ecb280b5f17289f3bf73770f39791f`.
    - log proves `registerEthernetInterface=0x0`, WCL join-done success, `airportd` association, `ITLWM_EAPOL path=rx ... stage=enqueue-ok len=113 result=0x0`, no `ITLWM_EAPOL path=tx`, no `setCIPHER_KEY`, and `IO80211RSNDone set=<0> res=<1>`.
  - regdiag:
    - `commit-approval/runtime_evidence/CR-113-regdiag-snapshot-after-rebuild-20260426.txt`, sha256 `e2180806a7991e97d0bb03f8120f7fab765fad01b8e29a994ddca6f5c30e5a93`.
    - snapshot shows driver objects present, `link=0x1`, `current_ssid=CONTROL_STA_NETWORK`, and live `txQ/rxQ` pointers.
  - decomp:
    - `xcrun nm -m /tmp/itlwm_ref/IO80211Family` exports `IO80211InfraInterface::registerInfraEthernetInterface(RegistrationInfo*, IOSkywalkPacketQueue**, unsigned int, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*)` at `0xffffff80022e6bc4`.
    - `xcrun nm -m /tmp/itlwm_ref/AppleBCMWLANCoreMac` shows AppleBCMWLAN dynamically imports the same symbol.
    - remote decomp `IO80211Family_decompiled.c:263824..263920` shows `registerInfraEthernetInterface(...)` conditionally handles infra MAC state, then calls the underlying `FUN_ffffff8002a3d994(..., 0)` registration body.
    - reference disassembly of `AppleBCMWLANSkywalkInterface::start(...)` around `0xffffff800155d1de` calls `IO80211InfraInterface::registerInfraEthernetInterface(...)`, not direct `registerEthernetInterface(...)`.
- candidate causes:
  - confirmed: local code bypasses the infra registration shim used by the reference Wi-Fi interface.
  - insufficient data: whether this shim is the full root cause of missing EAPOL TX/key producer.
  - still open: generic `IOSkywalkNetworkPacket` vs reference `IO80211NetworkPacket` subclass may be the next deeper packet-delivery mismatch, but implementing a custom packet class is not safe in this batch without exact local allocation proof.
- rejected causes:
  - call `IO80211InfraInterface::inputPacket(...)` manually from `skywalkRxInput(...)`: rejected because reference calls it with an `IO80211NetworkPacket*`/subclass; local RX allocation currently returns a generic Skywalk packet and a reinterpret-cast would violate ABI/ownership.
  - fabricate EAPOL TX, key install, or RSN done: rejected by protocol and no key material producer evidence.
  - add retry/replay/delay around RX completion: rejected by protocol and no reference producer-side proof.
- confirmed deviation: reference uses `registerInfraEthernetInterface(...)`; local code uses direct `registerEthernetInterface(...)`.
- root cause: not claimed as the sole RSN root cause. This is a confirmed active-layer reference divergence adjacent to the current blocker and safe to remove 1:1; the next runtime will show whether it restores IO80211 packet consumption or exposes the packet-subclass mismatch.
- fix: declare the non-virtual infra registration shim in the local IO80211 header and call it from Tahoe STEP 8d with the same arguments currently passed to direct registration, without changing queues, pools, registration info content, BSD attach ordering, link state, or packet flow.
- verification:
  - source check: STEP 8d log must report `registerInfraEthernetInterface=0x0`.
  - structural check: `git diff --check`.
  - after Stage 1 approval: build/install without unloading, reboot, join `CONTROL_STA_NETWORK`, collect `regdiag snapshot/trace` and sudo logs for infra registration, `ITLWM_IO80211_INPUT`, EAPOL RX/TX, `setCIPHER_KEY`, `IO80211RSNDone`, and deauth/DHCP.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-INFRA-REGISTRATION-DIRECT-039
- symptom: WCL association succeeds and EAPOL RX enqueue succeeds, but the Wi-Fi supplicant/key path does not produce EAPOL TX or `setCIPHER_KEY(...)`.
- expected system behavior: reference Wi-Fi Skywalk start goes through `IO80211InfraInterface::registerInfraEthernetInterface(...)`.
- actual behavior: local Tahoe start directly calls `IOSkywalkEthernetInterface::registerEthernetInterface(...)`.
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp::AirportItlwm::start` STEP 8d.
- evidence from runtime: `CR-113-focused-live-log-20260426-194658.log` proves direct registration success, WCL join success, EAPOL RX enqueue success, and missing EAPOL TX/key producer.
- evidence from decomp: IO80211Family exports `registerInfraEthernetInterface(...)`; AppleBCMWLAN imports and calls it in `AppleBCMWLANSkywalkInterface::start(...)`; IO80211Family decomp shows it wraps the same underlying registration body with infra-specific MAC/log handling.
- exact semantic mismatch between reference and our code: reference enters the Wi-Fi infra registration shim; local code bypasses it and enters the lower Ethernet registration body directly.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: this candidate only claims a confirmed reference deviation in the active packet-delivery layer, not final root cause of RSN. User instruction for the current batch allows confirmed 1:1 divergences adjacent to the blocker to be removed before another reboot.
- why proposed fix is 1:1 with reference architecture and semantics: same method, same argument shape, same queues, same pools, same registration info pointer, and no extra events/retries/replays are added.
- files/functions to modify:
  - `include/Airport/IO80211InfraInterface.h`
  - `AirportItlwm/AirportItlwmV2.cpp::AirportItlwm::start`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - manual `inputPacket(...)` call with a cast packet pointer: rejected as unsafe ABI mismatch.
  - change queue count or add synthetic queues: rejected until the local queue inventory is proven equivalent to reference `numTxQueues + 2`.
  - change packet pool type: rejected for this batch because it requires a larger custom `IO80211NetworkPacket` allocation path.
  - force RSN/key/EAPOL producer state: rejected.
- verification plan:
  - `git diff --check`.
  - create CR-113 patch artifact and Stage 1 request.
  - no build/install/runtime until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## ANOMALY

- id: A-IO80211-INPUT-VISIBILITY-GAP-040
- status: CORRELATED
- symptom: local RX EAPOL is enqueued to the RX completion queue, but current evidence cannot prove whether IO80211 infra input/peer-manager processing receives the packet after enqueue.
- first visible manifestation: `ITLWM_EAPOL path=rx ... stage=enqueue-ok` without any downstream EAPOL TX/key evidence in `CR-113-focused-live-log-20260426-194658.log`.
- expected system behavior: reference `IO80211InfraInterface::inputPacket(...)` logs RX completion through the interface monitor and then delegates to `IO80211PeerManager::skywalkInputPacket(...)` with the packet, peer, tag, and Ethernet header.
- actual behavior: local code has no marker at the virtual `inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool)` boundary, so the next missing edge cannot be distinguished in one reboot.
- divergence point: observability gap at `AirportItlwmSkywalkInterface` inherited `inputPacket(...)` slot.
- evidence:
  - runtime logs: RX EAPOL enqueue succeeds but no EAPOL TX/key producer is observed.
  - decomp: `IO80211InfraInterface::inputPacket(...)` at `0xffffff80022e3f20` calls monitor logging, resolves peer manager from infra state, fills tag ethertype from packet data when available, and calls `IO80211PeerManager::skywalkInputPacket(...)` at `0xffffff80021dd7b4`.
- candidate causes:
  - RX completion may not invoke `inputPacket(...)` at all.
  - RX completion may invoke `inputPacket(...)` with non-EAPOL/invalid header metadata.
  - RX completion may invoke `inputPacket(...)` successfully, making the next blocker inside peer manager/supplicant/key producer.
- rejected causes:
  - changing payload/ownership at this boundary: rejected; diagnostic must be pure pass-through.
  - forcing `inputPacket(...)` from local RX path: rejected because packet class compatibility is not proven.
- confirmed deviation: none; this is a diagnostic visibility gap.
- root cause: not claimed.
- fix: add a Tahoe-only override of `AirportItlwmSkywalkInterface::inputPacket(...)` that records a bounded regdiag/log marker, calls `IO80211InfraInterface::inputPacket(...)` with unchanged arguments, records the return, and returns it unchanged.
- verification:
  - source check: override does not mutate packet/tag/header/flags or alter return semantics.
  - after Stage 1 approval runtime: if `ITLWM_IO80211_INPUT` appears for EAPOL and returns success but TX/key still absent, the next blocker is downstream of IO80211 input; if it never appears, the blocker remains RX completion packet/queue class delivery.

## FIX_CANDIDATE

- anomaly_id: A-IO80211-INPUT-VISIBILITY-GAP-040
- symptom: after RX EAPOL enqueue success, downstream IO80211/supplicant processing is not observable.
- expected system behavior: IO80211 infra input boundary should be distinguishable from RX completion enqueue and key/TX producers.
- actual behavior: current diagnostics bracket enqueue and TX/key only; they do not prove inherited `inputPacket(...)` entry/return.
- exact divergence point: inherited `IO80211InfraInterface::inputPacket(...)` slot in `AirportItlwmSkywalkInterface`.
- evidence from runtime: `CR-113-focused-live-log-20260426-194658.log` has RX enqueue success with no EAPOL TX and no `setCIPHER_KEY`.
- evidence from decomp: IO80211Family decomp of `IO80211InfraInterface::inputPacket(...)` shows this is the exact boundary that delegates to peer manager packet input.
- exact semantic mismatch between reference and our code: not claimed; this is diagnostic instrumentation.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated: RX completion never reaches IO80211 input; IO80211 input receives EAPOL but fails/returns error; IO80211 input succeeds and the next blocker is supplicant/key producer; non-EAPOL metadata reaches input while EAPOL does not.
  - exact probe points: entry and return of `AirportItlwmSkywalkInterface::inputPacket(...)` Tahoe signature.
  - why these probe points are sufficient: they sit immediately downstream of the RX completion queue and immediately upstream of peer manager/supplicant processing recovered in decomp.
  - why instrumentation is behavior-neutral: the override only reads `ether_header::ether_type`, increments bounded diagnostic counters, emits bounded logs/traces, calls the base implementation with identical arguments, and returns the base result unchanged.
  - what exact runtime evidence must be collected: `ITLWM_IO80211_INPUT stage=entry|return type=0x888e eapol=1 result=...`, regdiag trace entries, EAPOL RX/TX markers, `setCIPHER_KEY`, `IO80211RSNDone`, DHCP/IP, and deauth timing.
- why this is root cause and not just correlation: root cause is not claimed; this probe is required to identify the first missing edge after already observed RX enqueue success.
- why proposed fix is 1:1 with reference architecture and semantics: no behavior is changed; the existing reference/base implementation remains the sole packet consumer.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - manual call to base `inputPacket(...)` from `skywalkRxInput(...)`: rejected as an extra callback and unsafe packet class assumption.
  - changing RX queue enqueue semantics: rejected for this diagnostic.
  - widening regdiag schema: rejected; existing trace fields are enough.
- verification plan:
  - `git diff --check`.
  - create CR-113 patch artifact and Stage 1 request.
  - after approval, build/install/reboot and use `airport_itlwm_regdiag get trace` plus sudo logs for one join attempt.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes for the infra registration reference path and for the IO80211 input/peer-manager downstream boundary.
- Есть ли прямое подтверждение по runtime-данным? Yes: CR-113 runtime and regdiag prove association/link/RX enqueue success with missing TX/key/RSN completion.
- Доказал ли я причинность, а не просто корреляцию? For final RSN failure, no; therefore only the confirmed registration deviation is fixed, and the IO80211 input change is explicitly diagnostic.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? The registration fix does; the diagnostic override calls the same base implementation and does not alter semantics.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: no forced association, no forced key, no fake RSN, no replay/retry/delay.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: files, symbol addresses, runtime artifact names, and hashes are listed above.

## ANOMALY

- id: A-SKYWALK-QUEUE-ACCESSORS-INHERITED-041
- status: CONFIRMED_DEVIATION
- symptom: after RX EAPOL is accepted by the local RX completion queue, the Apple supplicant/key path still does not emit EAPOL TX or `setCIPHER_KEY(...)`; static audit of the active Skywalk layer shows the interface-visible queue/pool accessors are still inherited from base stubs.
- first visible manifestation: CR-113 runtime proves `ITLWM_EAPOL path=rx ... stage=enqueue-ok len=113 result=0x0`, no `ITLWM_EAPOL path=tx`, no `setCIPHER_KEY(...)`, and `IO80211RSNDone set=<0> res=<1>`.
- expected system behavior: the Wi-Fi Skywalk interface must expose its registered queue/pool inventory through the IO80211 virtual accessors. Reference AppleBCMWLANSkywalkInterface returns the concrete RX completion queue, TX submission queue selected by WME AC, TX packet pool, RX packet pool, and number of TX queues.
- actual behavior: local `AirportItlwmSkywalkInterface` does not override these slots, so Tahoe `IO80211SkywalkInterface` base stubs return zero for `getRxCompQueue`, `getTxSubQueue`, `getTxPacketPool`, `getRxPacketPool`, and `getNumTxQueues`.
- divergence point: `AirportItlwm/AirportItlwmSkywalkInterface.hpp` lacks queue/pool accessor overrides; `AirportItlwm/AirportItlwmSkywalkInterface.cpp` lacks implementations that return the already-created `AirportItlwm::fTxQueue`, `fRxQueue`, `fTxPool`, and `fRxPool`.
- evidence:
  - runtime evidence: `commit-approval/runtime_evidence/CR-113-focused-live-log-20260426-194658.log`, sha256 `4a9c1ac912cc870746e38e5e9df5edd107ecb280b5f17289f3bf73770f39791f`, shows WCL join success, RX EAPOL enqueue success, no EAPOL TX/key producer.
  - regdiag evidence: `commit-approval/runtime_evidence/CR-113-regdiag-snapshot-after-rebuild-20260426.txt`, sha256 `e2180806a7991e97d0bb03f8120f7fab765fad01b8e29a994ddca6f5c30e5a93`, shows live `txQ` and `rxQ` pointers while the interface accessors remain inherited.
  - local static evidence: `rg` finds `getRxCompQueue/getTxSubQueue/getTxPacketPool/getRxPacketPool/getNumTxQueues` declarations only in `include/Airport/IO80211SkywalkInterface.h`, not in `AirportItlwmSkywalkInterface`.
  - IO80211Family decomp: base `IO80211SkywalkInterface` functions at `0xffffff800227857e`, `0xffffff800227858e`, `0xffffff8002278596`, `0xffffff800227859e`, and `0xffffff80022785b6` return `0`.
  - AppleBCMWLAN decomp:
    - `AppleBCMWLANSkywalkInterface::getRxCompQueue()` at `0xffffff800155fb36` returns ivars `+0x68`.
    - `AppleBCMWLANSkywalkInterface::getTxSubQueue(apple80211_wme_ac)` at `0xffffff800155fb5a` maps AC through ivars `+0x3c` and returns queue vector `+0x78 + queueId*8`.
    - `AppleBCMWLANSkywalkInterface::getTxSubQueue(unsigned int)` at `0xffffff800155fb7e` bounds-checks against `numTxQueues` and returns queue vector `+0x78 + index*8`.
    - `AppleBCMWLANSkywalkInterface::getTxPacketPool()` at `0xffffff800155fbb2` returns ivars `+0x50`.
    - `AppleBCMWLANSkywalkInterface::getRxPacketPool()` at `0xffffff800155fbc4` returns ivars `+0x58`.
    - `AppleBCMWLANSkywalkInterface::getNumTxQueues()` at `0xffffff800155fb24` returns ivars byte `+0x2a`.
- candidate causes:
  - confirmed: the local interface advertises no TX/RX queue/pool inventory through IO80211 accessors even though the controller has already created and registered those objects.
  - insufficient data: whether this accessor gap is the full root cause of missing supplicant TX/key production or one prerequisite alongside packet subclass/TxCompletionQueue work.
  - still open: reference has a separate TX completion queue and multicast queue; local has no proven 1:1 objects for those slots in this batch.
- rejected causes:
  - return `fTxQueue` from `getTxCompQueue()`: rejected because reference `getTxCompQueue()` returns a distinct TX completion queue at ivars `+0x60`, not the TX submission queue.
  - fabricate a TX completion queue in this batch: rejected because AppleBCM uses a custom `AppleBCMWLANSkywalkTxCompletionQueue` subclass and exact local completion semantics are not proven.
  - implement `pendingPackets()` / `packetSpace()` with guessed constants: rejected; reference calls queue methods and local public header/mangling for queue count/space still needs separate proof.
  - force EAPOL TX/key/RSN state: rejected by protocol.
- confirmed deviation: base IO80211 accessors return zero; reference Apple Wi-Fi accessors return concrete queue/pool inventory.
- root cause: not claimed for final RSN failure. This is a confirmed active-layer reference deviation adjacent to the current blocker and safe to remove for the queue/pool objects that already exist locally.
- fix: implement only the 1:1-safe accessor subset: `getRxCompQueue()`, `getTxSubQueue(apple80211_wme_ac)`, `getTxPacketPool()`, `getRxPacketPool()`, and `getNumTxQueues()`. Leave `getTxCompQueue()`, `getMultiCastQueue()`, depth/capacity, `pendingPackets()`, `packetSpace()`, and datapath methods unchanged until exact local objects/semantics are proven.
- implementation ABI note: local headers only forward-declare `apple80211_wme_ac`, but the recovered AppleBCM `getTxSubQueue(apple80211_wme_ac)` body receives it as a 32-bit value (`uint param_2`) and uses it as an AC index. Completing the local forward declaration as a one-`UInt32` POD carrier is required for an out-of-line override and preserves the recovered 4-byte calling convention.
- verification:
  - source check: accessors return only existing object pointers from `AirportItlwm` and do not allocate, retain/release, enable/disable, enqueue, retry, replay, or mutate packet/link/key state.
  - structural check: `git diff --check`.
  - after Stage 1 approval: build/install without unloading, reboot, join `CONTROL_STA_NETWORK`, and collect logs/regdiag for accessor-enabled queue inventory, IO80211 input, EAPOL RX/TX, `setCIPHER_KEY`, `IO80211RSNDone`, and deauth/DHCP.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-QUEUE-ACCESSORS-INHERITED-041
- symptom: WCL association succeeds and RX EAPOL enqueue succeeds, but the system still does not produce EAPOL TX/key installation; active-layer static audit finds inherited zero queue/pool accessors.
- expected system behavior: IO80211 consumers querying a Wi-Fi Skywalk interface receive the same queue/pool inventory that was registered with the logical link.
- actual behavior: local class inherits base stubs that return zero.
- exact divergence point: `AirportItlwmSkywalkInterface` vtable slots for RX completion queue, TX subqueue, TX pool, RX pool, and TX queue count.
- evidence from runtime: CR-113 focused log and regdiag show live queues/pools and RX enqueue success but no downstream TX/key producer.
- evidence from decomp: AppleBCMWLAN returns ivar-backed queue/pool pointers; IO80211SkywalkInterface base stubs return zero for the same slots.
- exact semantic mismatch between reference and our code: reference exposes the active queue/pool inventory through interface virtuals; local exposes no inventory through those virtuals.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: final RSN root cause is not claimed; the queue/pool accessor mismatch is a confirmed reference deviation in the currently active packet-delivery layer and removing it is necessary before a meaningful after-fix runtime can classify the remaining blocker.
- why proposed fix is 1:1 with reference architecture and semantics: the patch returns the concrete local equivalents of reference ivars `+0x68`, `+0x78[0]`, `+0x50`, `+0x58`, and `numTxQueues=1`; it does not invent missing distinct queues.
- files/functions to modify:
  - `include/Airport/IO80211SkywalkInterface.h`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - map TX completion to TX submission: rejected as a class/semantic mismatch.
  - add base `IOSkywalkTxCompletionQueue` now: rejected because reference uses a custom Apple queue subclass and exact completion ownership is not proven.
  - add pending/space/depth/capacity guessed values: rejected as non-reference heuristics.
  - force `inputPacket`, EAPOL, key, RSN, retry, delay, or replay: rejected.
- verification plan:
  - `git diff --check`
  - `bash -n scripts/build_tahoe.sh`
  - regenerate the superseding batch diff/request.
  - do not build/install/runtime-test until reviewer returns `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: AppleBCMWLAN queue/pool accessors return concrete ivars while IO80211SkywalkInterface base accessors return zero.
- Есть ли прямое подтверждение по runtime-данным? Yes: CR-113 runtime shows the local queues exist and accept RX EAPOL while downstream TX/key production is absent.
- Доказал ли я причинность, а не просто корреляцию? For full RSN, no; therefore this is a reference-alignment prerequisite in the active layer, not a final root-cause claim.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? For the included subset, yes: existing local TX submission/RX completion queues and TX/RX pools are exposed through the same accessor slots.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: no fake TX, no fake key, no fake RSN; unproven TX completion/multicast/space methods remain explicitly out of scope.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: symbol addresses, local grep result, runtime artifact names, and hashes are recorded above.

## ANOMALY

- id: A-SKYWALK-INVENTORY-LIFECYCLE-PARTIAL-042
- status: CONFIRMED_DEVIATION
- symptom: после успешной ассоциации и RX EAPOL enqueue система не доходит до устойчивого RSN/data path; предыдущая статическая ревизия оставила часть активного Skywalk inventory/lifecycle слоя на inherited/base stubs.
- first visible manifestation: CR-113/CR-114 runtime shows WCL association/link and `ITLWM_EAPOL path=rx ... stage=enqueue-ok`, but no stable EAPOL TX/key/data path completion.
- expected system behavior: reference `AppleBCMWLANSkywalkInterface` exposes the full local Skywalk queue inventory and lifecycle surface used by IO80211: TX submission queue vector, RX completion queue, distinct TX completion queue, multicast work source, TX/RX pools, TX queue count, queue depth/capacity/space, and datapath enable/disable.
- actual behavior: local `AirportItlwmSkywalkInterface` only exposes the CR-114 accessor subset and still inherits zero/no-op behavior for TX completion queue, multicast queue, depth/capacity/space, `pendingPackets`, `packetSpace`, and datapath lifecycle methods; `AirportItlwm` creates only TX submission and RX completion queues.
- divergence point: `AirportItlwmV2` Skywalk object inventory lacks `IOSkywalkTxCompletionQueue` and multicast event source; `AirportItlwmSkywalkInterface` lacks the corresponding accessor/lifecycle overrides.
- evidence:
  - AppleBCMWLAN decomp: `AppleBCMWLANSkywalkInterface::getTxCompQueue()` returns ivars `+0x60`; `getRxCompQueue()` returns `+0x68`; TX subqueues live at `+0x78 + index*8`; `getMultiCastQueue()` returns `+0x98`; pools are `+0x50/+0x58`; `getNumTxQueues()` returns byte `+0x2a`.
  - AppleBCMWLAN decomp: `enableDatapath()` enables TX completion queue and RX completion queue, requests RX enqueue, and enters the infra datapath enable edge; `disableDatapath()` disables TX subqueues, multicast queue, RX completion queue, and TX completion queue.
  - AppleBCMWLAN decomp: `AppleBCMWLANSkywalkTxCompletionQueue` is a distinct `IOSkywalkTxCompletionQueue` subclass; `AppleBCMWLANSkywalkMulticastQueue` is an `IO80211WorkSource` event source with no-op dequeue/stat methods and retained interface pointer.
  - local SDK headers: `IOSkywalkTxCompletionQueue::withPool(...)`, `enable()`, `disable()`, `requestEnqueue(...)`, `getPacketCount()`, and `IOSkywalkPacketQueue::getCapacity()/getFreeSpace()/getWorkLoop()` provide the local system contract for the missing packet queue object.
  - local static evidence: `AirportItlwmV2.hpp` currently has `fTxQueue` and `fRxQueue` only; `AirportItlwmSkywalkInterface.hpp/.cpp` currently override only `getRxCompQueue`, `getTxSubQueue`, pools, and `getNumTxQueues`.
- candidate causes:
  - confirmed: the local Wi-Fi Skywalk interface still reports an incomplete queue/lifecycle inventory compared with the reference active layer.
  - confirmed: IO80211 can see no TX completion queue or multicast work source through the virtual accessors, even though reference exposes both.
  - still open: whether this is the final RSN/data root cause or one required prerequisite before the next runtime boundary can be isolated.
- rejected causes:
  - alias `getTxCompQueue()` to the TX submission queue: rejected; reference uses a distinct TX completion queue object.
  - register multicast queue as a packet queue: rejected; reference multicast object is a work source/event source, not a packet queue in the logical-link queue array.
  - invent multiple TX queues or AC-specific queue depths: rejected; local has one TX submission queue, so all valid ACs map to queue 0.
  - force EAPOL/key/RSN/link/data success: rejected by protocol.
- confirmed deviation: reference has distinct TX completion/multicast objects and lifecycle accessors; local does not.
- root cause: not claimed as final RSN root cause; this is a confirmed active-layer reference deviation that must be restored before runtime evidence can be interpreted as a higher-layer failure.
- fix: restore the local one-queue equivalent of the reference layer: create a distinct `IOSkywalkTxCompletionQueue`, create a multicast `IOEventSource` work source, attach/remove both on `_fWorkloop`, expose `getTxCompQueue()`/`getMultiCastQueue()`, implement depth/capacity/space/pending accessors from the concrete queue methods, and implement datapath enable/disable without changing packet/key/link success semantics.
- verification:
  - static source check that new accessors return concrete objects only and do not fake RSN/key/data state.
  - `git diff --check`.
  - create superseding batch request/artifact; build/install/reboot only after Stage 1 approval.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-INVENTORY-LIFECYCLE-PARTIAL-042
- symptom: association/RX EAPOL reaches the local RX queue but stable RSN/data path is not reached; static audit shows missing reference Skywalk inventory/lifecycle objects and accessors.
- expected system behavior: the IO80211-facing Skywalk interface exposes the same complete queue/work-source inventory and datapath lifecycle surface as AppleBCMWLAN.
- actual behavior: local class exposes only the CR-114 queue/pool subset and lacks TX completion queue, multicast work source, depth/capacity/space, pending/space, and datapath lifecycle overrides.
- exact divergence point: `AirportItlwm::start()` Skywalk object creation/workloop attachment and `AirportItlwmSkywalkInterface` virtual slots `[367]`, `[368]`, `[425]`, `[426]`, `[428]`, `[433]`, `[437]`, `[438]`.
- evidence from runtime: CR-113/CR-114 logs prove live local TX/RX queues and RX EAPOL enqueue success, followed by missing EAPOL TX/key/RSN completion.
- evidence from decomp: AppleBCMWLAN returns distinct ivar-backed TX completion and multicast objects, implements depth/capacity/space from queue objects, and explicitly enables/disables those queues in datapath lifecycle.
- exact semantic mismatch between reference and our code: reference presents a complete active Skywalk object inventory and lifecycle; local presents an incomplete inventory and inherits zero/no-op base behavior for several system-facing slots.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: final RSN/data root cause is not claimed; the missing inventory/lifecycle surface is a confirmed reference deviation on the active path, and restoring it is required before attributing the remaining blocker to IO80211, firmware, key, or DHCP layers.
- why proposed fix is 1:1 with reference architecture and semantics: the patch adds the same object categories and accessors as reference while preserving the local one-TX-queue topology; it returns real local queue/pool/work-source objects and uses public queue methods for counts/capacity/space instead of constants or forced state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - fake TX completion by returning TX submission queue.
  - add guessed AC queue vector or multiple queues not present locally.
  - register multicast as a packet queue.
  - force/replay EAPOL, key install, RSN done, link up, DHCP, retry, delay, or poll.
- verification plan:
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh` if present.
  - generate superseding CR request and diff artifact.
  - no kext build/install/runtime until reviewer grants `APPROVED_FOR_AFTER_FIX_RUNTIME`.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: AppleBCMWLAN exposes and lifecycles TX completion, RX completion, TX subqueue vector, multicast work source, pools, queue count, depth/capacity, and datapath methods.
- Есть ли прямое подтверждение по runtime-данным? Yes for the active symptom boundary: local queues exist and RX EAPOL is enqueued, while downstream TX/key/RSN does not complete.
- Доказал ли я причинность, а не просто корреляцию? For final RSN/data failure, no; therefore this is recorded as a confirmed reference-alignment prerequisite, not final root cause.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes within local one-queue topology: distinct TX completion queue, multicast event source, concrete queue accessors, and datapath enable/disable surface are restored.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No fake key/link/RSN/data success is introduced.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: AppleBCMWLAN ivar offsets and local file/function divergences are recorded above.

## ANOMALY

- id: A-SKYWALK-QUEUE-ABI-SYMBOL-SURFACE-043
- status: CONFIRMED_DEVIATION
- symptom: CR-115 correctly identified the missing active Skywalk queue/lifecycle layer but failed Stage 1 structural review because the built kext referenced two symbols not exported by the Tahoe BootKC symbol surface.
- first visible manifestation: `commit-approval/decisions/COMMIT_DECISION_CR-115.md` reports `status: REJECTED`, `allow_after_fix_runtime: NO`, and BootKC unresolved symbols for `IOSkywalkPacketQueue::getCapacity()` and `IOSkywalkTxCompletionQueue::withPool(...)` with an `int`/`IOReturn` callback signature.
- expected system behavior: local Tahoe source and build headers must bind only to exported BootKC symbols or use local state for driver-owned constants; queue/lifecycle restoration must not introduce unresolved kernel imports.
- actual behavior: local `IOSkywalkTxCompletionQueue.h` declares `IOSkywalkTxCompletionQueueAction` as `IOReturn`, producing a `PFi...` mangled `withPool(...)` import, while BootKC exports the `PFj...`/`UInt32` callback form. Local CR-115 code also called non-const `IOSkywalkPacketQueue::getCapacity()`, while BootKC exports the const form and the local queue capacity is already driver-owned construction state.
- divergence point:
  - `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkTxCompletionQueue.h` and `scripts/build_tahoe.sh` do not patch the TX completion callback ABI to Tahoe's exported `UInt32` form.
  - `AirportItlwmSkywalkInterface::getTxQueueDepth()` and `getRxQueueCapacity()` call `getCapacity()` directly instead of returning local queue construction metadata.
  - `pendingPackets()` / `packetSpace()` used generic local queue methods even though Apple custom TX submission queue methods at vtable `+0x348/+0x340` return `0`.
- evidence:
  - CR-115 reviewer evidence: unresolved `__ZN20IOSkywalkPacketQueue11getCapacityEv` and `__ZN26IOSkywalkTxCompletionQueue8withPool...PFi...`.
  - local BootKC export scan: Tahoe exports `__ZN26IOSkywalkTxCompletionQueue8withPool...PFj...`, `__ZN26IOSkywalkTxCompletionQueue12initWithPool...PFj...`, `__ZNK20IOSkywalkPacketQueue11getCapacityEv`, and `__ZNK20IOSkywalkPacketQueue12getFreeSpaceEv`.
  - AppleBCMWLAN decomp: `AppleBCMWLANSkywalkTxSubmissionQueue::getQueueDepth()` and `getCapacity()` return the custom queue ivar `+0x28`; `getRingFreeSpace()` and `getPendingPacketCount()` return `0`.
  - AppleBCMWLAN decomp: `AppleBCMWLANSkywalkInterface::packetSpace(queue)` calls TX queue vtable `+0x340`; `pendingPackets(queue)` calls TX queue vtable `+0x348`; `getTxQueueDepth()` reads TX queue custom ivar `+0x28`; `getRxQueueCapacity()` reads RX completion custom ivar `+0x10`.
- candidate causes:
  - confirmed: the CR-115 source/header ABI used unexported manglings for TX completion queue factory and packet queue capacity.
  - confirmed: the local code used generic queue methods where the Apple custom queue layer uses local driver-owned ivars or no-op methods.
  - still open: whether the restored active Skywalk layer is sufficient for RSN/data completion; runtime remains blocked until Stage 1 approval.
- rejected causes:
  - remove TX completion queue entirely: rejected because BootKC exports the correct `UInt32` ABI and Apple reference exposes a distinct TX completion queue.
  - call `IOSkywalkPacketQueue::getCapacity()` by forcing a non-const declaration: rejected by BootKC symbol verification.
  - keep `packetSpace()` as generic free-space calculation: rejected because Apple custom `getRingFreeSpace()` returns `0` in the recovered decomp.
  - install/runtime-test despite Stage 1 rejection: rejected by protocol and reviewer decision.
- confirmed deviation: CR-115's queue restoration was structurally incomplete at the ABI/export layer.
- root cause: for CR-115 rejection, confirmed: wrong local header ABI and direct use of a non-exported capacity symbol. For RSN/data failure, not claimed.
- fix: patch `scripts/build_tahoe.sh` to align `IOSkywalkTxCompletionQueueAction` to `UInt32`, change the local TX completion callback to `UInt32`, stop calling `getCapacity()`/`getFreeSpace()` from the interface accessors, store driver-owned TX/RX queue sizes from the exact queue construction capacity, and return `0` from `pendingPackets()`/`packetSpace()` to match Apple custom queue methods.
- verification:
  - update docs with the CR-115 rejection and BootKC ABI findings.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` with BootKC verification, saving build evidence.
  - create fresh CR-116 artifact/request; do not install or runtime-test until Stage 1 approval.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-QUEUE-ABI-SYMBOL-SURFACE-043
- symptom: CR-115 active Skywalk layer restoration cannot pass structural review because the kext imports unexported Tahoe symbols.
- expected system behavior: all queue/lifecycle restoration code must bind to exported Tahoe BootKC symbols and local driver-owned state.
- actual behavior: TX completion queue factory uses the wrong callback ABI and queue capacity accessors import the wrong `getCapacity()` mangling.
- exact divergence point: `IOSkywalkTxCompletionQueueAction` callback type, `skywalkTxCompletionAction(...)` return type, direct `getCapacity()`/`getFreeSpace()` calls in `AirportItlwmSkywalkInterface`, and missing local queue-size fields in `AirportItlwm`.
- evidence from runtime: no new runtime allowed; CR-113/CR-114 remain before-fix symptom evidence, and CR-115 reviewer build evidence is the relevant structural failure evidence.
- evidence from decomp: Apple custom TX queue stores capacity/depth in local ivar `+0x28`, RX completion stores capacity in local ivar `+0x10`, and Apple custom pending/space methods return `0`.
- exact semantic mismatch between reference and our code: reference returns driver-owned queue metadata/custom no-op queue methods; local CR-115 called generic queue methods and imported a factory with the wrong callback ABI.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for CR-115 rejection, the unresolved symbols are the exact structural root cause. For RSN/data, root cause is not claimed; this fix only makes the restored layer structurally loadable and reference-aligned.
- why proposed fix is 1:1 with reference architecture and semantics: distinct TX completion queue is retained using the exported Tahoe `UInt32` callback ABI; depth/capacity come from driver-owned queue construction metadata like Apple custom ivars; pending/space return `0` like Apple custom TX queue methods.
- files/functions to modify:
  - `scripts/build_tahoe.sh`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `docs/tahoe_discrepancy_inventory.md`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
- forbidden alternative fixes considered and rejected:
  - dropping TX completion queue instead of fixing its ABI.
  - using non-exported/non-const `getCapacity()`.
  - replacing packetSpace with guessed free-space arithmetic.
  - installing or collecting after-fix runtime without Stage 1 approval.
- verification plan:
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` and archive output as CR-116 structural evidence.
  - regenerate artifact and request.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: Apple custom queue methods/ivars and interface callers are recovered.
- Есть ли прямое подтверждение по runtime-данным? For this structural failure, yes: CR-115 reviewer build evidence names exact unresolved imports.
- Доказал ли я причинность, а не просто корреляцию? Yes for the CR-115 rejection; no claim is made for final RSN/data root cause.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes for the restored layer within local one-queue topology and exported Tahoe ABI.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: no fake key/link/RSN/data state.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: CR-115 decision, BootKC nm exports, and AppleBCMWLAN decomp offsets are recorded above.

## ANOMALY

- id: A-APMODE-STA-FIXEDFAIL-SURFACE-044
- status: CONFIRMED_DEVIATION
- symptom: the AP/SoftAP layer was not explicitly documented after the last YAML update, and the Tahoe BSD bridge did not expose an explicit `APPLE80211_IOC_AP_MODE` fixed-fail route for the primary STA interface.
- first visible manifestation: static audit after CR-116 while restoring the next layer with special AP-mode coverage.
- expected system behavior: on the normal primary STA interface, `AppleBCMWLANCore::setAP_MODE(apple80211_apmode_data*)` returns fixed `0xe00002c7`; the normal path does not create APSTA state, does not advertise HostAP capability, and does not mutate public AP mode state.
- actual behavior: the local `processApple80211Ioctl(...)` bridge had no explicit `APPLE80211_IOC_AP_MODE` case, and local `setAP_MODE(...)` cached `data+4` before returning the same fixed fail code. That local cache is not present in the normal Apple path and could become a false AP state carrier if the public ioctl reaches the local handler.
- divergence point:
  - `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)` lacks an explicit `APPLE80211_IOC_AP_MODE` setter route even though the Tahoe bridge explicitly routes adjacent public setters.
  - `AirportItlwmSkywalkInterface::setAP_MODE(...)` writes `cachedApMode` despite reference normal-path semantics being fixed fail without state mutation.
- evidence:
  - decomp: remote `/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c`, `AppleBCMWLANCore::setAP_MODE(...) @ 0xffffff80016034dc`, initializes return value to `0xe00002c7`; only under verbose-debug plus feature gate `0x3f` does it clear the return and update bit 10 at core expansion `+0x288c`.
  - decomp: APSTA/SoftAP methods are on `AppleBCMWLANIO80211APSTAInterface`, not the primary STA core path. Recovered methods include `setHOST_AP_MODE(...)`, `getHOST_AP_MODE_HIDDEN(...)`, `getSOFTAP_PARAMS(...)`, `getSOFTAP_STATS(...)`, and `setSOFTAP_WIFI_NETWORK_INFO_IE(...)`.
  - local code: HostAP capability publication remains commented out in the STA capability path, which is consistent with not advertising AP support on the primary STA interface.
  - local code: `cachedApMode` is written only by `setAP_MODE(...)` and is otherwise unused, so removing it cannot remove a legitimate local producer.
- candidate causes:
  - confirmed: the public AP mode bridge route and handler side-effect are not aligned with the recovered primary STA contract.
  - rejected: enabling HostAP capability or creating APSTA/SoftAP state in this batch; the APSTA reference layer is a separate interface class and requires AP owner state not present on the local primary STA.
  - rejected: implementing APSTA SoftAP selectors on the primary STA bridge; decomp places them on APSTA and their private owner offsets are not present in the local topology.
- rejected causes:
  - treat AP_MODE as a success carrier: rejected because the normal reference path returns `0xe00002c7`.
  - cache the requested AP mode while returning failure: rejected because the normal reference path has no public state mutation.
  - advertise `APPLE80211_C_FLAG_HOST_AP` to force AP workflows: rejected because that would invite APSTA/SoftAP requests without the required APSTA owner layer.
- confirmed deviation: the local AP_MODE public surface was not an explicit fixed-fail/no-state STA contract.
- root cause: for AP layer restoration, confirmed static deviation. No claim is made that this is the current RSN/data root cause.
- fix: add an explicit `APPLE80211_IOC_AP_MODE` setter route to the Tahoe BSD bridge, make `setAP_MODE(...)` a pure fixed-fail/no-state path, remove the unused `cachedApMode` carrier, and document APSTA/SoftAP as a separate not-yet-restored interface layer.
- verification:
  - update YAML docs with post-Apr-14 findings and AP/APSTA contract.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` with BootKC verification.
  - create a fresh CR-117 batch request superseding CR-116; do not install or runtime-test until Stage 1 approval.

## FIX_CANDIDATE

- anomaly_id: A-APMODE-STA-FIXEDFAIL-SURFACE-044
- symptom: AP mode public surface is not restored/documented as an explicit primary-STA fixed-fail contract.
- expected system behavior: `APPLE80211_IOC_AP_MODE` on the primary STA surface reaches `setAP_MODE(...)` and returns fixed `0xe00002c7` without AP state side effects unless the hidden verbose-debug/feature-gated Apple path is active.
- actual behavior: local bridge falls through to unsupported unless super handles it, while the local handler would cache the caller carrier before returning failure if reached.
- exact divergence point: missing AP_MODE case in `processApple80211Ioctl(...)`; `cachedApMode` write in `setAP_MODE(...)`.
- evidence from runtime: no new AP runtime is claimed; current runtime focus remains RSN/data after CR-113/CR-114. This is a static layer-restoration fix requested before another runtime cycle.
- evidence from decomp: `AppleBCMWLANCore::setAP_MODE(...) @ 0xffffff80016034dc` returns `0xe00002c7` by default and only updates AP bit state behind verbose-debug plus feature gate `0x3f`; APSTA/SoftAP methods are recovered on `AppleBCMWLANIO80211APSTAInterface`.
- exact semantic mismatch between reference and our code: reference normal STA path has fixed fail/no state mutation; local handler cached AP mode and the bridge did not make the AP fixed-fail route explicit.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for the AP surface layer, the decomp identifies the exact return and side-effect contract; this fix restores that layer only and does not claim RSN/data root cause.
- why proposed fix is 1:1 with reference architecture and semantics: the primary STA handler becomes a pure fixed-fail path; APSTA/SoftAP selectors remain out of the primary STA bridge because reference implements them on a separate APSTA interface class.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/93_tahoe_post_yaml_findings_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/94_ap_mode_and_apsta_surface_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
- forbidden alternative fixes considered and rejected:
  - enabling HostAP capability on the STA interface.
  - implementing APSTA SoftAP selectors without APSTA owner state.
  - preserving a cached AP mode while returning failure.
  - using AP mode as a workaround for station join/RSN failures.
- verification plan:
  - `rg cachedApMode` must return no code references.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` and archive structural evidence.
  - regenerate exact artifact and submit CR-117.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: AppleBCMWLANCore primary STA AP_MODE and APSTA/SoftAP methods are recovered from the remote decomp.
- Есть ли прямое подтверждение по runtime-данным? No AP runtime claim is made; this is requested static AP layer restoration before another runtime cycle.
- Доказал ли я причинность, а не просто корреляцию? Yes for the AP surface semantic deviation; no claim is made for the current RSN/data blocker.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes for the primary STA AP_MODE path: fixed fail and no AP state mutation.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: no fake AP, link, key, RSN, DHCP, or data state.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: decomp offsets and local functions are listed above.

## ANOMALY

- id: A-APIE-LIST-ZEROLEN-SUCCESS-045
- status: CONFIRMED_DEVIATION
- symptom: AP-related public GET surface still has a stricter local failure contract than the recovered Apple primary STA path.
- first visible manifestation: static AP-surface audit after CR-117.
- expected system behavior: `AppleBCMWLANCore::getAP_IE_LIST(apple80211_ap_ie_data*)` initializes the output length to zero, delegates to `IO80211BssManager` to copy AP IE bytes into `data+8`, and writes the resulting length back to `data+4`. The visible AppleBCMWLAN wrapper does not reject a missing current BSS or an empty AP IE list before writing zero length.
- actual behavior: local `AirportItlwmSkywalkInterface::getAP_IE_LIST(...)` returns `kIOReturnError` when there is no current BSS, no `ni_rsnie_tlv`, zero IE length, `ni_rsnie_tlv_len > data->len`, or length over 1024. On Tahoe the public carrier embeds a 1024-byte IE array, so treating incoming `data->len` as required capacity is not the recovered Apple wrapper contract.
- divergence point:
  - local code checks `data->len` as input capacity and returns error for empty/no-BSS cases.
  - reference wrapper initializes local length to zero and writes the final length, making empty/no-current-BSS a zero-length publication rather than a local AP_IE_LIST error at the wrapper layer.
- evidence:
  - decomp: `AppleBCMWLANCore::getAP_IE_LIST(...) @ 0xffffff80015e5b46` sets local length to `0`, calls `FUN_ffffff800226698a(bssManager, data+8, &local_len)`, then writes `*(uint32_t *)(data+4) = local_len`.
  - decomp: `IO80211BssManager` helper `FUN_ffffff800226698a @ 0xffffff800226698a` returns `0xe0822403` if no current BSS and otherwise calls the node method at vtable `+0x1b0`; the AppleBCMWLAN wrapper does not propagate that helper return in the recovered code.
  - local struct: Tahoe `apple80211_ap_ie_data` embeds `ie_data[APPLE80211_NETWORK_DATA_MAX_IE_LEN]`, so the output buffer is fixed at 1024 bytes.
  - local code: `getAP_IE_LIST(...)` returns error on no BSS/no IE/zero length and on `ni_rsnie_tlv_len > data->len`.
- candidate causes:
  - confirmed: local AP_IE_LIST wrapper has stricter error gates than the recovered Apple wrapper.
  - rejected: fabricate AP IE bytes when none exist; reference publishes the helper-produced length and initializes it to zero.
  - rejected: copy beyond `APPLE80211_NETWORK_DATA_MAX_IE_LEN`; local hard maximum remains necessary because the Tahoe public carrier has a fixed array.
- rejected causes:
  - changing RSN_IE programming: AP_IE_LIST is read-only publication and must not mutate RSN state.
  - using caller-provided `data->len` as input capacity on Tahoe: rejected because the public struct has an embedded fixed buffer and Apple wrapper writes `len` as output.
  - treating this as final RSN/data root cause: rejected; this restores an AP-related public surface only.
- confirmed deviation: local AP_IE_LIST does not match Apple zero-length publication semantics for no/empty IE cases.
- root cause: confirmed for the AP_IE_LIST surface layer; no claim is made for final RSN/data.
- fix: set `version`, initialize `len` to zero, return success with zero length when no current AP IE list exists, ignore incoming `data->len` as capacity on Tahoe, and copy at most a valid local IE list no larger than `APPLE80211_NETWORK_DATA_MAX_IE_LEN`.
- verification:
  - update YAML AP surface docs.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` with BootKC verification.
  - create CR-118 superseding CR-117.

## FIX_CANDIDATE

- anomaly_id: A-APIE-LIST-ZEROLEN-SUCCESS-045
- symptom: AP_IE_LIST GET can fail at the local wrapper layer where the Apple primary STA wrapper publishes zero length.
- expected system behavior: wrapper initializes output length to zero, delegates BssManager, writes final length, and does not make the incoming length field a required capacity.
- actual behavior: local wrapper returns error for no BSS/no IE/zero length and if IE length exceeds caller-provided `data->len`.
- exact divergence point: `AirportItlwmSkywalkInterface::getAP_IE_LIST(...)`.
- evidence from runtime: no new runtime is claimed for this layer; this is static AP-surface restoration before the next runtime cycle.
- evidence from decomp: `AppleBCMWLANCore::getAP_IE_LIST(...) @ 0xffffff80015e5b46` and `IO80211BssManager` helper `FUN_ffffff800226698a @ 0xffffff800226698a`.
- exact semantic mismatch between reference and our code: reference publishes output length with zero as the initialized empty result; local code uses missing IE as an error and treats output length as input capacity.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for AP_IE_LIST semantics, the wrapper-level decomp and local code directly disagree. This fix restores that surface only.
- why proposed fix is 1:1 with reference architecture and semantics: zero-length publication mirrors the wrapper initialization and final length write; local fixed-array maximum prevents overflow without inventing IE content.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/93_tahoe_post_yaml_findings_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/94_ap_mode_and_apsta_surface_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
- forbidden alternative fixes considered and rejected:
  - fake AP IE content.
  - truncate invalid over-1024 IE silently.
  - mutate RSN or association state from this getter.
  - claim final RSN/data completion.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - regenerate artifact and submit CR-118.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: AppleBCMWLANCore AP_IE_LIST wrapper and IO80211BssManager helper are recovered.
- Есть ли прямое подтверждение по runtime-данным? No AP_IE runtime claim is made; this is a static surface restoration.
- Доказал ли я причинность, а не просто корреляцию? Yes for the AP_IE_LIST surface mismatch; no claim is made for RSN/data.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes at wrapper level: initialize length to zero, publish final length, no fake IE.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: function addresses and local function are listed above.

## ANOMALY

- id: A-LEGACY-APIE-LIST-SHADOW-046
- status: CONFIRMED_DEVIATION
- symptom: after the Tahoe AP_IE_LIST wrapper was restored, the legacy STA dispatcher still carried the old stricter AP_IE_LIST error contract, keeping a shadow mismatch that can regress non-Tahoe or reused IOCTL paths.
- first visible manifestation: post-CR-118 legacy dispatcher audit; `docs/tahoe_discrepancy_inventory.md` explicitly lists legacy `getAP_IE_LIST` as remaining shadow mismatch.
- expected system behavior: AP_IE_LIST wrapper semantics should be consistent across local dispatch planes: initialize output length to zero, return zero-length success when no current AP IE list exists, copy a valid local IE list when present, and reject only invalid arguments or fixed-carrier overflow.
- actual behavior: `AirportItlwm::getAP_IE_LIST(...)` still returns `kIOReturnError` for no current BSS, no IE pointer, zero IE length, and `ni_rsnie_tlv_len > data->len`.
- divergence point: `AirportSTAIOCTL.cpp::getAP_IE_LIST(...)` retained pre-CR-118 semantics after the Tahoe Skywalk implementation was corrected.
- evidence:
  - decomp: same Apple wrapper contract recorded in `A-APIE-LIST-ZEROLEN-SUCCESS-045`.
  - local docs: `docs/tahoe_discrepancy_inventory.md` "Legacy STA dispatcher still carries a shadow mismatch surface" lists `getAP_IE_LIST`.
  - local code: Tahoe `AirportItlwmSkywalkInterface::getAP_IE_LIST(...)` is now restored, while legacy `AirportItlwm::getAP_IE_LIST(...)` still has stricter error gates.
- candidate causes:
  - confirmed: legacy dispatcher drift from the restored AP_IE_LIST wrapper contract.
  - rejected: remove legacy dispatcher handling; non-Tahoe paths still build this code and it documents a shared Apple80211 contract.
  - rejected: leave legacy stricter than Tahoe; this preserves a known regression source in a neighboring dispatcher.
- rejected causes:
  - fabricate AP IE bytes on legacy path.
  - use incoming `data->len` as fixed-array capacity.
  - change RSN_IE/setRSN_IE behavior in the same patch without separate proof.
- confirmed deviation: legacy `getAP_IE_LIST(...)` no longer matches the restored reference wrapper semantics.
- root cause: confirmed for legacy AP_IE_LIST shadow drift; no claim is made for current Tahoe RSN/data.
- fix: apply the same zero-length success and fixed-array overflow semantics to `AirportItlwm::getAP_IE_LIST(...)`.
- verification:
  - update YAML docs.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` with BootKC verification.
  - create CR-119 superseding CR-118.

## FIX_CANDIDATE

- anomaly_id: A-LEGACY-APIE-LIST-SHADOW-046
- symptom: legacy AP_IE_LIST dispatcher remains stricter than the restored Apple wrapper.
- expected system behavior: zero-length success for no AP IE list; copy valid IE list; reject only bad argument or fixed-buffer overflow.
- actual behavior: legacy handler returns error for empty/no-current-BSS cases and treats `data->len` as input capacity.
- exact divergence point: `AirportItlwm::getAP_IE_LIST(...)` in `AirportSTAIOCTL.cpp`.
- evidence from runtime: no new runtime claim; this is static shadow-layer cleanup after CR-118.
- evidence from decomp: AppleBCMWLANCore AP_IE_LIST wrapper and IO80211BssManager helper from `A-APIE-LIST-ZEROLEN-SUCCESS-045`.
- exact semantic mismatch between reference and our code: reference wrapper publishes a length initialized to zero; legacy local code returns an error.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for the legacy shadow layer, the exact local function diverges from the recovered wrapper semantics and from the now-correct Tahoe path.
- why proposed fix is 1:1 with reference architecture and semantics: it mirrors the wrapper length publication without inventing IE content.
- files/functions to modify:
  - `AirportItlwm/AirportSTAIOCTL.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/93_tahoe_post_yaml_findings_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/94_ap_mode_and_apsta_surface_2026_04_26.yaml`
- forbidden alternative fixes considered and rejected:
  - deleting legacy AP_IE_LIST routing.
  - truncating oversized IE silently.
  - fabricating empty RSN IE bytes.
  - changing RSN_IE in the same batch.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - regenerate artifact and submit CR-119.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the AP_IE_LIST wrapper contract is already recovered.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made for this legacy shadow layer.
- Доказал ли я причинность, а не просто корреляцию? Yes for the shadow-layer mismatch; no claim is made for current RSN/data.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes at the wrapper-publication level.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: AP_IE_LIST decomp and legacy local function are recorded above.

## ANOMALY

- id: A-MCS-NRATE-PRODUCER-AND-VIF-SHADOW-047
- status: CONFIRMED_DEVIATION
- symptom: after the AP surface cleanup, two adjacent STA public surfaces still diverge from recovered Tahoe behavior: `getMCS(...)` uses association/node state instead of the Apple cached `nrate` producer, and the legacy dispatcher still collapses Tahoe `setVIRTUAL_IF_CREATE(...)` role failures to generic unsupported.
- first visible manifestation: post-CR-119 static audit of remaining STA shadow layers.
- expected system behavior:
  - `AppleBCMWLANCore::getMCS(apple80211_mcs_data*)` rejects `NULL`, queries cached `"nrate"` state, treats success and `0xe00002e3` as completed query paths, decodes the public MCS index from the cached nrate word only when the word encodes Apple rate families `0x01000000`, `0x02000000`, or `0x03000000`, and returns the config-query status.
  - `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` is not generic unsupported on Tahoe: NAN roles `8..10` expose `0xe00002c7`, proximity/AWDL role `6` and APSTA/SoftAP role `7` expose owner-dependent failures, and unknown roles expose `0xe0000001`.
- actual behavior:
  - Tahoe `AirportItlwmSkywalkInterface::getMCS(...)` only updates a local `cachedCurrentMcs` from `ic_bss->ni_txmcs` in `RUN` and otherwise returns success with the old cached scalar.
  - legacy `AirportItlwm::getMCS(...)` reads `ic_bss->ni_txmcs` directly and returns success even when there is no cached transport `nrate`.
  - legacy `AirportItlwm::setVIRTUAL_IF_CREATE(...)` returns `kIOReturnUnsupported` for every Tahoe-era role before the reference public fail split can be observed.
- divergence point:
  - `AirportItlwmSkywalkInterface.cpp::getMCS(...)`
  - `AirportSTAIOCTL.cpp::getMCS(...)`
  - `AirportSTAIOCTL.cpp::setVIRTUAL_IF_CREATE(...)` under `__IO80211_TARGET >= __MAC_13_0`
- evidence:
  - decomp/disasm: remote `CR120_getMCS_forced_disasm.txt` for `AppleBCMWLANCore::getMCS(...)` at `0xffffff80016214c4..0xffffff8001621603` shows the `"nrate"` config query, accepted `0xe00002e3`, family mask `rate & 0x07000000`, public index writes at `data+4`, and return of the query status.
  - decomp: remote `/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c`, `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280`, shows role checks for `8..10`, `6`, `7`, and default unknown-role return `0xe0000001`.
  - local code: the Tahoe path already uses cached nrate for `getMCS_VHT(...)`, so the required local transport carrier exists.
  - local code: `cachedCurrentMcs` is only a local helper for the currently incorrect `getMCS(...)` path and can be removed once `getMCS(...)` reads nrate directly.
- candidate causes:
  - confirmed: local `getMCS(...)` producer source is not the recovered Apple `nrate` source.
  - confirmed: legacy virtual-interface creation remains a generic unsupported shadow while the Tahoe path already exposes the recovered role-dependent failure shape.
  - superseded by A-APSTA-OWNER-LAYER-RECONSTRUCTION-048: APSTA/SoftAP enablement cannot be faked on primary STA, but role `7` is a required APSTA/SAP owner reconstruction target, not a permanent failure policy.
- rejected causes:
  - force association success, RSN completion, key installation, EAPOL replay, DHCP, or link readiness.
  - fake APSTA/SoftAP owner state or advertise HostAP capability before the APSTA/SAP owner layer is reconstructed.
  - keep `getMCS(...)` tied to `ic_bss->ni_txmcs` because it is locally convenient; reference reads cached transport `nrate`.
- confirmed deviation: the public MCS producer and legacy virtual-interface shadow do not match recovered Tahoe semantics.
- root cause: confirmed for these layer contracts only. This batch does not claim final internet reachability or RSN/data root cause.
- fix:
  - add a shared local nrate decode policy to both Tahoe and legacy files.
  - make both `getMCS(...)` paths return the config-cache status and decode MCS from cached nrate.
  - remove the stale Tahoe `cachedCurrentMcs` carrier.
  - mirror Tahoe `setVIRTUAL_IF_CREATE(...)` role-dependent public failures in the legacy dispatcher for Tahoe-era targets.
- verification:
  - update YAML documentation with the new nrate and virtual-interface findings.
  - update inventory docs so these no longer remain open shadow mismatches.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh` with BootKC verification.
  - create a fresh CR-120 batch request superseding CR-119.

## FIX_CANDIDATE

- anomaly_id: A-MCS-NRATE-PRODUCER-AND-VIF-SHADOW-047
- symptom: remaining STA shadow surfaces drift from recovered Tahoe `getMCS` and virtual-interface public fail contracts.
- expected system behavior: `getMCS` is backed by cached `"nrate"` and returns the config-query status; virtual-interface creation exposes role-dependent Tahoe failures instead of generic unsupported.
- actual behavior: local `getMCS` reads association/node `ni_txmcs` or stale cached MCS, and legacy virtual-interface creation returns generic unsupported for all Tahoe-era roles.
- exact divergence point: `AirportItlwmSkywalkInterface::getMCS(...)`, `AirportItlwm::getMCS(...)`, and `AirportItlwm::setVIRTUAL_IF_CREATE(...)`.
- evidence from runtime: no new runtime is claimed for this static layer batch; current runtime remains post-association/no-internet. These are confirmed adjacent static divergences found before the next reboot cycle.
- evidence from decomp: `AppleBCMWLANCore::getMCS(...)` forced disassembly at `0xffffff80016214c4..0xffffff8001621603`; `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280`.
- exact semantic mismatch between reference and our code: reference reads cached transport `nrate` and returns query status, while local code reads BSS node state and always succeeds; reference exposes virtual-interface role fail split, while legacy local code returns generic unsupported.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the recovered producer bodies and local handlers disagree at exact system-facing getter/setter boundaries. The fix restores only those boundaries and does not mask downstream RSN/data symptoms.
- why proposed fix is 1:1 with reference architecture and semantics: nrate family masks and index extraction follow the recovered instruction sequence; virtual-interface failures mirror the already restored Tahoe no-owner path only as an interim state until APSTA/SAP owner creation is reconstructed.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportSTAIOCTL.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/93_tahoe_post_yaml_findings_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
- forbidden alternative fixes considered and rejected:
  - keep using `ni_txmcs` as the MCS source.
  - return success when the cached nrate query has no value.
  - fake APSTA/SoftAP interfaces or HostAP capability bits before the APSTA/SAP owner layer is reconstructed.
  - install/reboot/commit without approval.
- verification plan:
  - `rg cachedCurrentMcs` must return no code references.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - regenerate exact artifact and submit CR-120.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: `getMCS` forced disassembly and `setVIRTUAL_IF_CREATE` decomp identify exact producer/fail semantics.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim; this is an approved-cycle static restoration batch before the next runtime.
- Доказал ли я причинность, а не просто корреляцию? Yes for these public contracts; no final RSN/data root-cause claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes within the local available transport carriers and primary-STA/no-APSTA topology.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: no fake association, key, RSN, EAPOL, DHCP, APSTA, or link state is introduced.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: remote disasm/decomp paths and local functions are listed above.

## ANOMALY

- id: A-APSTA-OWNER-LAYER-RECONSTRUCTION-048
- status: CONFIRMED_DEVIATION
- symptom: the APSTA/SoftAP layer was documented as a non-primary owner path, but the local topology still lacks the Apple-required owner class/structure that role 7 creates.
- first visible manifestation: post-CR-120 review of the recovered `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` role-7 branch and APSTA symbols.
- expected system behavior:
  - `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` role 7 creates an `AppleBCMWLANIO80211APSTAInterface` object through the APSTA factory, stores it at core expansion `+0x2c30`, initializes APSTA private state at `self+0x130`, and returns role-specific status based on owner creation state.
  - APSTA is not a primary-STA shim. It is a separate `IO80211SapProtocol` / `AppleBCMWLANIO80211APSTAInterface` owner surface with BSD identity `ap` unit `1`, Wi-Fi subfamily `3`, SoftAP IOCTL carriers, state block offsets, queue/pool accessors, and datapath enable/disable methods.
  - If a class, owner object, state block, queue, or carrier exists in the Apple contract, local recovery must reconstruct it instead of treating its absence as a permanent limitation.
- actual behavior:
  - local `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE(...)` returns the recovered public role failures, including `0xe00002bd` for role 7, but does not create or store an APSTA owner object.
  - local docs in the CR-117..CR-120 area contain wording such as "do not create APSTA owner state" and "owner object absent" that is valid only as an interim no-owner failure shape, not as a final restoration rule.
  - the local tree has no `IO80211SapProtocol` header/model and no `AirportItlwmAPSTAInterface` owner that can hold APSTA state at `self+0x130`.
- divergence point:
  - `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE(...)` role 7 branch.
  - Tahoe AP/APSTA YAML addenda `94` and `95`.
  - pending request `CR-120` claim scope and guardrails.
- evidence:
  - decomp: `/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c`, `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280`, role 7 calls `FUN_ffffff8001685422(param_1, data+4, 7, data+0x10)`, stores the result at `param_1[0x25] + 0x2c30`, then writes feature-gated bytes at `(*(apsta+0x130)+0x32a)` and `(*(apsta+0x130)+0x32b)`.
  - decomp: `FUN_ffffff8001685422 @ 0xffffff8001685422` allocates `AppleBCMWLANIO80211APSTAInterface` with object size `0x138`.
  - decomp/symbols: `AppleBCMWLANIO80211APSTAInterface::~AppleBCMWLANIO80211APSTAInterface()` calls `IO80211SapProtocol` destructor, proving a distinct SAP protocol owner layer above the base Skywalk interface.
  - symbols: `/srv/project/ghidra_output/kdk_symbols.txt` lists `AppleBCMWLANIO80211APSTAInterface::withOptions(AppleBCMWLANCore*, ether_addr*, uint, char*)`, `init(AppleBCMWLANCore*, ether_addr*, uint, char*)`, `start(AppleBCMWLANCore*, IOSkywalkEthernetInterface::RegistrationInfo*)`, `getBSDNamePrefix`, `getInterfaceSubFamily`, `getBSDUnitNumber`, SoftAP methods, RSN/STA methods, and datapath queue accessors.
  - vtable dump: `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt` shows `IO80211SapProtocol` has its own vtable at `0xffffff80023e8dc0`; APSTA vtable at `0xffffff8001777508` places identity methods at slots `311/319/320`, queue/datapath methods at slots `425..439`, `forwardPacket` at slot `465`, and SoftAP/SAP methods at slots `514..529`. This proves a direct subclass of the current local `AirportItlwmSkywalkInterface` would have the wrong ABI shape for APSTA.
  - decomp: `getInterfaceSubFamily()` returns `3`; `getBSDUnitNumber()` returns `1`; YAML `89` records the APSTA BSD prefix as `ap`.
  - decomp: `getSOFTAP_PARAMS(...)` reads state block `self+0x130` offsets `+0x18/+0x1c/+0x20/+0x24/+0x28/+0x68/+0x0e/+0x10`; `getSOFTAP_STATS(...)` copies `0x58` bytes from `state+0x1b0`; `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` copies `0x24` bytes into APSTA state at owner `+0x2c` when feature gate `0x46` is enabled and length byte `<0x21`.
  - decomp: APSTA queue/datapath accessors read `state+0x2a4`, `+0x2d8`, `+0x2e0`, `+0x2e8`, `+0x2f0`, `+0x300`, `+0x320`, and map AC through `state+0x2b8 + ac*4`.
- candidate causes:
  - confirmed: local topology has not reconstructed the `IO80211SapProtocol`/APSTA owner surface required by the recovered role-7 path.
  - confirmed: the APSTA ABI cannot be recovered by deriving from the current primary STA class because the APSTA/SAP vtable slots differ from the local `IO80211InfraProtocol`/primary STA shape.
  - confirmed: previous docs overstated "do not create APSTA owner state" as a guardrail; the correct rule is "do not fake role-7 success before the required owner layer is reconstructed".
  - rejected: implement SoftAP selectors on the primary STA interface; reference places them on APSTA, so this would preserve the wrong owner topology.
  - rejected: leave APSTA permanently absent because Apple uses a class/object not yet present locally; the RE objective is to reconstruct required classes/objects.
- confirmed deviation: role-7 owner creation and SAP/APSTA class topology are missing locally.
- root cause: confirmed for the APSTA/SoftAP layer itself. This is not claimed as the current STA internet-connect root cause, but it is a real Apple-contract divergence that must be restored before AP mode can be considered complete.
- fix: supersede the CR-120 APSTA wording, document the exact APSTA owner reconstruction requirements, and prepare the next code batch around `IO80211SapProtocol` / `AirportItlwmAPSTAInterface` rather than around primary-STA failure-only handling.
- verification:
  - update YAML docs so "missing owner" becomes "reconstruct required owner" rather than "do not implement".
  - update pending commit request text so the reviewer sees APSTA failure-shape as interim, not final policy.
  - preserve current primary STA behavior until the APSTA owner object, state block, vtable surface, queues, and registration path are recovered 1:1.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-OWNER-LAYER-RECONSTRUCTION-048
- symptom: APSTA/SoftAP has a recovered owner class/state contract but local docs and request text still describe owner absence as a limiting guardrail.
- expected system behavior: recover missing Apple-required owner classes, state blocks, and structures; do not satisfy APSTA by primary-STA stubs.
- actual behavior: role 7 currently exposes a failure-only shape and docs say not to create owner state.
- exact divergence point: `setVIRTUAL_IF_CREATE` role 7, APSTA/SoftAP YAML addenda, and CR-120 claim scope.
- evidence from runtime: no new runtime claim; this is a static RE correction from recovered decomp/symbols.
- evidence from decomp: `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)`, APSTA factory/object size, APSTA state offsets, APSTA identity methods, SoftAP carriers, and queue/datapath accessors listed above.
- exact semantic mismatch between reference and our code: reference has a separate APSTA/SAP owner object; local code only returns missing-owner failure and the docs accidentally frame that as final.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the role-7 branch directly constructs/stores an APSTA owner and later APSTA methods directly read its state block; no alternate owner path exists in the reference evidence.
- why proposed fix is 1:1 with reference architecture and semantics: it changes the restoration target from primary-STA stubs to the recovered APSTA owner topology and preserves failure-only behavior only until the missing owner is implemented.
- files/functions to modify:
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/94_ap_mode_and_apsta_surface_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/95_mcs_nrate_and_virtual_if_shadow_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_signal_chain_audit.md`
  - `docs/tahoe_discrepancy_inventory.md`
  - `commit-approval/requests/CR-120-mcs-nrate-vif-shadow.md`
- forbidden alternative fixes considered and rejected:
  - declare APSTA/SoftAP permanently out of scope because the local owner class is absent.
  - implement APSTA/SoftAP selectors on the primary STA interface.
  - advertise HostAP capability before APSTA owner lifecycle is reconstructed.
  - fake role-7 success without an APSTA owner object, state block, queues, and registration path.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - no build required for documentation-only correction; the next code batch must build before submission.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: role-7 owner creation, APSTA object size, state offsets, identity methods, SoftAP carriers, and datapath accessors are recovered.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim is made here.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA layer incompleteness: the reference directly constructs and stores an owner object that local code lacks.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? This correction aligns the target architecture; implementation follows in the next code batch.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it reopens the missing owner layer as a required reconstruction item.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact decomp addresses, symbols, local functions, and docs are listed above.

## ANOMALY

- id: A-APSTA-STATE-BLOCK-SCAFFOLD-049
- status: CONFIRMED_DEVIATION
- symptom: APSTA/SAP owner reconstruction now has recovered state offsets, but the local tree still has no compile-time representation of the APSTA state block at `self+0x130`.
- first visible manifestation: APSTA owner-layer audit after A-APSTA-OWNER-LAYER-RECONSTRUCTION-048.
- expected system behavior:
  - `AppleBCMWLANIO80211APSTAInterface` owns a private state block at object offset `+0x130`.
  - SoftAP getters and setters read/write fixed fields inside that state block.
  - APSTA queue/datapath accessors read fixed queue, pool, queue-map, and feature-bit offsets inside the same state block.
  - Feature gates in role-7 creation write APSTA state bits at offsets `+0x32a` and `+0x32b`.
- actual behavior:
  - local code has no APSTA state-block type, no compile-time offset checks, and no local carrier for the recovered SAP/APSTA queue/pool fields.
  - the current role-7 path can only return missing-owner failure because there is no owner state object to initialize safely.
- divergence point:
  - missing local APSTA state definition corresponding to APSTA object member `self+0x130`.
  - no local offsets for SoftAP fields, stats, TX/RX pools, completion queues, multicast queue, or feature bits.
- evidence:
  - decomp: role-7 creation writes `(*(apsta+0x130)+0x32a)=1` and `(*(apsta+0x130)+0x32b)=1` after APSTA object creation.
  - decomp: `getSOFTAP_PARAMS(...)` reads APSTA state offsets `+0x0e`, `+0x10`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, `+0x28`, and `+0x68`.
  - decomp: `getSOFTAP_STATS(...)` copies `0x58` bytes from APSTA state `+0x1b0`.
  - decomp: `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` copies `0x24` bytes to APSTA state `+0x2c` when the Apple feature gate permits it.
  - decomp: APSTA queue/datapath accessors read `+0x2a4`, `+0x2b8`, `+0x2d8`, `+0x2e0`, `+0x2e8`, `+0x2f0`, `+0x300`, and `+0x320`.
- candidate causes:
  - confirmed: the local APSTA state layer is absent.
  - rejected: implement APSTA by adding these fields to the primary STA object; Apple keeps them in the APSTA/SAP owner state.
  - rejected: enable role-7 success before the APSTA owner lifecycle and registration path exist.
- confirmed deviation: local code lacks the recovered APSTA state block that Apple APSTA methods directly consume.
- root cause: confirmed for the APSTA owner state layer. This is a structural prerequisite and does not claim to solve the current STA association/data blocker by itself.
- fix:
  - add a local APSTA state-block definition with compile-time static asserts for every recovered Apple offset.
  - include that definition in the Tahoe Skywalk interface build so offset drift fails compilation immediately.
  - keep role-7 success disabled until the next owner-object/lifecycle layer is reconstructed.
- verification:
  - compile-time `static_assert` offsets for SoftAP, stats, queue/pool, multicast, and feature-bit fields.
  - YAML docs updated with the new local carrier and remaining owner-object work.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - create CR-122 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-STATE-BLOCK-SCAFFOLD-049
- symptom: recovered APSTA state offsets are documented but not represented in compilable local code.
- expected system behavior: local APSTA/SAP reconstruction has an exact state-block carrier before role-7 creation is exposed.
- actual behavior: no local APSTA state block exists, so role-7 creation cannot be restored without guessing storage.
- exact divergence point: Apple APSTA object `self+0x130` state block versus absent local carrier.
- evidence from runtime: no new runtime claim; this is a decomp/static reconstruction step.
- evidence from decomp: role-7 feature-bit writes, SoftAP getters/setters, stats copy, and APSTA datapath accessors listed in A-APSTA-STATE-BLOCK-SCAFFOLD-049.
- exact semantic mismatch between reference and our code: reference has fixed APSTA owner state offsets; local code has none.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: every recovered APSTA SoftAP/datapath method dereferences this state block directly, so the owner layer cannot be implemented correctly without it.
- why proposed fix is 1:1 with reference architecture and semantics: it encodes the recovered offsets only and does not invent state transitions or fake role-7 success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - add APSTA fields to the primary STA object.
  - expose role-7 success with no APSTA owner lifecycle.
  - derive APSTA from the current primary STA class before the SAP vtable/header is recovered.
  - fabricate queues or queue depths without the APSTA owner state.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-122.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the recovered APSTA methods directly read/write the listed state offsets.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim is made for this scaffold.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA owner reconstruction: the state block is a direct operand of the recovered Apple methods.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: offset-only state carrier, no guessed behavior.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: this is the missing structural layer needed before APSTA owner creation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA state offsets are listed above and mirrored in compile-time asserts.

## ANOMALY

- id: A-IO80211SAP-PROTOCOL-SCAFFOLD-050
- status: CONFIRMED_DEVIATION
- symptom: the APSTA state block now exists locally, but the Tahoe SAP protocol vtable shape is still absent, so a future APSTA owner would either derive from the wrong primary-STA class or append SoftAP methods at the wrong slots.
- first visible manifestation: APSTA/SAP vtable audit after A-APSTA-STATE-BLOCK-SCAFFOLD-049.
- expected system behavior:
  - `IO80211SapProtocol` is a distinct Tahoe owner protocol with vtable at `0xffffff80023e8dc0`.
  - Its vtable inherits the Tahoe `IO80211SkywalkInterface` shape through `syncDPSStats` and then exposes SAP/virtual-interface slots before APSTA overrides SoftAP and station-management selectors.
  - `AppleBCMWLANIO80211APSTAInterface` overrides SAP slots for `getSSID`, `getCHANNEL`, `getSTATE`, `getOP_MODE`, `getSTATION_LIST`, station IE/stats/key carriers, peer-cache, HostAP, SoftAP, RSN, and STA authorization/deauth.
- actual behavior:
  - local `include/Airport/IO80211VirtualInterface.h` is an older IOService-based virtual-interface header and does not represent the Tahoe SAP/Skywalk vtable seam.
  - local code has no `IO80211SapProtocol.h`, so there is no compile-time APSTA/SAP base shape to prevent vtable drift.
- divergence point:
  - recovered `IO80211SapProtocol` vtable slots `481..519` and APSTA slots `505..531` versus absent local Tahoe SAP protocol header.
- evidence:
  - remote vtable dump `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt` shows `IO80211SapProtocol` vtable at `0xffffff80023e8dc0`, with slots `280..480` aligned to the Tahoe Skywalk base and slots `481..519` forming the SAP/virtual-interface extension seam.
  - the same dump shows `AppleBCMWLANIO80211APSTAInterface` vtable at `0xffffff8001777508`, overriding APSTA datapath slots `425..439`, `forwardPacket` at slot `465`, and SAP/SoftAP slots `505..531`.
  - KDK symbols list only `IO80211SapProtocol` meta/ctor/dtor symbols, proving there is no rich local method header to import from; the slot surface must be reconstructed from vtable and APSTA override evidence.
  - KDK symbols identify APSTA methods and signatures including `getSSID(apple80211_ssid_data*)`, `getCHANNEL(apple80211_channel_data*)`, `getSTATE(apple80211_state_data*)`, `getOP_MODE(apple80211_opmode_data*)`, `getSTATION_LIST(apple80211_sta_data*)`, `getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*)`, `getHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t*)`, `getSOFTAP_PARAMS(apple80211_softap_params*)`, `getSOFTAP_STATS(apple80211_softap_stats*)`, `setHOST_AP_MODE(apple80211_network_data*)`, `setSTA_AUTHORIZE(apple80211_sta_authorize_data*)`, `setSTA_DEAUTH(apple80211_sta_disassoc_data*)`, `setRSN_CONF(apple80211_rsn_conf_data*)`, `setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params*)`, and `setSOFTAP_WIFI_NETWORK_INFO_IE(apple80211_softap_wifi_network_info*)`.
- candidate causes:
  - confirmed: local SAP protocol header is absent.
  - confirmed: deriving APSTA from `IO80211InfraProtocol` or the old local `IO80211VirtualInterface` would produce the wrong owner topology or vtable slots.
  - rejected: use no-arg reserved placeholders for SoftAP slots; APSTA methods must occupy the recovered slots with their typed signatures, not append after them.
- confirmed deviation: local headers cannot yet express the recovered Tahoe SAP/APSTA method-slot seam.
- root cause: confirmed for APSTA owner-class reconstruction. This remains structural and does not claim primary-STA data/connectivity resolution.
- fix:
  - add a Tahoe-only `IO80211SapProtocol` contract header that records the recovered SAP/APSTA slot seam.
  - declare the recovered typed SoftAP and station-management slot carriers as a compile-time contract so future APSTA owner implementation cannot append them at wrong slots.
  - do not define a C++ `IO80211SapProtocol` base class yet because the recovered APSTA `forwardPacket` override occupies a slot currently named differently in the local Skywalk header; the full SAP class requires a separate exact slot map, not a subclass of the current primary-STA header.
  - include the header from the V3/V2 Apple80211 umbrella so it is compiled in the Tahoe build.
  - do not instantiate SAP/APSTA or return role-7 success yet.
- verification:
  - compile with the new header in the Tahoe umbrella include path.
  - update YAML/docs with the SAP protocol vtable seam.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - create CR-123 batch request.

## FIX_CANDIDATE

- anomaly_id: A-IO80211SAP-PROTOCOL-SCAFFOLD-050
- symptom: APSTA owner state exists locally but the SAP protocol base/vtable seam is still missing.
- expected system behavior: local APSTA owner reconstruction has a Tahoe `IO80211SapProtocol` base shape before any APSTA class is instantiated.
- actual behavior: local headers only have primary STA and old virtual-interface shapes.
- exact divergence point: `IO80211SapProtocol` vtable `481..519` and APSTA overrides `505..531`.
- evidence from runtime: no new runtime claim; this is a decomp/static header reconstruction step.
- evidence from decomp: resolved SAP/APSTA vtable dump and APSTA KDK symbols listed above.
- exact semantic mismatch between reference and our code: reference has a distinct SAP protocol seam; local code has no way to place APSTA SoftAP methods at the recovered slots.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: APSTA role-7 creation returns an APSTA object whose virtual methods are called through this exact SAP/Skywalk vtable seam. A wrong base class would shift the selectors even if the state block is correct.
- why proposed fix is 1:1 with reference architecture and semantics: it records the recovered SAP/APSTA vtable seam and typed slots without defining an ABI-wrong C++ base class or inventing runtime behavior.
- files/functions to modify:
  - `include/Airport/IO80211SapProtocol.h`
  - `include/Airport/Apple80211.h`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - derive APSTA from `IO80211InfraProtocol` or current `AirportItlwmSkywalkInterface`.
  - derive APSTA from the old local `IO80211VirtualInterface` header.
  - define `IO80211SapProtocol` as a simple subclass of current local `IO80211SkywalkInterface` while slot aliases remain unresolved.
  - use no-arg reserved placeholders for typed SAP/SoftAP slots.
  - instantiate APSTA before constructor/start/free and registration contracts are recovered.
  - advertise HostAP or return role-7 success in this batch.
- verification plan:
  - compile Tahoe target with the new header included.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-123.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the SAP/APSTA vtable dump and APSTA symbols recover the seam and typed selectors.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made for this header scaffold.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA owner reconstruction: wrong base shape would place APSTA selectors in the wrong vtable slots.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes at the header/vtable-shape level; no runtime behavior is invented.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: this restores the missing SAP protocol layer required before APSTA owner creation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: vtable slot ranges and APSTA symbols are listed above.

## ANOMALY

- id: A-APSTA-LIFECYCLE-STATE-RESOURCE-051
- status: CONFIRMED_DEVIATION
- symptom: the APSTA state scaffold covers SoftAP/datapath offsets, but the recovered APSTA lifecycle uses additional state resources and an exact state allocation size that are not yet represented locally.
- first visible manifestation: APSTA owner lifecycle audit after A-IO80211SAP-PROTOCOL-SCAFFOLD-050.
- expected system behavior:
  - APSTA `free()` treats the private state block at `self+0x130` as a `0x338`-byte allocation.
  - APSTA `freeResources()`, `stop(IOService*)`, `reset()`, and `initSoftAPParameters()` release or reset timer/work-source/resource pointers and state flags at fixed offsets.
  - The APSTA state block carries not only public SoftAP/datapath fields, but also lifecycle resources needed for safe teardown and reset.
- actual behavior:
  - local `AirportItlwmAPSTAStateBlock` only asserted coverage through feature bits and left lifecycle/resource offsets inside padding.
  - state size was asserted only as minimum coverage rather than exact recovered lifecycle size.
- divergence point:
  - APSTA state offsets `+0x70`, `+0x78`, `+0xb8`, `+0x1a8`, `+0x218`, `+0x240`, `+0x248`, `+0x250`, `+0x258`, `+0x260`, `+0x26c`, `+0x329`, and exact state size `0x338`.
- evidence:
  - decomp: `AppleBCMWLANIO80211APSTAInterface::free()` calls `freeResources()` and then zeroes/releases `*(self+0x130)` with size `0x338`.
  - decomp: `freeResources()` stops/releases state pointers at `+0x70`, `+0x78`, `+0x240`, `+0x248`, `+0x250`, `+0x258`, and `+0x260`.
  - decomp: `stop(IOService*)` detaches TX queues, TX/RX completion queues, and multicast work source at state offsets already recorded for datapath.
  - decomp: `reset()` writes `0` to state `+0x26c`, clears byte `+0x329`, reads owner/core pointer at `+0x218`, updates link state, and zeroes `state+0xb8` for `0xf0` bytes.
  - decomp: `initSoftAPParameters()` clears the `state+0xb8` runtime block and writes zero at `state+0x1a8`.
- candidate causes:
  - confirmed: lifecycle/resource state offsets were hidden inside padding in the local scaffold.
  - confirmed: exact APSTA state allocation size is now recovered as `0x338`.
  - rejected: keep treating state size as unknown once `free()` provides the release size.
- confirmed deviation: local APSTA state block did not yet encode lifecycle resources and exact state allocation size.
- root cause: confirmed for APSTA lifecycle scaffold completeness. This still does not instantiate APSTA or claim primary-STA connectivity resolution.
- fix:
  - split APSTA state padding into named lifecycle/resource fields at recovered offsets.
  - add static asserts for lifecycle timers/resources, owner/core pointer, reset flag/state, runtime block, and exact `0x338` state size.
  - update YAML/docs with lifecycle offset evidence.
  - keep role-7 success disabled until constructor/start/registration are implemented.
- verification:
  - compile-time offset asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - create CR-124 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-LIFECYCLE-STATE-RESOURCE-051
- symptom: APSTA state scaffold lacks lifecycle/resource fields and exact state size recovered from APSTA `free/reset/stop` paths.
- expected system behavior: APSTA state block exposes lifecycle resources and exact `0x338` allocation size before APSTA owner instantiation.
- actual behavior: those offsets were padding and size was only minimum coverage.
- exact divergence point: APSTA lifecycle state offsets and state release size.
- evidence from runtime: no new runtime claim; this is decomp/static lifecycle reconstruction.
- evidence from decomp: APSTA `free()`, `freeResources()`, `stop(IOService*)`, `reset()`, and `initSoftAPParameters()` offsets listed above.
- exact semantic mismatch between reference and our code: reference lifecycle operates on named state resources; local scaffold hid them and did not assert exact size.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: APSTA teardown/reset directly dereferences these offsets, so a role-7 owner cannot be made safe without them.
- why proposed fix is 1:1 with reference architecture and semantics: it only names and asserts recovered offsets and exact allocation size; it does not create or start APSTA.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - leave lifecycle offsets as anonymous padding.
  - keep APSTA state size open after `free()` proves `0x338`.
  - instantiate APSTA owner before lifecycle resources and teardown path are encoded.
  - return role-7 success or advertise HostAP in this batch.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-124.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA lifecycle methods directly use the recovered offsets and state release size.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made for this scaffold.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA lifecycle safety: these offsets are direct teardown/reset operands.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: offset and size assertions only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores the missing lifecycle state layer.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: APSTA lifecycle functions and offsets are listed above.

## ANOMALY

- id: A-APSTA-ROLE7-CREATION-STORAGE-052
- status: CONFIRMED_DEVIATION
- symptom: APSTA state and SAP contracts are now present, but the role-7 creation/storage contract from `setVIRTUAL_IF_CREATE` is still only prose and not represented in compiled local code.
- first visible manifestation: APSTA owner creation audit after A-APSTA-LIFECYCLE-STATE-RESOURCE-051.
- expected system behavior:
  - `apple80211_virt_if_create_data` role field at `data+0x0c` selects APSTA role `7`.
  - role 7 passes `data+0x04` as the MAC carrier and `data+0x10` as the output BSD-name carrier to the APSTA factory.
  - the APSTA owner pointer is stored in core expansion at `+0x2c30`.
  - duplicate APSTA owner returns `0xe00002d2`; failed APSTA creation returns `0xe00002bd`; unknown roles return `0xe0000001`.
  - successful creation applies feature gates `0x0d` and `0x0c` to APSTA state bytes `+0x32a` and `+0x32b`.
- actual behavior:
  - local code still has only failure-only role-7 behavior and no compiled owner-storage/factory-argument contract.
  - APSTA core expansion storage offsets and role-7 carrier offsets are documented but not asserted in code.
- divergence point:
  - role-7 branch of `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` versus absent local `AirportItlwmAPSTACoreExpansionStorageLayout` and role-7 carrier witness.
- evidence:
  - decomp: `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280` checks `*(int *)(data+0x0c) == 7`.
  - decomp: when no APSTA owner exists at `coreExpansion+0x2c30`, it calls `FUN_ffffff8001685422(core, data+0x04, 7, data+0x10)` and stores the returned pointer at `coreExpansion+0x2c30`.
  - decomp: duplicate APSTA owner returns `0xe00002d2`; failed creation returns `0xe00002bd`; unknown role returns `0xe0000001`.
  - decomp: after successful creation, feature gate `0x0d` writes `state+0x32a`, and feature gate `0x0c` writes `state+0x32b`.
  - symbols: `AppleBCMWLANIO80211APSTAInterface::withOptions(AppleBCMWLANCore*, ether_addr*, uint, char*)` and `init(AppleBCMWLANCore*, ether_addr*, uint, char*)` confirm the factory/init argument shape.
- candidate causes:
  - confirmed: local code lacks a compiled owner-storage/role-7 carrier witness.
  - rejected: wire role-7 success before final APSTA class, registration, resource creation, and SAP slot aliases are complete.
- confirmed deviation: role-7 owner storage and factory carrier contract absent from compiled local APSTA scaffold.
- root cause: confirmed for APSTA owner-creation scaffold completeness only.
- fix:
  - add role-7 constants and return-code constants to APSTA scaffold.
  - add a packed virtual-interface create carrier witness with static asserts for `mac`, `role`, and `bsdName` offsets.
  - add a core expansion storage witness with APSTA owner pointer at `+0x2c30`.
  - add a typed factory function contract for `(core, ether_addr*, role, bsdName)`.
  - keep local `setVIRTUAL_IF_CREATE` role-7 runtime behavior unchanged until the APSTA owner class and registration path are implemented.
- verification:
  - compile-time offset asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - create CR-125 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-ROLE7-CREATION-STORAGE-052
- symptom: role-7 APSTA creation/storage facts are recovered but not compiled into local code.
- expected system behavior: local APSTA scaffold records carrier offsets, core storage offset, factory argument shape, role, return codes, and feature gates before runtime creation is enabled.
- actual behavior: these facts exist only in docs and role 7 still returns no-owner failure.
- exact divergence point: `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` role-7 branch.
- evidence from runtime: no runtime claim; this is static/decomp scaffold restoration.
- evidence from decomp: role-7 branch and APSTA factory symbols listed above.
- exact semantic mismatch between reference and our code: reference stores APSTA owner at core expansion `+0x2c30` using exact carrier offsets; local code has no compiled witness for that storage contract.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: role-7 creation directly consumes these offsets and writes the owner pointer at this core expansion field.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes the recovered constants/offsets and keeps runtime creation disabled until later layers are complete.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - return role-7 success without APSTA owner class/registration.
  - store APSTA owner in primary STA object state instead of core expansion `+0x2c30`.
  - use guessed carrier offsets instead of the recovered `data+0x04/+0x0c/+0x10`.
  - advertise HostAP in this batch.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-125.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: role-7 branch directly shows carrier offsets, core storage, return codes, and feature gates.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA owner creation: these are direct operands of the recovered creation path.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: constants and offsets only, no guessed behavior.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores the missing owner-storage scaffold.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact role-7 branch evidence is listed above.

## ANOMALY

- id: A-APSTA-DATAPATH-ACTIVATION-RESOURCE-053
- status: CONFIRMED_DEVIATION
- symptom: APSTA role-7 storage, lifecycle, and queue accessors are now represented, but two active APSTA datapath/resource state operands are still hidden inside padding.
- first visible manifestation: APSTA datapath activation audit after A-APSTA-ROLE7-CREATION-STORAGE-052.
- expected system behavior:
  - APSTA method at `0xffffff8001694064` returns the private state object at `state+0x210`.
  - SoftAP/APSTA event and async callback paths use `state+0x210` as the bytestream/logger sink for firmware event payload traces.
  - `enableDatapath()` calls the object stored at `state+0x2d0` through vtable slot `+0x120` before starting TX/RX completion queues.
  - `disableDatapath()` calls the object stored at `state+0x2d0` through vtable slot `+0x128` before stopping RX/TX completion queues.
  - queue-missing failure remains `0xe00002bc`; APSTA creation remains disabled until owner class/start/resource creation is complete.
- actual behavior:
  - local `AirportItlwmAPSTAStateBlock` names queues, pools, lifecycle resources, and feature bits, but still treats `+0x210` and `+0x2d0` as anonymous padding.
  - local docs list queue accessors but not the datapath activation owner object used by enable/disable.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::enableDatapath() @ 0xffffff8001693b82`
  - `AppleBCMWLANIO80211APSTAInterface::disableDatapath() @ 0xffffff8001693e80`
  - `FUN_ffffff8001694064 @ 0xffffff8001694064`
- evidence:
  - decomp: `FUN_ffffff8001694064` returns `*(state+0x210)`.
  - decomp: `handleEvent(...)`, `handleSetRpsNoaAsyncCallBack`, and `handleSetBcnIntervalAsyncCallBack` use `state+0x210` with bytestream helper calls.
  - decomp: `enableDatapath()` invokes `(*(state+0x2d0)->vtable+0x120)(state+0x2d0, self)`, starts `state+0x2e8` and `state+0x2f0`, then arms the RX completion queue via vtable `+0x298`.
  - decomp: `disableDatapath()` invokes `(*(state+0x2d0)->vtable+0x128)(state+0x2d0, self)`, then stops `state+0x2f0` and `state+0x2e8`.
  - docs: APSTA queue accessor offsets are already recorded in YAML 96, but activation owner `+0x2d0` and bytestream/resource `+0x210` are absent from the compiled state scaffold.
- candidate causes:
  - confirmed: local APSTA scaffold lacks named fields/static asserts for two direct APSTA datapath/resource operands.
  - rejected: enable role-7 success now; constructor, start registration, resource creation, and SAP slot aliases are not complete.
- confirmed deviation: two direct APSTA state operands are not represented in compiled local code.
- root cause: confirmed for APSTA datapath activation/resource scaffold completeness only.
- fix:
  - split APSTA state padding to name `resource210` at `+0x210`.
  - split APSTA state padding to name `datapathOwner2d0` at `+0x2d0`.
  - add static asserts for both recovered offsets.
  - update YAML and prose docs with the activation contract.
  - keep runtime role-7/APSTA creation disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-126 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-DATAPATH-ACTIVATION-RESOURCE-053
- symptom: APSTA datapath activation/resource offsets are recovered in decomp but absent from compiled local scaffold.
- expected system behavior: local APSTA scaffold records every direct state operand used by APSTA datapath activation and bytestream/resource access before any runtime APSTA creation is enabled.
- actual behavior: `state+0x210` and `state+0x2d0` remain anonymous padding.
- exact divergence point: APSTA `enableDatapath()`, `disableDatapath()`, and `FUN_ffffff8001694064`.
- evidence from runtime: no new runtime claim; this is static/decomp scaffold restoration.
- evidence from decomp: functions and offsets listed in A-APSTA-DATAPATH-ACTIVATION-RESOURCE-053.
- exact semantic mismatch between reference and our code: reference uses named owner/resource slots at `+0x210` and `+0x2d0`; local code cannot safely model APSTA activation without them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: these are direct operands dereferenced by reference APSTA datapath methods.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes recovered offsets and does not start queues, allocate APSTA, or return role-7 success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - return role-7 success before constructor/start/resource creation are recovered.
  - invent a local datapath owner object without the factory/start path.
  - null-guard or mask missing APSTA queues.
  - fake HostAP/APSTA capability in the primary STA object.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-126.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA datapath/resource functions directly dereference `state+0x210` and `state+0x2d0`.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA activation scaffold completeness: the offsets are direct operands.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: offset fields/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores missing APSTA state operands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact decomp functions and offsets are listed above.

## ANOMALY

- id: A-APSTA-INIT-START-RESOURCE-CONTRACT-054
- status: CONFIRMED_DEVIATION
- symptom: APSTA private-state offsets for datapath activation are now named, but the APSTA init/start resource contract that populates those fields is still absent from compiled local scaffolding.
- first visible manifestation: APSTA factory/init/start audit after A-APSTA-DATAPATH-ACTIVATION-RESOURCE-053.
- expected system behavior:
  - `withOptions(core, mac, role, bsdName)` allocates APSTA object size `0x138` and calls `init(core, mac, role, bsdName)` through the APSTA vtable.
  - `init(core, mac, role, bsdName)` allocates APSTA state, stores owner/core at `state+0x218`, clears feature bytes `+0x32a/+0x32b`, initializes timer/resource fields, and sets APSTA queue defaults.
  - `init(...)` stores resource references at state offsets `+0x210`, `+0x228`, `+0x240`, `+0x248`, `+0x250`, `+0x258`, and `+0x260`.
  - `init(...)` sets `state+0x268 = 2`, `state+0x20c = 0`, clears `state+0x88` and `state+0xb0`, sets `state+0x14 = 0x12c`, sets `state+0x2a4 = 4`, and seeds a four-entry map at `state+0x2a8..0x2b4` with `0,1,2,3`.
  - `start(core, RegistrationInfo*)` refreshes logger/resource `state+0x210`, stores work queue at `state+0x330`, stores bus interface/provider at `state+0x2c8`, resolves datapath owner at `state+0x2d0`, and calls owner vtable `+0x118` with a stack configuration that points to APSTA queue/pool storage fields.
- actual behavior:
  - local scaffold names lifecycle resources and datapath accessors but does not encode the init/start state slots or the queue-configuration carrier.
  - local code has no compiled witness for the stack contract passed to datapath owner vtable `+0x118`.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::withOptions(...) @ 0xffffff8001685422`
  - `AppleBCMWLANIO80211APSTAInterface::init(AppleBCMWLANCore*, ether_addr*, uint, char*) @ 0xffffff80016854ee`
  - `AppleBCMWLANIO80211APSTAInterface::start(AppleBCMWLANCore*, IOSkywalkEthernetInterface::RegistrationInfo*) @ 0xffffff8001686378`
- evidence:
  - disasm: `withOptions(...)` allocates `0x138`, installs the APSTA vtable, and calls vtable slot `+0x1090` with `(core, mac, role, bsdName)`.
  - disasm: `init(...)` stores `core` at `state+0x218`, clears `+0x32a/+0x32b`, creates timer sources at `+0x70/+0x78`, and fills resource offsets `+0x210`, `+0x228`, `+0x240`, `+0x248`, `+0x250`, `+0x258`, `+0x260`.
  - disasm: `init(...)` writes defaults at `+0x14`, `+0x20c`, `+0x268`, `+0x2a4`, and `+0x2a8..0x2b4`.
  - disasm: `start(...)` stores work queue `+0x330`, bus/provider `+0x2c8`, datapath owner `+0x2d0`, and passes pointers to `+0x2a8`, `+0x300`, `+0x2f8`, `+0x2e8`, `+0x2f0`, `+0x320`, `+0x2d8`, `+0x2e0`, plus logger to vtable `+0x118`.
  - disasm: on registration failure, `start(...)` removes work sources for TX queues, TX/RX completion queues, and multicast queue instead of masking failures.
- candidate causes:
  - confirmed: local APSTA scaffold lacks named fields/static asserts for init/start operands and lacks the start queue-configuration carrier witness.
  - rejected: allocate local APSTA or return role-7 success before the full class/registration/resource path is represented.
- confirmed deviation: APSTA init/start resource slots and configuration carrier are not represented in compiled local code.
- root cause: confirmed for APSTA owner init/start scaffold completeness only.
- fix:
  - add named state fields and static asserts for recovered init/start offsets.
  - add a local stack-carrier witness for the APSTA start queue/resource configuration passed to datapath owner vtable `+0x118`.
  - update YAML/prose docs.
  - keep runtime role-7 creation disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-127 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-INIT-START-RESOURCE-CONTRACT-054
- symptom: APSTA init/start resource and queue-configuration contracts are recovered but absent from compiled local scaffolding.
- expected system behavior: local APSTA scaffolding records every direct init/start state operand and the carrier shape used when APSTA asks the datapath owner to create queues/pools.
- actual behavior: the state slots and carrier remain anonymous or absent.
- exact divergence point: APSTA `withOptions(...)`, `init(core, mac, role, bsdName)`, and `start(core, RegistrationInfo*)`.
- evidence from runtime: no new runtime claim; this is static/disasm scaffold restoration.
- evidence from decomp: APSTA disassembly addresses and operands listed in A-APSTA-INIT-START-RESOURCE-CONTRACT-054.
- exact semantic mismatch between reference and our code: reference init/start writes and passes APSTA owner/resource fields that local compiled scaffolding does not yet represent.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: these are direct stores and direct carrier fields used before APSTA can register and create its datapath.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes recovered offsets and carrier layout; it does not allocate APSTA, create queues, or return role-7 success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - instantiate APSTA before the init/start carrier is represented.
  - invent queue ownership or work-source registration without the recovered `+0x118` owner call.
  - ignore `state+0x2c8`, `state+0x2f8`, or `state+0x330` because current local code lacks matching objects.
  - fake HostAP/APSTA capability in the primary STA object.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-127.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA init/start disassembly directly shows the stores, references, and carrier offsets.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA init/start scaffold completeness: these are direct operands of APSTA creation/start.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: fields and carrier layout only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores missing APSTA init/start operands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA disassembly addresses and offsets are listed above.

## ANOMALY

- id: A-APSTA-HOSTAP-STATE-MACHINE-FIELDS-055
- status: CONFIRMED_DEVIATION
- symptom: APSTA init/start resource scaffolding is present, but AP/SoftAP control state fields used by HostAP mode transitions are still hidden in padding or over-broad fields.
- first visible manifestation: AP-mode control-path audit after A-APSTA-INIT-START-RESOURCE-CONTRACT-054.
- expected system behavior:
  - `holdSoftAPPowerAssertion()` writes `state+0x0c = 1`.
  - `setHOST_AP_MODE_HIDDEN(...)` writes hidden-mode state byte `state+0x0d`.
  - `hostAPPowerOff()` and `setHOST_AP_MODE_HIDDEN(...)` clear `state+0x0e` on AP shutdown/hidden-mode transitions.
  - `configureLowPowerModeExit()` checks a 32-bit field at `state+0xb4`.
  - `setHostApModeInternal(...)` tests/writes `state+0x26c` as the AP-up state and tests/writes `state+0x270` as a HostAP transition/in-progress state.
  - successful AP enable sets `state+0x26c = 1`, clears `state+0x20c`, and updates `state+0x14`.
  - `setHOST_AP_MODE(...)` wraps `setHostApModeInternal(...)` with proximity/NAN bringdown/bringup around core expansion owners `+0x2c28`, `+0x74f0`, and `+0x74f8`, gated by feature `0x46`.
- actual behavior:
  - local APSTA state scaffold had no named bytes for `+0x0c/+0x0d`, no exact `+0xb4` 32-bit field, and no named `+0x270` transition field.
  - local docs described APSTA resources and queue start but not the AP-mode control flags.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHOST_AP_MODE(...) @ 0xffffff80016884ae`
  - `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...) @ 0xffffff8001688bc2`
  - `AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff() @ 0xffffff8001692772`
  - `AppleBCMWLANIO80211APSTAInterface::setHOST_AP_MODE_HIDDEN(...) @ 0xffffff800168d970`
  - `AppleBCMWLANIO80211APSTAInterface::holdSoftAPPowerAssertion() @ 0xffffff800168dbc2`
  - `AppleBCMWLANIO80211APSTAInterface::configureLowPowerModeExit() @ 0xffffff80016928e4`
- evidence:
  - disasm: `holdSoftAPPowerAssertion()` writes byte `state+0x0c`.
  - disasm: `setHOST_AP_MODE_HIDDEN(...)` writes byte `state+0x0d` and clears byte `state+0x0e` on the shutdown branch.
  - decomp: `hostAPPowerOff()` tests `state+0x26c`, calls `setPowerSaveState`, clears byte `state+0x0e`, and calls `setHostApModeInternal(NULL)`.
  - disasm: `configureLowPowerModeExit()` compares 32-bit `state+0xb4` with zero.
  - disasm: `setHostApModeInternal(...)` tests/writes 32-bit `state+0x270`, writes `state+0x26c`, clears `state+0x20c`, and updates `state+0x14`.
  - decomp: `setHOST_AP_MODE(...)` coordinates proximity/NAN owners around `setHostApModeInternal(...)` under feature gate `0x46`.
- candidate causes:
  - confirmed: AP-mode control fields were not represented as exact local state operands.
  - rejected: implement HostAP state transitions before the full control path and firmware IOVAR sequence are represented.
- confirmed deviation: AP-mode control flags and transition state fields are missing from compiled local scaffold.
- root cause: confirmed for APSTA AP-mode state scaffold completeness only.
- fix:
  - name state bytes `+0x0c` and `+0x0d`.
  - split the `+0xb0` runtime area so `+0xb4` is an exact 32-bit field.
  - name state dword `+0x270`.
  - add static asserts and update YAML/prose docs.
  - keep runtime HostAP/role-7 creation disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-128 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-HOSTAP-STATE-MACHINE-FIELDS-055
- symptom: AP/SoftAP control state fields are recovered but absent from the compiled APSTA state scaffold.
- expected system behavior: APSTA state scaffold records AP-mode flags and transition fields before HostAP mode methods are implemented.
- actual behavior: those fields remain padding or over-broad runtime fields.
- exact divergence point: APSTA `setHOST_AP_MODE`, `setHostApModeInternal`, `setHOST_AP_MODE_HIDDEN`, `holdSoftAPPowerAssertion`, `hostAPPowerOff`, and `configureLowPowerModeExit`.
- evidence from runtime: no new runtime claim; this is static/decomp scaffold restoration.
- evidence from decomp: APSTA decomp/disasm addresses and operands listed in A-APSTA-HOSTAP-STATE-MACHINE-FIELDS-055.
- exact semantic mismatch between reference and our code: reference AP-mode control dereferences exact state bytes/dwords that local code does not yet model.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: these fields are direct operands of the AP-mode transition path.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes recovered offsets; it does not implement AP enable/disable, firmware IOVAR calls, or capability publication.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - advertise HostAP capability from primary STA.
  - force AP mode success without firmware IOVAR sequence.
  - hide missing AP state with default/null guards.
  - enable role-7 owner before final APSTA class and method bodies are complete.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-128.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA AP-mode methods directly read/write these offsets.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for AP-mode scaffold completeness: these are direct operands in AP-mode transitions.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact fields/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores missing AP-mode state operands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA AP-mode methods and offsets are listed above.

## ANOMALY

- id: A-APSTA-REGISTRATION-INFO-CARRIER-056
- status: CONFIRMED_DEVIATION
- symptom: APSTA owner/state/start scaffolds exist, but the exact APSTA `RegistrationInfo` carrier assembled by role-7 `setVIRTUAL_IF_CREATE` is still absent from compiled local scaffolding.
- first visible manifestation: APSTA role-7 registration audit after A-APSTA-HOSTAP-STATE-MACHINE-FIELDS-055.
- expected system behavior:
  - after successful role-7 APSTA owner creation and feature gates, `setVIRTUAL_IF_CREATE(...)` zeroes a stack `RegistrationInfo` block of size `0x130`.
  - it calls `APSTA->initRegistrationInfo(info, 1, 0x130)` before APSTA-specific field writes.
  - it writes `info+0x14 = 2`.
  - it copies the APSTA hardware address returned by APSTA vtable `+0xcf8` to bytes `info+0x108..0x10d`.
  - it writes qword `info+0x24 = 0x8000000080`.
  - it writes dword `info+0x0c = 3`.
  - it writes BSD prefix pointer `"ap"` to `info+0x30` and BSD unit `1` to `info+0x38`.
  - it writes flags at `info+0x40`, using `6` when `IOPMResetPowerStateOnWake` is present and otherwise OR-ing bit `0x4` into the existing field.
  - it then calls `AppleBCMWLANIO80211APSTAInterface::start(core, info)`.
- actual behavior:
  - local code has only an opaque `IOSkywalkEthernetInterface::RegistrationInfo` pad and no APSTA-specific compiled carrier witness.
  - APSTA registration fields are documented only indirectly in disassembly notes.
- divergence point:
  - role-7 branch of `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)`, disassembly range `0xffffff80015fc628..0xffffff80015fc73b`.
- evidence:
  - disasm: `leaq -0x160(%rbp), %r12; movl $0x130, %esi; call bzero`.
  - disasm: call `0xffffff8002a3d7f2` with APSTA owner, stack info, type `1`, and size `0x130`.
  - disasm: writes `0x14`, `0x24`, `0x0c`, `0x30`, `0x38`, `0x40`, and bytes `0x108..0x10d`.
  - disasm: calls `AppleBCMWLANIO80211APSTAInterface::start(core, RegistrationInfo*)` with the same stack carrier.
- candidate causes:
  - confirmed: local APSTA scaffold lacks an APSTA registration carrier layout and offset asserts.
  - rejected: rely on the opaque local registration pad when implementing role-7 success.
- confirmed deviation: APSTA role-7 registration info layout is not represented in compiled local code.
- root cause: confirmed for APSTA registration scaffold completeness only.
- fix:
  - add `AirportItlwmAPSTARegistrationInfoLayout` as a packed layout witness.
  - add constants for size, init type, fixed APSTA fields, and registration option qword.
  - add static asserts for all recovered offsets and size.
  - update YAML/prose docs.
  - keep role-7 success disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-129 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-REGISTRATION-INFO-CARRIER-056
- symptom: APSTA registration carrier fields are recovered but absent from compiled local scaffolding.
- expected system behavior: local scaffolding records the exact APSTA `RegistrationInfo` producer contract before role-7 success is enabled.
- actual behavior: only an opaque registration pad exists locally.
- exact divergence point: role-7 `setVIRTUAL_IF_CREATE(...)` APSTA registration-info assembly.
- evidence from runtime: no new runtime claim; this is static/disasm scaffold restoration.
- evidence from decomp: disassembly range `0xffffff80015fc628..0xffffff80015fc73b` listed above.
- exact semantic mismatch between reference and our code: reference writes APSTA-specific registration fields at fixed offsets; local code does not model those offsets.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: these fields are direct inputs to APSTA `start(core, RegistrationInfo*)`.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes recovered constants/offsets; it does not call `start`, allocate APSTA, or return role-7 success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - use the opaque local `RegistrationInfo` pad without APSTA offset assertions.
  - set APSTA BSD fields by guessed names or guessed offsets.
  - enable role-7 APSTA before registration carrier, class/vtable, and queue creation are fully represented.
  - fake APSTA registration through primary STA.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-129.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: role-7 registration-info assembly directly writes the recovered fields.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA registration scaffold completeness: these fields are direct inputs to APSTA start.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact carrier layout/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores missing APSTA registration carrier operands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact disassembly addresses and offsets are listed above.

## ANOMALY

- id: A-APSTA-REGISTER-ETHERNET-QUEUE-LIST-057
- status: CONFIRMED_DEVIATION
- symptom: APSTA `RegistrationInfo` carrier is now represented, but the APSTA queue-list carrier passed to `registerEthernetInterface` is still only prose.
- first visible manifestation: APSTA start registration audit after A-APSTA-REGISTRATION-INFO-CARRIER-056.
- expected system behavior:
  - `AppleBCMWLANIO80211APSTAInterface::start(core, RegistrationInfo*)` reads `numTxQueues` from `state+0x2a4`.
  - it builds a queue pointer list from `state+0x300` TX submission queues.
  - it appends TX completion queue `state+0x2e8` and RX completion queue `state+0x2f0`.
  - with APSTA init default `numTxQueues = 4`, the effective registration list has 6 entries.
  - it registers each TX submission queue and both completion queues with the work queue before calling `registerEthernetInterface`.
  - it calls `registerEthernetInterface(info, queueList, numTxQueues + 2, state+0x2d8, state+0x2e0, 0)`.
  - on registration failure it removes work sources for TX queues, TX completion, RX completion, and multicast queue.
- actual behavior:
  - local APSTA scaffold has queue storage fields but no compiled witness for the queue-list carrier consumed by `registerEthernetInterface`.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::start(AppleBCMWLANCore*, IOSkywalkEthernetInterface::RegistrationInfo*)`, disassembly range `0xffffff80016865ca..0xffffff8001686881`.
- evidence:
  - disasm: copies `state+0x300` TX queue pointers into a stack list using `numTxQueues * 8`.
  - disasm: writes `state+0x2e8` after the TX queue block and `state+0x2f0` after that.
  - disasm: loops over `state+0x300` and calls work queue vtable `+0x140` for each TX queue.
  - disasm: calls work queue vtable `+0x140` for `state+0x2e8` and `state+0x2f0`.
  - disasm: calls `registerEthernetInterface(info, queueList, numTxQueues + 2, txPool, rxPool, 0)`.
  - disasm: failure path removes work sources through work queue vtable `+0x148`.
- candidate causes:
  - confirmed: local APSTA scaffold lacks a queue-list carrier witness for APSTA Ethernet registration.
  - rejected: register only one primary-STA queue or collapse TX completion/RX completion into the primary STA list.
- confirmed deviation: APSTA registration queue-list carrier is not represented in compiled local code.
- root cause: confirmed for APSTA start/registration scaffold completeness only.
- fix:
  - add constants for APSTA extra completion queues and effective registration queue count.
  - add `AirportItlwmAPSTARegisterQueueListLayout` with four TX queues plus TX/RX completion queues.
  - add static asserts for completion queue offsets and size.
  - update YAML/prose docs.
  - keep runtime role-7 creation disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-130 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-REGISTER-ETHERNET-QUEUE-LIST-057
- symptom: APSTA queue-list registration carrier is recovered but absent from compiled local scaffolding.
- expected system behavior: local scaffolding records the exact APSTA queue list passed to `registerEthernetInterface`.
- actual behavior: queue storage exists, but queue-list carrier shape is not compiled.
- exact divergence point: APSTA `start(core, RegistrationInfo*)` queue registration and `registerEthernetInterface` call.
- evidence from runtime: no new runtime claim; this is static/disasm scaffold restoration.
- evidence from decomp: APSTA start disassembly range `0xffffff80016865ca..0xffffff8001686881`.
- exact semantic mismatch between reference and our code: reference registers a six-entry APSTA queue list for the default four-TX-queue topology; local code does not model that list.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the list is a direct argument to APSTA Ethernet registration.
- why proposed fix is 1:1 with reference architecture and semantics: it only encodes recovered carrier shape and constants; it does not register queues or enable APSTA.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - collapse APSTA into the primary STA one-queue topology.
  - omit TX completion or RX completion from the registration list.
  - register queues without modeling the failure cleanup path.
  - enable role-7 APSTA before queue creation and registration are complete.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-130.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA start disassembly directly constructs and registers the queue list.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA registration scaffold completeness: this list is a direct registration argument.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact queue-list carrier/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores missing APSTA queue-registration carrier operands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA start disassembly range and offsets are listed above.

## ANOMALY

- id: A-APSTA-SOFTAP-PUBLIC-CARRIER-CONTRACT-058
- status: CONFIRMED_DEVIATION
- symptom: APSTA owner scaffolding now has start/registration carriers, but several public SAP/SoftAP getter/setter carriers and the RSN reject gate are still not represented in compiled local witnesses.
- first visible manifestation: APSTA public/SAP surface audit after A-APSTA-REGISTER-ETHERNET-QUEUE-LIST-057.
- expected system behavior:
  - `getSTATE(...)` writes state value `4` at output offset `+0x04` and returns success.
  - `getPEER_CACHE_MAXIMUM_SIZE(...)` writes max peer count `8` at output offset `+0x04` and returns success.
  - `getHOST_AP_MODE_HIDDEN(...)` rejects NULL with raw `0x16`; non-NULL writes `1` at the output base.
  - `getSOFTAP_PARAMS(...)` copies APSTA state offsets `+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` into output offsets `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x16/+0x17/+0x18`.
  - `getSOFTAP_STATS(...)` copies `0x58` bytes from APSTA state `+0x1b0`.
  - `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` is feature-gate `0x46` controlled; when enabled it accepts only inputs whose byte `+0x03` is below `0x21`, copies exactly `0x24` bytes into state `+0x2c`, and otherwise returns `0xe00002c2`.
  - `setSOFTAP_TRIGGER_CSA(...)` returns `6` when AP is not up or reset flag bit 0 is not set, rejects NULL with raw `0x16`, accepts parsed chanspec values below `0x10000`, and rejects parsed chanspec values at or above `0x10000` with raw `0x16`.
  - `setRSN_CONF(...)` tests byte `state+0x29b`; when bit `0x10` is set it returns `0xe00002d5` instead of entering the normal RSN parse path.
  - `setSTA_AUTHORIZE(...)` rejects NULL with `0xe00002c2`; `setSTA_DEAUTH(...)` tailcalls the existing vtable slot at `+0x1040`.
- actual behavior:
  - local APSTA scaffold documents some of these contracts in prose/YAML, but no compiled witness records the SoftAP params output shape, Wi-Fi network info IE input shape, public constants, or the RSN gate byte at `state+0x29b`.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::getSTATE(...) @ 0xffffff8001687dfe`
  - `AppleBCMWLANIO80211APSTAInterface::getPEER_CACHE_MAXIMUM_SIZE(...) @ 0xffffff80016882da`
  - `AppleBCMWLANIO80211APSTAInterface::getHOST_AP_MODE_HIDDEN(...) @ 0xffffff80016882ea`
  - `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_TRIGGER_CSA(...) @ 0xffffff800168e0ae`
  - `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_WIFI_NETWORK_INFO_IE(...) @ 0xffffff800168e602`
  - `AppleBCMWLANIO80211APSTAInterface::getSOFTAP_PARAMS(...) @ 0xffffff800168e7f4`
  - `AppleBCMWLANIO80211APSTAInterface::getSOFTAP_STATS(...) @ 0xffffff800168e838`
  - `AppleBCMWLANIO80211APSTAInterface::setRSN_CONF(...) @ 0xffffff800168e85c`
  - `AppleBCMWLANIO80211APSTAInterface::setSTA_AUTHORIZE(...) @ 0xffffff800168f016`
  - `AppleBCMWLANIO80211APSTAInterface::setSTA_DEAUTH(...) @ 0xffffff800168f14c`
- evidence:
  - decomp: `getSTATE(...)` writes `*(param+4) = 4`.
  - decomp: `getPEER_CACHE_MAXIMUM_SIZE(...)` writes `*(param+4) = 8`.
  - decomp: `getHOST_AP_MODE_HIDDEN(...)` writes `*param = 1` for non-NULL and returns raw `0x16` for NULL.
  - decomp: `getSOFTAP_PARAMS(...)` copies the exact state/output offsets listed above.
  - decomp: `getSOFTAP_STATS(...)` calls memcpy from `self+0x130+0x1b0` for `0x58` bytes.
  - decomp: `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` checks feature `0x46`, tests `param+3 < 0x21`, copies `0x24`, and otherwise returns `0xe00002c2`.
  - decomp: `setSOFTAP_TRIGGER_CSA(...)` checks `state+0x26c`, bit `state+0x329 & 1`, NULL input, and parsed channel spec threshold `0x10000`.
  - decomp: `setRSN_CONF(...)` tests `(*(byte *)(state+0x29b) & 0x10)` and returns `0xe00002d5` when blocked.
  - decomp: `setSTA_AUTHORIZE(...)` returns `0xe00002c2` for NULL; `setSTA_DEAUTH(...)` calls vtable `+0x1040`.
- candidate causes:
  - confirmed: local APSTA scaffold lacks compiled carriers/constants for this public SoftAP/SAP surface and lacks named state byte `+0x29b`.
  - rejected: implement APSTA getters/setters with primary-STA guessed structures or generic unsupported return codes.
- confirmed deviation: reference public APSTA methods use fixed offsets and raw return codes not fully represented in local compiled scaffolding.
- root cause: confirmed for public APSTA/SoftAP scaffold completeness only; no claim is made that this alone fixes STA internet reachability.
- fix:
  - add APSTA public constants for state, peer cache, hidden mode, raw/IOKit return values, CSA threshold, and SoftAP network-info length limit.
  - add packed witness layouts for SoftAP params output and SoftAP Wi-Fi network-info IE carrier.
  - split `AirportItlwmAPSTAStateBlock` padding to name `rsnConfGate29b`.
  - add static asserts for all recovered offsets and carrier sizes.
  - update YAML/prose docs.
  - keep runtime role-7 APSTA creation disabled in this batch.
- verification:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-131 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-SOFTAP-PUBLIC-CARRIER-CONTRACT-058
- symptom: public APSTA/SoftAP getter/setter contracts are recovered but not compiled into local witnesses.
- expected system behavior: local APSTA scaffolding records the exact public carrier offsets, fixed values, reject codes, and RSN gate byte before implementing a real APSTA owner.
- actual behavior: several recovered contracts remain prose-only and `state+0x29b` is still anonymous padding.
- exact divergence point: APSTA public methods listed in A-APSTA-SOFTAP-PUBLIC-CARRIER-CONTRACT-058.
- evidence from runtime: no new runtime claim; this is a static/decomp layer restoration batch.
- evidence from decomp: APSTA decomp at addresses `0xffffff8001687dfe`, `0xffffff80016882da`, `0xffffff80016882ea`, `0xffffff800168e0ae`, `0xffffff800168e602`, `0xffffff800168e7f4`, `0xffffff800168e838`, `0xffffff800168e85c`, `0xffffff800168f016`, and `0xffffff800168f14c`.
- exact semantic mismatch between reference and our code: reference uses fixed public output/input offsets and return values; local scaffolding does not yet encode them and therefore cannot be used safely as an APSTA owner ABI base.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this layer, the missing constants/offsets are the exact ABI operands consumed by public SAP methods; the fix is not claiming final STA association/data root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it only records recovered layouts, constants, and state byte offsets; it does not synthesize AP success, force RSN, or call contested Wi-Fi methods.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - return generic unsupported for APSTA public methods.
  - fake AP/RSN/link state to satisfy UI.
  - implement SoftAP public methods against the primary STA owner.
  - ignore the `state+0x29b` RSN gate and always parse RSN_CONF.
  - add fallback/retry/replay/polling around APSTA public methods.
- verification plan:
  - compile-time offset static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-131.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: each constant, carrier offset, and `state+0x29b` bit check is from APSTA decomp.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for public APSTA/SAP ABI scaffold completeness; no final RSN/data root-cause claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, offsets, and layout witnesses only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores public APSTA ABI operands without changing runtime STA behavior.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA method addresses and offsets are listed above.

## ANOMALY

- id: A-APSTA-SAP-VTABLE-ALIAS-CONTRACT-059
- status: CONFIRMED_DEVIATION
- symptom: APSTA public method carriers are now recorded, but the SAP/APSTA vtable alias contract still does not distinguish the base `IO80211SapProtocol` extension slots from the concrete APSTA slot aliases.
- first visible manifestation: SAP protocol/class slot audit after A-APSTA-SOFTAP-PUBLIC-CARRIER-CONTRACT-058.
- expected system behavior:
  - the recovered `IO80211SapProtocol` vtable has slots `280..519`.
  - the base SAP extension starts at slot `481` / byte offset `0x0f08`.
  - base `IO80211SapProtocol` slot `483` / byte offset `0x0f18` points at `IO80211VirtualInterface::forwardPacket(IO80211NetworkPacket*)`.
  - concrete `AppleBCMWLANIO80211APSTAInterface` overrides `forwardPacket(IO80211NetworkPacket*)` at concrete slot `465` / byte offset `0x0e88`.
  - concrete APSTA slot `488` / byte offset `0x0f40` is `setMacAddress(ether_addr&)`.
  - concrete APSTA public SAP/SoftAP method slots `505..531` occupy byte offsets `0x0fc8..0x1098`.
  - `IO80211SapProtocol` base vtable stops at slot `519`; APSTA extends the concrete surface through slot `531`.
- actual behavior:
  - local `IO80211SapProtocol.h` records method slot constants and typedefs, but it lacks explicit base-vs-concrete alias constants and byte-offset guards.
  - without those guards, a later C++ class definition could place APSTA `forwardPacket` at the base virtual-interface slot `483` instead of the concrete APSTA slot `465`.
- divergence point:
  - `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt`
  - `IO80211SapProtocol vtable 0xffffff80023e8dc0 slots 280..519`
  - `AppleBCMWLANIO80211APSTAInterface vtable 0xffffff8001777508 slots 280..559`
- evidence:
  - vtable dump: base `IO80211SapProtocol` slot `481` is at `+0x0f08`.
  - vtable dump: base `IO80211SapProtocol` slot `483` is `IO80211VirtualInterface::forwardPacket(...)` at `+0x0f18`.
  - vtable dump: concrete APSTA slot `465` is `AppleBCMWLANIO80211APSTAInterface::forwardPacket(...)` at `+0x0e88`.
  - vtable dump: concrete APSTA slot `488` is `AppleBCMWLANIO80211APSTAInterface::setMacAddress(...)` at `+0x0f40`.
  - vtable dump: concrete APSTA slots `505..531` map to APSTA public SAP/SoftAP methods.
  - decomp: APSTA destructor calls `IO80211SapProtocol::~IO80211SapProtocol()`.
- candidate causes:
  - confirmed: local SAP header lacks compiled alias/byte-offset guards for base-vs-concrete APSTA slot placement.
  - rejected: define a C++ `IO80211SapProtocol` base class now using current primary-STA method ordering.
- confirmed deviation: SAP/APSTA slot aliases are known from reference but not fully encoded as compiled local constants/static asserts.
- root cause: confirmed for SAP/APSTA ABI scaffold completeness only.
- fix:
  - add explicit constants for base SAP vtable range, base virtual-interface `forwardPacket` slot, and concrete APSTA slot range.
  - add byte-offset constants for key APSTA/SAP slots.
  - add static asserts that preserve the slot collision distinction before any class definition is introduced.
  - update YAML/prose docs.
  - keep `IO80211SapProtocol` as a contract header, not a C++ base class, in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-132 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-SAP-VTABLE-ALIAS-CONTRACT-059
- symptom: SAP/APSTA vtable aliases are recovered but not compiled with enough guards to prevent wrong APSTA class layout.
- expected system behavior: local scaffolding distinguishes base `IO80211SapProtocol` extension slots from concrete `AppleBCMWLANIO80211APSTAInterface` slots.
- actual behavior: local header has method slot constants but lacks explicit base-vs-concrete alias constants and byte-offset assertions.
- exact divergence point: `IO80211SapProtocol` and `AppleBCMWLANIO80211APSTAInterface` vtable dumps listed in A-APSTA-SAP-VTABLE-ALIAS-CONTRACT-059.
- evidence from runtime: no new runtime claim; this is static ABI restoration.
- evidence from decomp: vtable dump plus APSTA destructor decomp calling `IO80211SapProtocol` destructor.
- exact semantic mismatch between reference and our code: reference APSTA `forwardPacket` is concrete slot `465`, while base virtual-interface forwardPacket remains slot `483`; local code does not yet guard this distinction.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this ABI layer, wrong slot placement would make the future APSTA owner call the wrong method; the vtable entries are direct dispatch targets.
- why proposed fix is 1:1 with reference architecture and semantics: it only records recovered slot/byte-offset aliases and keeps the class undefined until the slot map is safe.
- files/functions to modify:
  - `include/Airport/IO80211SapProtocol.h`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - define the SAP base class now.
  - move APSTA `forwardPacket` to slot `483`.
  - collapse APSTA into the primary STA Skywalk vtable.
  - implement thunks without recording exact slot aliases.
  - use guessed vtable padding.
- verification plan:
  - compile-time static asserts.
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-132.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: vtable dump and APSTA destructor decomp directly identify the SAP/APSTA relation and slot aliases.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for ABI scaffold completeness; no final RSN/data root-cause claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact slot and byte-offset constants/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it prevents a known wrong APSTA owner ABI without changing runtime behavior.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact vtable slots, byte offsets, and destructor relation are listed above.

## ANOMALY

- id: A-APSTA-FORWARD-PACKET-TX-QUEUE-SELECTION-060
- status: CONFIRMED_DEVIATION
- symptom: SAP/APSTA vtable aliases are guarded, but the concrete APSTA `forwardPacket` datapath semantics are still not compiled into the local APSTA witness.
- first visible manifestation: APSTA datapath audit after A-APSTA-SAP-VTABLE-ALIAS-CONTRACT-059.
- expected system behavior:
  - `AppleBCMWLANIO80211APSTAInterface::forwardPacket(IO80211NetworkPacket*)` is the concrete slot-465 APSTA forward path.
  - it calls a packet metadata helper on the `IO80211NetworkPacket`.
  - it selects a TX subqueue pointer from APSTA state using `state+0x300 + ((metadata >> 4) & 0xff8)`.
  - it calls the selected queue's vtable entry `+0x318` with the packet.
- actual behavior:
  - local APSTA state block has `txSubQueues` at `state+0x300`, but no compiled constants record the packet metadata shift, selector mask, or queue vtable call offset.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::forwardPacket(IO80211NetworkPacket*) @ 0xffffff8001693940`.
- evidence:
  - decomp: `uVar2 = FUN_ffffff8002a341a8(param_2);`
  - decomp: `plVar1 = *(long **)(*(long *)(param_1 + 0x130) + 0x300 + (ulong)(uVar2 >> 4 & 0xff8));`
  - decomp: `(**(code **)(*plVar1 + 0x318))(plVar1,param_2);`
  - vtable dump: concrete APSTA `forwardPacket` is slot `465` / byte offset `0x0e88`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks forwardPacket queue-selection constants.
  - rejected: route APSTA packets through primary STA single-queue forwarding or guessed AC mapping.
- confirmed deviation: APSTA forwardPacket queue-selection semantics are recovered but not represented locally.
- root cause: confirmed for APSTA TX datapath scaffold completeness only.
- fix:
  - add constants for metadata shift `4`, selector mask `0xff8`, TX subqueue base offset `0x300`, and selected queue vtable offset `0x318`.
  - assert that the forwardPacket TX subqueue base constant matches `AirportItlwmAPSTAStateBlock::txSubQueues`.
  - update YAML/prose docs.
  - keep runtime APSTA forwarding disabled in this batch.
- verification:
  - compile-time static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-133 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-FORWARD-PACKET-TX-QUEUE-SELECTION-060
- symptom: APSTA forwardPacket queue-selection contract is recovered but absent from compiled local APSTA scaffolding.
- expected system behavior: local APSTA scaffolding records the exact packet metadata shift/mask and selected queue vtable offset used by reference APSTA forwarding.
- actual behavior: local code only records the TX subqueue array, not how `forwardPacket` indexes it.
- exact divergence point: APSTA `forwardPacket(IO80211NetworkPacket*) @ 0xffffff8001693940`.
- evidence from runtime: no new runtime claim; this is static APSTA TX datapath restoration.
- evidence from decomp: APSTA `forwardPacket` decomp listed in A-APSTA-FORWARD-PACKET-TX-QUEUE-SELECTION-060.
- exact semantic mismatch between reference and our code: reference derives the TX subqueue pointer from packet metadata using `>> 4` and `& 0xff8`, then calls queue vtable `+0x318`; local scaffold does not record those operands.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this TX datapath layer, those operands are the direct dispatch path for APSTA transmitted packets.
- why proposed fix is 1:1 with reference architecture and semantics: it only records recovered constants and asserts the state offset; it does not transmit packets, retry, reorder, or force success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - send APSTA packets through the primary STA forwarder.
  - select TX queue by guessed AC mapping in `forwardPacket`.
  - clamp or fallback to queue zero without reference evidence.
  - add retry/poll/replay around queue submission.
- verification plan:
  - compile-time static asserts.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-133.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA `forwardPacket` decomp directly shows the metadata helper, shift, mask, state offset, and queue vtable offset.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA TX datapath scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records the APSTA forwarding path without enabling it.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA `forwardPacket` function and operands are listed above.

## ANOMALY

- id: A-APSTA-DATAPATH-METRIC-ACCESSORS-061
- status: CONFIRMED_DEVIATION
- symptom: APSTA forwardPacket queue selection is recorded, but APSTA datapath metric accessors still lack compiled constants for queue-internal depth/capacity operands.
- first visible manifestation: APSTA datapath accessor audit after A-APSTA-FORWARD-PACKET-TX-QUEUE-SELECTION-060.
- expected system behavior:
  - `getTxHeadroom()` returns `0`.
  - `getTxQueueDepth()` reads the first APSTA TX subqueue from `state+0x300`; if missing it returns `0`.
  - when the TX subqueue exists, `getTxQueueDepth()` reads a nested object pointer at `queue+0x168` and returns dword `+0x28` from that nested object.
  - `getRxQueueCapacity()` reads RX completion queue from `state+0x2f0`; if missing it returns `0`.
  - when the RX completion queue exists, `getRxQueueCapacity()` reads a nested object pointer at `queue+0x138` and returns dword `+0x10` from that nested object.
- actual behavior:
  - YAML/prose docs record these accessor semantics, but compiled APSTA scaffolding lacks constants for the nested queue offsets and missing-queue zero value.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::getTxHeadroom() @ 0xffffff800169411e`
  - `AppleBCMWLANIO80211APSTAInterface::getTxQueueDepth() @ 0xffffff8001694126`
  - `AppleBCMWLANIO80211APSTAInterface::getRxQueueCapacity() @ 0xffffff800169414e`
- evidence:
  - decomp: `getTxHeadroom()` returns `0`.
  - decomp: `getTxQueueDepth()` loads `*(state+0x300)`, returns `0` when NULL, otherwise returns `*(uint32_t *)(*(queue+0x168)+0x28)`.
  - decomp: `getRxQueueCapacity()` loads `*(state+0x2f0)`, returns `0` when NULL, otherwise returns `*(uint32_t *)(*(queue+0x138)+0x10)`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks nested queue metric offset constants.
  - rejected: use generic queue capacity APIs or guessed local queue counters for APSTA metrics.
- confirmed deviation: APSTA metric accessor operands are recovered but not represented locally.
- root cause: confirmed for APSTA datapath metric scaffold completeness only.
- fix:
  - add constants for zero headroom/missing metric, TX queue nested object offset `0x168`, TX depth dword `0x28`, RX queue nested object offset `0x138`, and RX capacity dword `0x10`.
  - update YAML/prose docs.
  - keep runtime APSTA datapath disabled in this batch.
- verification:
  - compile-time static asserts and header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-134 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-DATAPATH-METRIC-ACCESSORS-061
- symptom: APSTA datapath metric accessors are documented but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact zero/default and nested queue offsets used by reference metric accessors.
- actual behavior: local code has APSTA queue fields but not queue-internal metric operands.
- exact divergence point: APSTA `getTxHeadroom`, `getTxQueueDepth`, and `getRxQueueCapacity` methods listed in A-APSTA-DATAPATH-METRIC-ACCESSORS-061.
- evidence from runtime: no new runtime claim; this is static APSTA datapath restoration.
- evidence from decomp: APSTA metric accessor decomp listed above.
- exact semantic mismatch between reference and our code: reference reads nested queue objects at `+0x168/+0x138` and dwords at `+0x28/+0x10`; local scaffold does not record those operands.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this accessor layer, these operands are the exact returned metrics.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not call unexported queue APIs or synthesize live metrics.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - call generic queue capacity APIs.
  - invent local queue metrics not present in APSTA decomp.
  - return fixed nonzero capacity/depth.
  - enable APSTA datapath before owner/queue allocation is restored.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-134.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA metric accessor decomp directly shows each offset.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for metric accessor scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records APSTA metric operands without enabling the datapath.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact functions and offsets are listed above.

## ANOMALY

- id: A-APSTA-DATAPATH-LIFECYCLE-VTABLE-OFFSETS-062
- status: CONFIRMED_DEVIATION
- symptom: APSTA datapath storage and metrics are represented, but the exact enable/disable lifecycle vtable offsets remain prose-only.
- first visible manifestation: APSTA datapath lifecycle audit after A-APSTA-DATAPATH-METRIC-ACCESSORS-061.
- expected system behavior:
  - `enableDatapath()` calls the datapath owner at `state+0x2d0` vtable `+0x120`.
  - `enableDatapath()` starts TX completion queue `state+0x2e8` via queue vtable `+0x150`.
  - `enableDatapath()` starts RX completion queue `state+0x2f0` via queue vtable `+0x150`.
  - `enableDatapath()` arms RX completion queue via queue vtable `+0x298` with arguments `0, 0`.
  - missing TX or RX completion queue returns `0xe00002bc`.
  - `disableDatapath()` calls the datapath owner at `state+0x2d0` vtable `+0x128`.
  - `disableDatapath()` stops RX completion queue `state+0x2f0` via queue vtable `+0x158`, then stops TX completion queue `state+0x2e8` via queue vtable `+0x158`.
- actual behavior:
  - local APSTA scaffold has `datapathOwner2d0`, `txCompQueue`, and `rxCompQueue`, but lacks compiled constants for the owner lifecycle vtable offsets and queue start/stop/arm offsets.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::enableDatapath() @ 0xffffff8001693b82`
  - `AppleBCMWLANIO80211APSTAInterface::disableDatapath() @ 0xffffff8001693e80`
- evidence:
  - decomp: enable calls `(**(state+0x2d0 vtable +0x120))(owner, self)`.
  - decomp: enable calls `(**(state+0x2e8 vtable +0x150))()` and `(**(state+0x2f0 vtable +0x150))()`.
  - decomp: enable calls `(**(state+0x2f0 vtable +0x298))(rxCompQueue, 0, 0)`.
  - decomp: enable returns `0xe00002bc` on missing completion queues.
  - decomp: disable calls `(**(state+0x2d0 vtable +0x128))(owner, self)`.
  - decomp: disable calls stop vtable `+0x158` on RX completion first and TX completion second.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks lifecycle vtable offset constants.
  - rejected: start/stop queues directly from primary STA workloop without APSTA datapath owner.
- confirmed deviation: APSTA datapath lifecycle vtable operands are recovered but not represented locally.
- root cause: confirmed for APSTA datapath lifecycle scaffold completeness only.
- fix:
  - add constants for datapath owner enable/disable vtable offsets `0x120/0x128`.
  - add constants for completion queue start/stop offsets `0x150/0x158`.
  - add constant for RX completion arm offset `0x298`.
  - add constants for arm arguments `0,0` and missing queue return `0xe00002bc`.
  - update YAML/prose docs.
  - keep runtime APSTA datapath disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-135 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-DATAPATH-LIFECYCLE-VTABLE-OFFSETS-062
- symptom: APSTA datapath lifecycle offsets are documented but not compiled as contract constants.
- expected system behavior: local APSTA scaffold records exact owner/queue vtable offsets for enable/disable lifecycle.
- actual behavior: local code has storage fields but not lifecycle vtable operands.
- exact divergence point: APSTA `enableDatapath` and `disableDatapath` methods listed above.
- evidence from runtime: no new runtime claim; this is static APSTA datapath restoration.
- evidence from decomp: APSTA datapath lifecycle decomp listed in A-APSTA-DATAPATH-LIFECYCLE-VTABLE-OFFSETS-062.
- exact semantic mismatch between reference and our code: reference sequences owner enable/disable and completion queue start/stop/arm through fixed vtable offsets; local scaffold does not record those offsets.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this lifecycle layer, those vtable offsets are the direct queue/owner transition calls.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not start queues, add retries, or force datapath success.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - bypass datapath owner `state+0x2d0`.
  - start only one completion queue.
  - ignore RX arm vtable `+0x298`.
  - treat missing queues as success.
  - add retry/poll around queue start.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-135.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA enable/disable datapath decomp directly shows each vtable offset.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for datapath lifecycle scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records lifecycle operands without enabling APSTA datapath.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact enable/disable functions and vtable offsets are listed above.

## ANOMALY

- id: A-APSTA-START-WORKSOURCE-REGISTRATION-063
- status: CONFIRMED_DEVIATION
- symptom: APSTA `start` queue-list and datapath lifecycle operands are represented, but the work-source registration/cleanup contract around `registerEthernetInterface` remains partially prose-only.
- first visible manifestation: APSTA `start(core, RegistrationInfo*)` audit after A-APSTA-DATAPATH-LIFECYCLE-VTABLE-OFFSETS-062.
- expected system behavior:
  - APSTA `start` calls datapath owner `state+0x2d0` vtable `+0x118` with the queue config carrier.
  - APSTA `start` treats `numTxQueues >= 7` as an invalid/trap path before building the registration queue list.
  - if multicast queue `state+0x320` exists, APSTA adds it to work queue `state+0x330` before Ethernet registration.
  - it adds each TX submission queue from `state+0x300` to the work queue through vtable `+0x140`.
  - it adds TX completion `state+0x2e8` and RX completion `state+0x2f0` through the same work queue vtable `+0x140`.
  - it calls `registerEthernetInterface(info, queueList, numTxQueues + 2, state+0x2d8, state+0x2e0, 0)`.
  - if registration fails, it removes TX queues, TX completion, RX completion, and multicast through work queue vtable `+0x148` when present.
- actual behavior:
  - local APSTA scaffold has the queue config and queue list carrier, but compiled constants do not yet record owner config vtable `+0x118`, work queue add/remove offsets `+0x140/+0x148`, the TX queue trap threshold, or the register call flags.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::start(AppleBCMWLANCore*, RegistrationInfo*)`, disassembly `0xffffff8001686378..0xffffff800168689d`.
- evidence:
  - disasm: calls `state+0x2d0` vtable `+0x118` with the stack queue config carrier.
  - disasm: `cmpq $0x7, %r13; jae ...` before copying TX queues.
  - disasm: optional multicast work-source add before queue-list assembly.
  - disasm: loops over TX queues and calls work queue vtable `+0x140`.
  - disasm: calls vtable `+0x140` for `state+0x2e8` and `state+0x2f0`.
  - disasm: computes count `numTxQueues + 2` and passes final zero argument to `registerEthernetInterface`.
  - disasm: on nonzero registration result, removes TX queues, TX/RX completion queues, and multicast using work queue vtable `+0x148`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks start-time work-source registration/cleanup constants.
  - rejected: register Ethernet queues without work-source membership.
- confirmed deviation: APSTA start work-source registration and cleanup operands are recovered but not represented locally.
- root cause: confirmed for APSTA start scaffold completeness only.
- fix:
  - add constants for datapath owner queue-config vtable offset `0x118`.
  - add constants for work queue add/remove vtable offsets `0x140/0x148`.
  - add constants for TX queue trap threshold `7`, maximum accepted TX queues `6`, and register flags `0`.
  - update YAML/prose docs.
  - keep runtime APSTA `start` disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-136 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-START-WORKSOURCE-REGISTRATION-063
- symptom: APSTA start work-source add/remove and register flags are recovered but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact vtable offsets/order/limits for start-time work-source membership and cleanup.
- actual behavior: local code has queue storage and carriers but not these start-time operands.
- exact divergence point: APSTA `start` disassembly `0xffffff8001686378..0xffffff800168689d`.
- evidence from runtime: no new runtime claim; this is static APSTA start restoration.
- evidence from decomp: APSTA start disassembly listed in A-APSTA-START-WORKSOURCE-REGISTRATION-063.
- exact semantic mismatch between reference and our code: reference registers queues as work sources before Ethernet registration and removes them on failure through fixed work queue vtable offsets; local scaffold does not record those operands.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this start layer, these operands are the direct lifecycle calls around Ethernet registration.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not allocate APSTA, add work sources, call registration, or mask failures.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - skip work-source add before Ethernet registration.
  - skip failure cleanup.
  - treat registration failure as success.
  - infer a different queue-count limit.
  - enable APSTA `start` before the full owner class is implemented.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-136.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA start disassembly directly shows config, add/remove, count, and register operands.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA start scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records start operands without enabling APSTA runtime registration.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact APSTA start disassembly range and offsets are listed above.

## ANOMALY

- id: A-APSTA-TEARDOWN-RESOURCE-CLEANUP-064
- status: CONFIRMED_DEVIATION
- symptom: APSTA start/work-source registration operands are represented, but the matching stop/freeResources cleanup contract remains partially prose-only.
- first visible manifestation: APSTA teardown audit after A-APSTA-START-WORKSOURCE-REGISTRATION-063.
- expected system behavior:
  - `freeResources()` cancels timer sources at `state+0x70` and `state+0x78` through vtable `+0x158`, releases them through vtable `+0x28`, and clears both fields.
  - `freeResources()` releases retained init resources at `state+0x240`, `+0x248`, `+0x250`, `+0x260`, and `+0x258` through vtable `+0x28` when present and clears each field.
  - `stop(IOService*)` iterates `state+0x300 + i*8` while `i < state+0x2a4`, stops each TX queue through vtable `+0x158`, removes it from the work queue through vtable `+0x148`, releases it through vtable `+0x28` if still present, and clears the slot.
  - `stop(IOService*)` applies the same stop/remove/release/clear sequence to TX completion `state+0x2e8` and RX completion `state+0x2f0`.
  - `stop(IOService*)` stops multicast queue `state+0x320`, removes it through work queue vtable `+0x148`, calls the direct `IO80211WorkQueue::removeWorkSource` path, releases it, clears the field, and then tailcalls the super stop vtable offset `+0x5d8`.
- actual behavior:
  - local APSTA scaffold has queue/timer/resource fields and start-time add/remove constants, but lacks compiled teardown offset aliases for stop/release/null and the exact stop/freeResources cleanup topology.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::freeResources() @ 0xffffff8001685d64..0xffffff8001685e92`
  - `AppleBCMWLANIO80211APSTAInterface::stop(IOService*) @ 0xffffff8001686a7e..0xffffff8001686c91`
- evidence:
  - decomp/disasm: `freeResources()` uses timer vtable `+0x158`, release vtable `+0x28`, and clears `state+0x70/+0x78/+0x240/+0x248/+0x250/+0x260/+0x258`.
  - decomp/disasm: `stop(IOService*)` loops over `state+0x300` bounded by byte `state+0x2a4`; stop vtable `+0x158`, work queue remove vtable `+0x148`, release vtable `+0x28`, and NULL clear are all visible.
  - disasm: multicast cleanup at `state+0x320` includes both work queue vtable `+0x148` and direct `IO80211WorkQueue::removeWorkSource` call before release/clear.
  - disasm: final tailcall uses super vtable offset `+0x5d8`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks teardown constants tying stop/freeResources to the recovered fields and vtable offsets.
  - rejected: rely on primary STA queue teardown for APSTA queues.
  - rejected: skip direct multicast remove because the start path already removes via vtable on registration failure.
- confirmed deviation: APSTA teardown operands are recovered but not represented as compiled local witnesses.
- root cause: confirmed for APSTA teardown scaffold completeness only.
- fix:
  - add constants for object release, timer cancel, queue stop, work queue remove, direct multicast-remove marker, super stop offset, teardown NULL value, freeResources offsets, and stop queue offsets.
  - add static asserts tying those constants to `AirportItlwmAPSTAStateBlock` fields.
  - update YAML/prose docs and save a local reference teardown snippet.
  - keep runtime APSTA stop/freeResources disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-137 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-TEARDOWN-RESOURCE-CLEANUP-064
- symptom: APSTA teardown offsets are recovered but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact stop/freeResources release, remove, clear, and super-tailcall operands used by reference.
- actual behavior: local code has fields and start-time registration constants, but not teardown operand witnesses.
- exact divergence point: APSTA `freeResources()` and `stop(IOService*)` ranges listed above.
- evidence from runtime: no new runtime claim; this is static APSTA teardown restoration.
- evidence from decomp: APSTA teardown decomp/disasm listed in A-APSTA-TEARDOWN-RESOURCE-CLEANUP-064.
- exact semantic mismatch between reference and our code: reference uses fixed vtable offsets and field cleanup order for APSTA timers/resources/queues; local scaffold does not record those operands.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this teardown layer, those operands are the direct reference cleanup actions for every APSTA timer/resource/queue field in scope.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not invoke teardown, add fallback cleanup, or mask leaks.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_teardown_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - use primary STA teardown for APSTA queues.
  - skip direct multicast `removeWorkSource` because the vtable remove exists.
  - infer extra release/reset fields not present in the teardown disassembly.
  - call stop/freeResources before APSTA owner construction is implemented.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-137.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA `freeResources()` and `stop(IOService*)` decomp/disasm directly show each field and vtable offset.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for teardown scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records cleanup operands without enabling APSTA runtime teardown.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact teardown function ranges and offsets are listed above.

## ANOMALY

- id: A-APSTA-RESET-SOFTAP-DEFAULTS-065
- status: CONFIRMED_DEVIATION
- symptom: APSTA teardown is represented, but reset/initSoftAPParameters defaults and reset-time timer/stat cleanup remain partially prose-only.
- first visible manifestation: APSTA lifecycle audit after A-APSTA-TEARDOWN-RESOURCE-CLEANUP-064.
- expected system behavior:
  - `reset()` clears `state+0x26c` and byte `state+0x329`.
  - `reset()` calls `AppleBCMWLANCore::setConcurrencyState(4, false)`.
  - `reset()` zeroes `state+0xb8` for `0xf0` bytes, clears `state+0x0` and `state+0xb0`, calls `setPowerSaveState(0, 0xa)`, invokes timer sources `state+0x70/+0x78` through vtable `+0x218`, clears stats `state+0x1b0..+0x207`, and clears runtime qwords `state+0x90/+0x98/+0xa0`.
  - `initSoftAPParameters()` clears stats `state+0x1b0..+0x207`, clears `state+0x1a8`, zeroes `state+0xb8` for `0xf0` bytes, clears `state+0x0`, writes DTIM/default fields `state+0x16 = 1`, `state+0x18 = 0xf`, `state+0x1c = 0x1e`, `state+0x20 = 0x708`, `state+0x24 = 0xa`, and `state+0x28 = 3`.
  - `initSoftAPParameters()` calls `setBeaconInterval(state+0x14)` and, when `state+0x16 != state+0x6a`, sends or runs IOCTL `0x4e` through commander `state+0x228` before writing applied DTIM `state+0x6a = state+0x16` on success.
- actual behavior:
  - local APSTA scaffold has broad runtime/stat buffers but lacks compiled field names/constants for reset defaults, DTIM/applied-DTIM, reset timer action, and initSoftAPParameters IOCTL `0x4e`.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::reset() @ 0xffffff8001686cc6..0xffffff8001686e32`
  - `AppleBCMWLANIO80211APSTAInterface::initSoftAPParameters() @ 0xffffff8001687888..0xffffff8001687ade`
- evidence:
  - disasm: `reset()` writes `0` to `state+0x26c/+0x329/+0x0/+0xb0`, zeroes `state+0xb8` size `0xf0`, calls `setPowerSaveState(0,0xa)`, calls timer vtable `+0x218`, and clears stats/runtime qwords.
  - disasm: `initSoftAPParameters()` clears stats qwords `+0x1b0..+0x200`, clears `+0x1a8`, zeroes `+0xb8` size `0xf0`, writes default qwords at `+0x18/+0x20`, writes `+0x16` and `+0x28`, calls `setBeaconInterval`, and uses IOCTL `0x4e` for DTIM period update when `+0x16 != +0x6a`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks reset/default constants and named fields needed for AP/SoftAP parameter initialization.
  - rejected: derive AP/SoftAP defaults from local primary STA defaults.
  - rejected: skip DTIM/applied-DTIM because AP runtime is not enabled yet.
- confirmed deviation: APSTA reset/initSoftAPParameters operands are recovered but not fully represented locally.
- root cause: confirmed for APSTA reset/default scaffold completeness only.
- fix:
  - name the recovered state fields at `+0x0`, `+0x16`, `+0x6a`, `+0x90`, `+0x98`, and `+0xa0`.
  - add constants for reset/default offsets, sizes, values, timer action vtable `+0x218`, concurrency/power-save arguments, and DTIM IOCTL `0x4e`.
  - add static asserts tying constants to local state layout.
  - update YAML/prose docs and save a local reference note.
  - keep runtime APSTA reset/initSoftAPParameters disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-138 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-RESET-SOFTAP-DEFAULTS-065
- symptom: APSTA reset/initSoftAPParameters defaults are recovered but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact reset clears, SoftAP defaults, DTIM/applied-DTIM fields, timer action, and IOCTL operand used by reference.
- actual behavior: local code has broad reserved buffers and earlier prose notes but not the compiled field/default witnesses.
- exact divergence point: APSTA `reset()` and `initSoftAPParameters()` ranges listed above.
- evidence from runtime: no new runtime claim; this is static APSTA lifecycle/default restoration.
- evidence from decomp: APSTA reset/default disassembly listed in A-APSTA-RESET-SOFTAP-DEFAULTS-065.
- exact semantic mismatch between reference and our code: reference writes specific AP/SoftAP defaults and reset clears at fixed offsets; local scaffold leaves several of those offsets anonymous and lacks the constants.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this lifecycle/default layer, these operands are the direct reference writes/calls that initialize APSTA SoftAP state.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not run AP initialization, synthesize defaults dynamically, or force AP state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_reset_init_softap_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer AP defaults from primary STA code.
  - skip DTIM/applied-DTIM state because AP runtime is still disabled.
  - call `initSoftAPParameters()` before APSTA owner construction is implemented.
  - force AP state or concurrency state at runtime.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-138.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA `reset()` and `initSoftAPParameters()` disassembly directly shows fields, defaults, sizes, vtable offset, and IOCTL.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for reset/default scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, field names, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records default/reset operands without enabling APSTA runtime initialization.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function ranges and disassembly operands are listed above.

## ANOMALY

- id: A-CR167-RX-PRODUCER-TAG-STATS-CLOSURE
- status: FIX_IMPLEMENTED
- symptom: after RX pending-producer restoration, the active RX completion producer still omitted the reference tag-carrier and post-batch accounting edges.
- first visible manifestation: static/decomp audit of the CR-166 RX producer before after-fix runtime.
- expected system behavior:
  - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...)` drains owner-side staged RX packets.
  - it passes packet scratch/tag into the interface input slot.
  - it maps tag TID offset `+0x18` into service class offset `+0x29`.
  - it fills the Skywalk-provided produced packet array.
  - after the batch it calls `IO80211SkywalkInterface::recordInputPacket(int, int)` and then the RX-counter virtual slot.
- actual behavior:
  - local `skywalkRxAction(...)` created tag metadata only immediately before `inputPacket(...)`.
  - local RX pending ring carried only the packet pointer.
  - local `skywalkRxAction(...)` did not call `recordInputPacket(...)` or `updateRxCounter(...)` after producing packets.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction(...)`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxStagePendingPacket(...)`
  - reference `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @ 0xffffff80014ca8e4`
- evidence:
  - panic logs: none for this request.
  - runtime logs: current live driver is not CR-166/CR-167, so no after-fix runtime claim is made.
  - ioreg: not used for this static batch.
  - packet traces: not used for this static batch.
  - firmware traces: not used for this static batch.
  - decomp: Apple producer reads packet scratch/tag, maps `+0x18` to `+0x29`, calls input, stores produced packets, then calls `recordInputPacket(int,int) @ 0xffffff8002277c96` and interface RX-counter slot.
  - docs: `docs/reference/AppleBCMWLAN_rx_producer_tag_stats_2026_04_27.md`, YAML `108_rx_producer_tag_stats_2026_04_27.yaml`.
- candidate causes:
  - confirmed: local RX pending producer lacked a staged metadata carrier.
  - confirmed: local RX producer lacked post-batch IO80211 RX accounting.
  - rejected: raw write to `packet+0x78`; Tahoe generic `IOSkywalkNetworkPacket` class size is `0x78`, so Apple PCIe scratch storage is subclass-specific.
- confirmed deviation: tag carrier and RX accounting edges were missing.
- root cause: confirmed for RX producer semantic incompleteness. This batch does not claim final RSN/DHCP/data root cause.
- fix:
  - add `fRxPendingTags[]` and `fRxPendingLengths[]` next to `fRxPendingPackets[]`.
  - stage tag/length with each RX pending packet.
  - pop staged tag/length with the packet in `skywalkRxAction(...)`.
  - call `recordInputPacket(produced, producedBytes)` and `updateRxCounter(produced)` after produced RX batches.
  - document the local packet-class constraint and new YAML layer.
- verification:
  - pending build and CR-167 request.
- notes:
  - no retry, replay, duplicate notify, forced accepted success, forced EAPOL TX, key, RSN, DHCP, link state, or deauth masking was added.

## FIX_CANDIDATE

- anomaly_id: A-CR167-RX-PRODUCER-TAG-STATS-CLOSURE
- symptom: CR-166 RX producer still lacked reference tag-carrier and post-batch RX accounting edges.
- expected system behavior: RX completion producer carries tag metadata with the produced packet, calls input once per produced packet, fills the produced packet array, and updates IO80211 RX accounting once per batch.
- actual behavior: local producer carried only packet pointers and returned produced packets without `recordInputPacket/updateRxCounter`.
- exact divergence point: `skywalkRxAction(...)` and `skywalkRxStagePendingPacket(...)` vs. Apple RX completion producer at `0xffffff80014ca8e4`.
- evidence from runtime: current loaded driver is not this candidate; runtime verification remains pending after Stage 1.
- evidence from decomp: Apple producer body and IO80211 symbol evidence listed above.
- exact semantic mismatch between reference and our code: local RX producer omitted staged tag metadata and the post-batch RX accounting calls.
- fix justification path: SYSTEM_CONTRACT_FIX plus REFERENCE_ALIGNMENT_FIX for the exported accounting calls.
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: RX pending producer record, inputPacket tag argument, produced packet array, `recordInputPacket`, `updateRxCounter`.
  - expected contract at each touchpoint: tag metadata accompanies the packet through producer action; input is called once; produced packet is returned once; accounting is updated once per batch with count and byte total.
  - why no relevant touchpoints are missing: scope is limited to RX completion producer handoff/accounting; downstream EAPOL TX, key, RSN, DHCP, data and link state are non-claims.
  - why proposed path adds no extra system-visible side effects: no packet replay, retry, delay, forced state, accepted-success forcing, or masking.
- why this is root cause and not just correlation: the missing tag/accounting edges are direct producer operands in reference; this fixes producer semantic incompleteness before the next runtime.
- why proposed fix is 1:1 with reference architecture and semantics: it restores owner-side metadata staging and the reference accounting edge while avoiding unsafe subclass-field synthesis.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `include/Airport/IO80211SkywalkInterface.h`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_rx_completion_input_handoff_2026_04_27.md`
  - `docs/reference/AppleBCMWLAN_rx_producer_tag_stats_2026_04_27.md`
  - `docs/tahoe_signal_chain_audit.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/108_rx_producer_tag_stats_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
- forbidden alternative fixes considered and rejected:
  - raw `packet+0x78` scratch pointer write: rejected because generic packet size is `0x78`.
  - call input from `skywalkRxInput(...)`: rejected because reference calls from the RX producer action.
  - force accepted success, EAPOL TX, key, RSN, DHCP or link state: rejected by protocol and reference scope.
  - add retry/replay/delay/poll around RX: rejected by protocol.
- verification plan:
  - `git diff --check`.
  - YAML parse for new/changed YAML.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - create CR-167 superseding CR-166.

## ANOMALY

- id: A-APSTA-BEACON-DTIM-IOCTL-CARRIERS-066
- status: CONFIRMED_DEVIATION
- symptom: APSTA reset/defaults record `setBeaconInterval` and DTIM apply entry points, but the exact IOCTL carrier/callback operands are not compiled.
- first visible manifestation: APSTA beacon/DTIM audit after A-APSTA-RESET-SOFTAP-DEFAULTS-065.
- expected system behavior:
  - `setBeaconInterval(uint16_t)` compares the requested value to applied beacon interval `state+0x68`; if equal, it returns without IOCTL.
  - when different, it builds a 4-byte payload head, uses commander `state+0x228`, and sends or runs IOCTL `0x4c`.
  - async path installs `handleSetBcnIntervalAsyncCallBack` in callback context slot `+0x8`; sync path passes no callback.
  - on success, `setBeaconInterval` writes applied beacon interval `state+0x68 = requested`.
  - `initSoftAPParameters()` uses the same 4-byte payload head with IOCTL `0x4e` for DTIM period and writes applied DTIM `state+0x6a = state+0x16` on success.
  - both async callbacks ignore status `0`, log nonzero status through the interface logger, then emit bytestream telemetry from `CommandRxPayload` pointer `+0x0`, length `+0x8`, through `state+0x210`.
- actual behavior:
  - local APSTA scaffold has state fields for `+0x68/+0x6a` and default constants, but lacks compiled constants/layout witness for the IOCTL numbers, 4-byte payload head, callback payload offsets, and callback telemetry contract.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setBeaconInterval(uint16_t) @ 0xffffff8001687ae4..0xffffff8001687c7e`
  - `AppleBCMWLANIO80211APSTAInterface::handleSetBcnIntervalAsyncCallBack(...) @ 0xffffff800169365a..0xffffff800169370b`
  - `AppleBCMWLANIO80211APSTAInterface::handleSetBcnDTIMPeriodAsyncCallBack(...) @ 0xffffff800169370e..0xffffff80016937bf`
- evidence:
  - disasm: `setBeaconInterval` compares `%r14w` with `state+0x68`, prepares stack payload pointer and length `4`, uses commander `state+0x228`, and calls IOCTL `0x4c`.
  - disasm: async beacon path writes callback function pointer at callback context `+0x8`; sync path passes NULL callback arguments.
  - disasm: success path writes requested value to `state+0x68`.
  - disasm: DTIM path in `initSoftAPParameters` prepares the same payload length `4`, calls IOCTL `0x4e`, and writes `state+0x6a` on success.
  - disasm: both callbacks read bytestream pointer at callback payload `+0x0`, length at `+0x8`, and call bytestream logging through `state+0x210`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks beacon/DTIM IOCTL carrier and callback payload constants.
  - rejected: use generic AP parameter setters without per-IOCTL payload/callback ABI.
  - rejected: write `state+0x68/+0x6a` without confirmed IOCTL success path.
- confirmed deviation: APSTA beacon/DTIM IOCTL operands are recovered but not represented locally.
- root cause: confirmed for APSTA beacon/DTIM scaffold completeness only.
- fix:
  - add constants for IOCTL `0x4c`, IOCTL `0x4e`, payload size `4`, callback context offset `+0x8`, callback rxPayload pointer/length offsets, bytestream enable flag, and applied beacon/DTIM field offsets.
  - add a command payload head layout witness.
  - rename `state+0x68` field to applied beacon interval.
  - update YAML/prose docs and save a local reference note.
  - keep runtime APSTA beacon/DTIM setters disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-139 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-BEACON-DTIM-IOCTL-CARRIERS-066
- symptom: APSTA beacon interval and DTIM IOCTL carrier/callback operands are recovered but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact IOCTLs, payload size, applied-state fields, callback context, and rxPayload bytestream offsets used by reference.
- actual behavior: local code has default fields but not the IOCTL/callback ABI witnesses.
- exact divergence point: APSTA `setBeaconInterval`, `handleSetBcnIntervalAsyncCallBack`, and `handleSetBcnDTIMPeriodAsyncCallBack` ranges listed above.
- evidence from runtime: no new runtime claim; this is static APSTA beacon/DTIM carrier restoration.
- evidence from decomp: APSTA beacon/DTIM disassembly listed in A-APSTA-BEACON-DTIM-IOCTL-CARRIERS-066.
- exact semantic mismatch between reference and our code: reference uses fixed IOCTLs and payload/callback ABI for beacon/DTIM apply; local scaffold does not record those operands.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this AP parameter layer, these operands are the direct reference command/callback contract.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants and a layout witness only; it does not send IOCTLs, force applied fields, or bypass callbacks.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_beacon_dtim_ioctl_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer payload ABI from unrelated IOCTLs.
  - write applied beacon/DTIM fields without IOCTL success.
  - suppress callback errors.
  - call beacon/DTIM setters before APSTA owner construction is implemented.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-139.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA beacon/DTIM disassembly directly shows IOCTLs, payload length, fields, callback context, and rxPayload offsets.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for beacon/DTIM carrier scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, one payload-head layout witness, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records IOCTL/callback ABI without sending commands.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function ranges and disassembly operands are listed above.

## ANOMALY

- id: A-APSTA-HOSTAP-SUCCESS-TAIL-067
- status: CONFIRMED_DEVIATION
- symptom: APSTA beacon/DTIM carriers are represented, but the `setHostApModeInternal` success-tail state/timer/closednet operands remain prose-only.
- first visible manifestation: APSTA HostAP success-tail audit after A-APSTA-BEACON-DTIM-IOCTL-CARRIERS-066.
- expected system behavior:
  - after AP bring-up succeeds, reference writes `state+0x26c = 1`, clears `state+0x20c`, clears `state+0x88`, and calls `handleAPStatsUpdates(state+0x70)`.
  - it schedules monitor timer `state+0x78` through vtable `+0x1d0` with interval `0x3e8`.
  - it reads network-data flags at input `+0x4`; bit `8` selects beacon interval `0x64`, otherwise `0x12c`, and writes the selected value to `state+0x14`.
  - if flags bit `9` is set, it sends IOVAR `"closednet"` through commander `state+0x228` with 4-byte payload value `1`.
  - on closednet success it continues to `initSoftAPParameters()`; on nonzero error it logs but still rejoins the common path.
- actual behavior:
  - local APSTA scaffold has state fields and beacon/DTIM command carriers, but lacks compiled constants for this HostAP success-tail state, timer, flag-bit, and closednet IOVAR contract.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...) @ 0xffffff800168c138..0xffffff800168c296`
- evidence:
  - disasm: writes `1` to `state+0x26c`, `0` to `state+0x20c`, and `0` to `state+0x88`.
  - disasm: calls `handleAPStatsUpdates` with `state+0x70`, then schedules `state+0x78` through vtable `+0x1d0` with `0x3e8`.
  - disasm: reads `input+0x4`, tests bits `8` and `9`, selects beacon interval `0x64` or `0x12c`, writes `state+0x14`, then optionally sends IOVAR `"closednet"` with payload size `4` and value `1`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks HostAP success-tail constants.
  - rejected: use generic link-up/timer defaults from primary STA.
  - rejected: omit closednet because hidden-mode setter exists elsewhere.
- confirmed deviation: APSTA HostAP success-tail operands are recovered but not represented locally.
- root cause: confirmed for APSTA HostAP success-tail scaffold completeness only.
- fix:
  - add constants for AP-up state, stats/runtime clears, monitor timer vtable/interval, network-data flags offset, flag bits `8/9`, beacon interval values `0x64/0x12c`, closednet payload value/size, and closednet IOVAR presence.
  - add static asserts tying offset constants to local state layout.
  - update YAML/prose docs and save a local reference note.
  - keep runtime HostAP mode disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-140 batch request.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-HOSTAP-SUCCESS-TAIL-067
- symptom: APSTA HostAP success-tail state/timer/closednet operands are recovered but not compiled as APSTA contract constants.
- expected system behavior: local APSTA scaffold records exact state writes, timer schedule, beacon interval selection, and closednet IOVAR operands used by reference after AP bring-up.
- actual behavior: local code has fields and adjacent command carriers but not this success-tail witness.
- exact divergence point: APSTA `setHostApModeInternal` success-tail range listed above.
- evidence from runtime: no new runtime claim; this is static APSTA HostAP success-tail restoration.
- evidence from decomp: APSTA HostAP success-tail disassembly listed in A-APSTA-HOSTAP-SUCCESS-TAIL-067.
- exact semantic mismatch between reference and our code: reference uses fixed state/timer/flag/closednet operands; local scaffold does not record them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this HostAP success-tail layer, these are the direct reference writes/calls after AP bring-up.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not enable HostAP, force AP-up state, schedule timers, or send closednet.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_hostap_success_tail_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer AP-up side effects from primary STA link-up.
  - schedule timers without reference interval.
  - skip closednet because hidden-mode setter exists.
  - force AP-up state at runtime.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-140.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: `setHostApModeInternal` disassembly directly shows state writes, timer vtable/interval, flag bits, beacon interval values, and closednet payload.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for HostAP success-tail scaffold completeness; no final STA RSN/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records HostAP success-tail operands without enabling HostAP.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact success-tail function range and disassembly operands are listed above.

## ANOMALY

- id: A-APSTA-HOSTAP-ASSOC-VENDOR-IE-LAYER-068
- status: CONFIRMED_DEVIATION
- symptom: APSTA HostAP success tail is represented, but the immediately preceding max-assoc and vendor-IE programming operands remain prose-only.
- first visible manifestation: APSTA `setHostApModeInternal` audit after A-APSTA-HOSTAP-SUCCESS-TAIL-067.
- expected system behavior:
  - before the AP-up success tail, reference reads firmware/config max-assoc from the core expansion path, stores it at `state+0x8`, calls `setMaxAssoc(value)`, then invokes APSTA vtable `+0xb18` selector `0x57` over `state+0x8` with length `4`.
  - `setMaxAssoc(unsigned int)` compares the requested value against `state+0x4`, computes payload `state+0x0 + requested`, verifies it does not exceed `state+0x8`, writes `state+0x4 = requested`, and sends IOVAR `maxassoc` through commander `state+0x228` with a 4-byte payload.
  - if network data contains a vendor IE list at `network_data+0x2dc/+0x2e0`, reference calls `programVendorIEList`; otherwise it calls `programAppleVendorIE`.
  - `programVendorIEList` accepts chunks only while at least 6 bytes remain, validates IE body length from input `+0x1`, allocates an `0x814`-byte `apple80211_ie_data` carrier, fills fixed header values, copies IE bytes, calls `AppleBCMWLANCore::setVendorIE(interfaceId, carrier)`, frees the carrier, and advances by `length + 2`.
  - `programAppleVendorIE` uses IOVAR `vndr_ie`: it sizes the RX buffer from `min(maxTxPayload, maxRxPayload) - strlen("vndr_ie") - 1`, deletes existing Apple OUI entries, sends a fixed Apple capability IE with length `0x18`, and can append extended capability data from `state+0x2c/+0x2e/+0x2f/+0x30/+0x50/+0x51/+0x59` when feature flag `0x46` and local SoftAP IE fields require it.
- actual behavior:
  - local APSTA scaffold has state fields and command payload head, but lacks compiled constants/layout witnesses for max-assoc, network-data vendor IE list offsets, vendor IE carrier size/header, and Apple vendor IE `vndr_ie` command operands.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...) @ 0xffffff800168c0bf..0xffffff800168c138`
  - `AppleBCMWLANIO80211APSTAInterface::setMaxAssoc(unsigned int) @ 0xffffff800168c6ac..0xffffff800168c7b5`
  - `AppleBCMWLANIO80211APSTAInterface::programVendorIEList(unsigned char*, unsigned int) @ 0xffffff800168c7ba..0xffffff800168c9da`
  - `AppleBCMWLANIO80211APSTAInterface::programAppleVendorIE() @ 0xffffff800168c9e0..0xffffff800168d30d`
- evidence:
  - disasm: `setHostApModeInternal` stores max-assoc at `state+0x8`, calls `setMaxAssoc`, then calls vtable `+0xb18` with selector `0x57`, payload `state+0x8`, and length `4`; it branches on network-data vendor IE length `+0x2dc` and data `+0x2e0`.
  - disasm: `setMaxAssoc` uses offsets `state+0x0/+0x4/+0x8`, IOVAR `maxassoc`, payload length `4`, and commander `state+0x228`.
  - disasm: `programVendorIEList` validates input length, allocates `0x814`, writes fixed header qwords `0x1a00000001` and `0x400000001`, writes IE id at `+0x14`, copies payload to `+0x15`, writes `length + 1` at `+0x10`, calls `setVendorIE`, and frees the carrier.
  - disasm: `programAppleVendorIE` uses `vndr_ie`, `getMaxCmdTxPayload`, `getMaxCmdRxPayload`, delete command `del`, add command `add`, fixed carrier allocation `0x52`, capability payload length `0x18`, Apple OUI compare length `3`, feature flag `0x46`, and extended IE source fields in the APSTA state block.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks this HostAP max-assoc/vendor-IE layer.
  - rejected: treat vendor IE programming as optional because APSTA runtime is not enabled yet.
  - rejected: synthesize AP vendor IE behavior from primary STA association IE handling.
- confirmed deviation: APSTA HostAP max-assoc and vendor-IE operands are recovered but not represented locally.
- root cause: confirmed for APSTA HostAP max-assoc/vendor-IE scaffold completeness only.
- fix:
  - add constants and static asserts for max-assoc state offsets, `maxassoc` payload size, selector `0x57`, vtable offset `0xb18`, network-data vendor IE length/data offsets, vendor IE carrier layout, Apple vendor IE command sizes, Apple OUI/delete/add/capability/extended capability operands.
  - add local layout witnesses for the vendor IE carrier and command set buffer.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-141 batch request.
- notes:
  - This is a static reference-alignment batch. It does not send `maxassoc`, `vndr_ie`, selector `0x57`, or vendor IE commands at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-HOSTAP-ASSOC-VENDOR-IE-LAYER-068
- symptom: APSTA HostAP success-tail is represented, but the max-assoc/vendor-IE layer before it is not compiled as a local APSTA contract.
- expected system behavior: local APSTA scaffold records exact max-assoc state operands, selector `0x57`, network-data vendor IE list offsets, vendor IE carrier layout, and Apple `vndr_ie` command operands used by reference before AP-up.
- actual behavior: local code has adjacent HostAP state fields but not this contract layer.
- exact divergence point: APSTA `setHostApModeInternal`, `setMaxAssoc`, `programVendorIEList`, and `programAppleVendorIE` ranges listed above.
- evidence from runtime: no new runtime claim; this is static APSTA HostAP max-assoc/vendor-IE restoration.
- evidence from decomp: APSTA disassembly listed in A-APSTA-HOSTAP-ASSOC-VENDOR-IE-LAYER-068.
- exact semantic mismatch between reference and our code: reference uses fixed offsets, carrier sizes, command names, selector, and payload lengths; local scaffold does not record them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this HostAP layer, these are the direct reference calls and data carriers immediately before AP-up success-tail.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants and layout witnesses only; it does not enable HostAP, send firmware commands, or force state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_hostap_assoc_vendor_ie_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer AP max-assoc behavior from primary STA association limits.
  - treat missing vendor IE programming as harmless because APSTA runtime is still disabled.
  - send `maxassoc` or `vndr_ie` before APSTA owner construction is complete.
  - collapse Apple vendor IE programming into a generic IE copy path.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-141.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the APSTA disassembly directly shows max-assoc offsets, selector `0x57`, vendor IE list offsets, carrier sizes, fixed header values, command names, and payload lengths.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for HostAP max-assoc/vendor-IE scaffold completeness; no final STA association/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records reference operands without enabling APSTA runtime.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function ranges and disassembly operands are listed above.

## ANOMALY

- id: A-APSTA-ENABLE-AP-INTERFACE-LAYER-069
- status: CONFIRMED_DEVIATION
- symptom: APSTA HostAP success-tail and pre-tail max-assoc/vendor-IE layers are represented, but `enableAPInterface()` AP bring-up side effects remain prose-only.
- first visible manifestation: APSTA `enableAPInterface` audit after A-APSTA-HOSTAP-ASSOC-VENDOR-IE-LAYER-068.
- expected system behavior:
  - reference `setHostApModeInternal` calls `enableAPInterface()` after success-tail/concurrency setup when feature gates and caller network data require AP interface enablement.
  - `enableAPInterface()` conditionally sends `rrm_bcn_req_thrtl_win` and `rrm_bcn_req_max_off_chan_time` with 4-byte zero payloads when feature flag `0x15` and config byte `+0xe2` allow it.
  - it conditionally sends `wnm` with a 4-byte zero payload when feature flag `0x19` and config byte `+0xe3` allow it.
  - it reads boot arg `wlan.ap.maxmpdu` with size `4`; failure maps to `0xffffffff`, success with nonzero value calls `configureMPDUSize(value)`, and success with zero skips MPDU override.
  - it sets core-private bit `0x10000` at private offset `+0x2890`.
  - it calls APSTA vtable `+0xe70` with arguments `(2, 1)`.
  - it prepares `scb_probe` payload qword `0xf0000001e` and dword `5` with payload size `0x0c`; it sends async with `handleSetScbProbeAsyncCallBack` when the commander/feature path supports async completion, otherwise runs sync.
  - it notifies the core path with event id `0x1e`, optional interface name pointer/length capped below `0x11`, and flag `1`.
  - it calls APSTA vtable `+0xb18` selector `4` with zero payload args, calls `AppleBCMWLANCore::addEventBit(5)`, and tailcalls `writeEventBitField()`.
- actual behavior:
  - local APSTA scaffold has generic command payload and HostAP layers, but lacks compiled constants/layout witnesses for `enableAPInterface` RRM/WNM/MPDU/scb_probe/event operands.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::enableAPInterface() @ 0xffffff800168d310..0xffffff800168d858`
- evidence:
  - disasm: feature flag `0x15`, config byte `+0xe2`, IOVAR names `rrm_bcn_req_thrtl_win` and `rrm_bcn_req_max_off_chan_time`, payload size `4`, payload value `0`.
  - disasm: feature flag `0x19`, config byte `+0xe3`, IOVAR `wnm`, payload size `4`, payload value `0`.
  - disasm: boot arg `wlan.ap.maxmpdu` size `4`, default `0xffffffff`, conditional `configureMPDUSize`.
  - disasm: OR `0x10000` into core-private `+0x2890`, APSTA vtable `+0xe70` args `(2,1)`, `scb_probe` qword/dword payload size `0x0c`, callback context offsets `+0/+0x8/+0x10`, notification id `0x1e`, final vtable `+0xb18` selector `4`, `addEventBit(5)`, and `writeEventBitField`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks the `enableAPInterface` side-effect layer.
  - rejected: treat AP-enable as equivalent to primary STA link-up.
  - rejected: skip RRM/WNM/scb_probe because APSTA runtime remains disabled.
- confirmed deviation: APSTA `enableAPInterface` operands are recovered but not represented locally.
- root cause: confirmed for APSTA `enableAPInterface` scaffold completeness only.
- fix:
  - add constants for RRM/WNM feature gates, config byte offsets, IOVAR names, payload sizes/defaults, MPDU boot-arg behavior, core-private bit, APSTA vtable calls, `scb_probe` payload/completion layout, core notification id, and event bit.
  - add local layout witnesses for `scb_probe` payload and command completion context.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-142 batch request.
- notes:
  - This is a static reference-alignment batch. It does not send RRM/WNM/MPDU/scb_probe commands at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-ENABLE-AP-INTERFACE-LAYER-069
- symptom: APSTA `enableAPInterface()` side-effect operands are recovered but not compiled as a local APSTA contract.
- expected system behavior: local APSTA scaffold records exact RRM/WNM disable, MPDU override, core-private flag, APSTA vtable calls, `scb_probe`, notification, and event-bit operands used by reference.
- actual behavior: local code has adjacent HostAP layers but not this AP-enable contract layer.
- exact divergence point: APSTA `enableAPInterface()` range listed above.
- evidence from runtime: no new runtime claim; this is static APSTA AP-enable restoration.
- evidence from decomp: APSTA disassembly listed in A-APSTA-ENABLE-AP-INTERFACE-LAYER-069.
- exact semantic mismatch between reference and our code: reference uses fixed feature gates, config byte offsets, IOVAR names, payload sizes, vtable selectors, event ids, and payload carriers; local scaffold does not record them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this AP-enable layer, these are the direct reference calls and side-effect operands before AP link-up publication.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants and layout witnesses only; it does not enable HostAP, send firmware commands, or force state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_enable_ap_interface_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer AP-enable behavior from primary STA link-up.
  - omit RRM/WNM/scb_probe because APSTA runtime is still disabled.
  - send any AP-enable IOVAR before APSTA owner construction is complete.
  - force final event bit or link-up publication.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-142.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: `enableAPInterface` disassembly directly shows RRM/WNM/MPDU/scb_probe/event operands.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for AP-enable scaffold completeness; no final STA association/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records reference AP-enable operands without enabling APSTA runtime.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function range and disassembly operands are listed above.

## ANOMALY

- id: A-APSTA-HIDDEN-POWER-ASSERTION-LAYER-070
- status: CONFIRMED_DEVIATION
- symptom: APSTA AP-enable side effects are represented, but hidden-mode and SoftAP power-assertion operands remain prose-only.
- first visible manifestation: APSTA `setHOST_AP_MODE_HIDDEN` / `holdSoftAPPowerAssertion` audit after A-APSTA-ENABLE-AP-INTERFACE-LAYER-069.
- expected system behavior:
  - `setHOST_AP_MODE_HIDDEN` first requires AP-up state `state+0x26c != 0`; otherwise it returns `6`.
  - null input returns raw invalid argument `0x16`.
  - hidden value is read from input `+0x4` and must be `0` or `1`.
  - it sends IOVAR `closednet` through commander `state+0x228` with a 4-byte payload carrying the requested hidden value.
  - on success it writes `state+0x0d = (hidden != 0)`.
  - when hidden is cleared and AP remains up, it calls `setPowerSaveState(0, 9)`, clears `state+0x0e`, and calls `holdSoftAPPowerAssertion()`.
  - `holdSoftAPPowerAssertion()` writes `state+0x0c = 1`, then notifies the core path with event id `0x8d`, payload value `1`, payload size `4`, and flag `1` through the core resource `state+0x218 -> +0x128 -> +0x2c20`.
- actual behavior:
  - local APSTA scaffold has the state fields but lacks compiled constants for hidden input offset/range, post-success state writes, power-save reason, and power assertion notification operands.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHOST_AP_MODE_HIDDEN(...) @ 0xffffff800168d970..0xffffff800168dbbc`
  - `AppleBCMWLANIO80211APSTAInterface::holdSoftAPPowerAssertion() @ 0xffffff800168dbc2..0xffffff800168dc8c`
- evidence:
  - disasm: `setHOST_AP_MODE_HIDDEN` tests `state+0x26c`, returns `6` when AP is not up, validates input and `input+0x4 <= 1`, sends `closednet` payload length `4`, writes `state+0x0d`, optionally calls `setPowerSaveState(0, 9)`, clears `state+0x0e`, and calls `holdSoftAPPowerAssertion`.
  - disasm: `holdSoftAPPowerAssertion` writes `state+0x0c = 1`, builds a 4-byte payload value `1`, uses event id `0x8d`, core resource `core+0x128+0x2c20`, and flag `1`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks hidden-mode and power assertion operands.
  - rejected: map hidden mode to the HostAP success-tail `closednet` default only.
  - rejected: skip power assertion because APSTA runtime remains disabled.
- confirmed deviation: APSTA hidden-mode and SoftAP power-assertion operands are recovered but not represented locally.
- root cause: confirmed for APSTA hidden/power scaffold completeness only.
- fix:
  - add constants for hidden input offset/range, AP-up required state, return values, `closednet` payload, post-success state fields, power-save args, and hold-power notification operands.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-143 batch request.
- notes:
  - This is a static reference-alignment batch. It does not send hidden-mode or power assertion commands at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-HIDDEN-POWER-ASSERTION-LAYER-070
- symptom: APSTA hidden-mode and SoftAP power-assertion operands are recovered but not compiled as a local APSTA contract.
- expected system behavior: local APSTA scaffold records exact hidden-mode validation, `closednet` payload, post-success field writes, power-save transition, and power assertion notification operands.
- actual behavior: local code has adjacent AP-enable fields but not this hidden/power contract layer.
- exact divergence point: APSTA `setHOST_AP_MODE_HIDDEN` and `holdSoftAPPowerAssertion` ranges listed above.
- evidence from runtime: no new runtime claim; this is static APSTA hidden/power restoration.
- evidence from decomp: APSTA disassembly listed in A-APSTA-HIDDEN-POWER-ASSERTION-LAYER-070.
- exact semantic mismatch between reference and our code: reference uses fixed validation, return, state, IOVAR, power-save, and notification operands; local scaffold does not record them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this hidden/power layer, these are the direct reference calls and state writes.
- why proposed fix is 1:1 with reference architecture and semantics: it records constants only; it does not enable hidden-mode runtime, send commands, or force state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_hidden_power_assertion_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - infer hidden-mode behavior from HostAP success-tail `closednet`.
  - omit power assertion because APSTA runtime is still disabled.
  - send `closednet` or core notification before APSTA owner construction is complete.
  - force hidden state or AP-up state locally.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-143.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: hidden-mode and power assertion disassembly directly shows validation, offsets, return values, payloads, and notification operands.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for hidden/power scaffold completeness; no final STA association/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records reference hidden/power operands without enabling APSTA runtime.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function ranges and disassembly operands are listed above.

## ANOMALY

- id: A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071
- status: CONFIRMED_DEVIATION
- symptom: APSTA hidden/power operands are represented, but channel, CSA, and STA-control public method carriers are still partially prose-only and the CSA threshold documentation was inverted.
- first visible manifestation: APSTA public method audit after A-APSTA-HIDDEN-POWER-ASSERTION-LAYER-070.
- expected system behavior:
  - `getCHANNEL(...)` builds a 0x0c-byte RX payload, uses virtual IOCTL get selector `0x1d`, copies the received channel number to output `+0x08`, and ORs output flags `+0x0c` with `0x08` for channels below `0x0f` or `0x10` otherwise.
  - `setCHANNEL(...)` rejects null input and channels `>= 0x100` with raw `0x16`, maps flags `0x02/0x04/0x400` to bandwidth `2/3/4`, calls `AppleBCMWLANCore::getChanSpec`, returns `0xe00002c2` for zero chanspec, and sends 4-byte IOVAR `chanspec` for nonzero chanspec.
  - `setSOFTAP_TRIGGER_CSA(...)` requires `state+0x26c != 0` and `state+0x329 & 1`, rejects null input with raw `0x16`, accepts parsed chanspec values below `0x10000`, rejects values at or above `0x10000`, and sends 6-byte IOVAR `csa`.
  - `setSTA_AUTHORIZE(...)` rejects null input with `0xe00002c2`, reads the MAC at input `+0x08`, and selects virtual IOCTL `0x7a` for flag values below `1` or `0x79` otherwise.
  - `setSTA_DISASSOCIATE(...)` occupies APSTA vtable slot `522`/byte offset `0x1050`, builds a 0x0c-byte payload from input `+0x04/+0x08/+0x0c`, writes sentinel word `0xaaaa`, and calls virtual IOCTL set selector `0xc9`.
  - `setSTA_DEAUTH(...)` occupies APSTA vtable slot `523`/byte offset `0x1058` and tailcalls APSTA byte offset `+0x1040`.
- actual behavior:
  - local APSTA scaffold has adjacent public SoftAP constants, but channel/CSA/STA-control carrier layouts and vtable byte-offset guards are incomplete.
  - previous local prose/YAML stated the CSA threshold in reverse: “below `0x10000` returns raw `0x16`”.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::getCHANNEL(...) @ 0xffffff8001687cbe`
  - `AppleBCMWLANIO80211APSTAInterface::setCHANNEL(...) @ 0xffffff800168dcfa`
  - `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_TRIGGER_CSA(...) @ 0xffffff800168e0ae`
  - `AppleBCMWLANIO80211APSTAInterface::setSTA_AUTHORIZE(...) @ 0xffffff800168f016`
  - `AppleBCMWLANIO80211APSTAInterface::setSTA_DEAUTH(...) @ 0xffffff800168f14c`
  - `AppleBCMWLANIO80211APSTAInterface::setSTA_DISASSOCIATE(...) @ 0xffffff800168f15e`
- evidence:
  - decomp: KDK symbols and resolved APSTA vtable show channel/CSA/STA-control slots and method addresses.
  - disasm: `getCHANNEL(...)` uses selector `0x1d`, RX size `0x0c`, range qword `0x0000000c000c000c`, output number `+0x08`, and flags `0x08/0x10`.
  - disasm: `setCHANNEL(...)` validates channel `< 0x100`, maps flags to bandwidth, gets chanspec, and sends IOVAR `chanspec`.
  - disasm: `setSOFTAP_TRIGGER_CSA(...)` accepts chanspec values below `0x10000`, rejects `>= 0x10000`, and builds the 6-byte `csa` payload.
  - disasm: `setSTA_AUTHORIZE(...)` uses null return `0xe00002c2`, MAC payload `input+0x08`, and selectors `0x79/0x7a`.
  - disasm: `setSTA_DISASSOCIATE(...)` builds the 0x0c-byte payload and calls selector `0xc9`.
  - disasm: `setSTA_DEAUTH(...)` tailcalls byte offset `+0x1040`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_channel_csa_sta_control_2026_04_27.md`.
- candidate causes:
  - confirmed: local APSTA scaffold lacked compiled channel/CSA/STA-control carrier witnesses and carried an inverted CSA threshold statement.
  - rejected: treat CSA as unsupported because APSTA runtime is not yet enabled.
  - rejected: implement channel setters through primary STA channel state.
  - rejected: add null guard to `setSTA_DISASSOCIATE` without producer-side reference evidence.
- confirmed deviation: APSTA channel/CSA/STA-control operands are recovered from reference but not fully represented locally; CSA threshold prose contradicted disassembly.
- root cause: confirmed for APSTA public channel/CSA/STA-control scaffold completeness only; no final STA association/data-path root-cause claim is made.
- fix:
  - add constants and layout witnesses for channel data, channel carrier, command RX range, CSA input/payload, STA authorize input, STA disassociate input/payload, and STA-control selectors.
  - add SAP/APSTA vtable slot and byte-offset constants for `getCHANNEL`, `setCHANNEL`, `setSTA_AUTHORIZE`, `setSTA_DISASSOCIATE`, `setSTA_DEAUTH`, `setRSN_CONF`, and `setSOFTAP_TRIGGER_CSA`.
  - correct CSA threshold documentation in YAML, discrepancy inventory, signal-chain audit, and analysis.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-145 batch request.
- notes:
  - This is a static reference-alignment batch. It does not send channel, CSA, authorize, disassociate, or deauth commands at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071
- symptom: APSTA channel/CSA/STA-control method contracts are recovered but incomplete locally, and CSA threshold prose is inverted.
- expected system behavior: local APSTA scaffold records exact channel, CSA, authorize, disassociate, deauth carriers, selectors, return codes, vtable slots, and CSA threshold semantics before enabling a real APSTA owner.
- actual behavior: several carriers/slots remain prose-only and CSA was documented as rejecting the accepted `< 0x10000` range.
- exact divergence point: APSTA methods listed in A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071.
- evidence from runtime: no new runtime claim; this is static APSTA public-method restoration.
- evidence from decomp: APSTA disassembly listed in A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071 plus resolved APSTA vtable dump.
- exact semantic mismatch between reference and our code: reference uses fixed carriers, selectors, return values, and threshold semantics; local scaffold did not encode them and docs contradicted CSA disassembly.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this layer, these are direct ABI and command operands consumed by APSTA public methods. The fix does not claim final STA connect/data root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants, layouts, and vtable slot guards only; it does not synthesize APSTA runtime, force AP/STA state, or add retry/fallback behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `include/Airport/IO80211SapProtocol.h`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_channel_csa_sta_control_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - force APSTA channel success or CSA success locally.
  - add fallback/retry/polling around channel or CSA methods.
  - implement APSTA channel setters using primary STA channel state.
  - add unreferenced null guard to `setSTA_DISASSOCIATE`.
  - keep the old CSA threshold documentation after disassembly disproved it.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-145.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: APSTA channel/CSA/STA-control disassembly directly shows selectors, offsets, payload sizes, vtable byte offsets, and CSA threshold.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA public-method scaffold completeness; no final STA association/data claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records reference APSTA operands without enabling APSTA runtime.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-ASYNC-CALLBACK-TELEMETRY-CONTRACTS-079
- status: CONFIRMED_DEVIATION
- symptom: APSTA HostAP IPv4 packet-filter delete and beacon interval/DTIM async callback telemetry contracts were still not represented as compiled local witnesses.
- first visible manifestation: APSTA callback-tail audit after A-APSTA-MONITOR-POWER-STATS-CONTRACTS-078.
- expected system behavior:
  - `setHostApModeInternal(...)` sends `pkt_filter_delete` through `state+0x228` with 4-byte payload value `0x6c`, no RX expected, callback cookie `0`, and callback `deleteIPv4PktFiltersAsyncCallBack`.
  - `deleteIPv4PktFiltersAsyncCallBack(...)` returns on status `0`; nonzero status logs at level `2` and decodes errors through `state+0x218` vtable `+0x780`.
  - `setBeaconInterval(uint16_t)` skips when requested equals `state+0x68`, sends IOCTL `0x4c` payload size `4`, uses callback `handleSetBcnIntervalAsyncCallBack`, and writes `state+0x68` after successful send/sync set.
  - DTIM setup in `initSoftAPParameters()` skips when `state+0x16 == state+0x6a`, sends IOCTL `0x4e` payload size `4`, uses callback `handleSetBcnDTIMPeriodAsyncCallBack`, and writes `state+0x6a` after successful send/sync set.
  - beacon interval/DTIM callbacks return on status `0`; nonzero status logs at level `1` and emits `CommandRxPayload` data `+0x00`, length `+0x08`, telemetry flag `1`, through resource `state+0x210`.
- actual behavior:
  - local APSTA scaffold had beacon/DTIM carrier documentation, but lacked compiled string/constant witnesses for callback labels, log levels/lines, async RX payload offsets, and IPv4 packet-filter delete.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...) @ 0xffffff8001688d12..0xffffff8001688d84`
  - `deleteIPv4PktFiltersAsyncCallBack(...) @ 0xffffff8001692b52`
  - `setBeaconInterval(uint16_t) @ 0xffffff8001687ae4`
  - DTIM producer in `initSoftAPParameters() @ 0xffffff800168795f..0xffffff8001687ac2`
  - `handleSetBcnIntervalAsyncCallBack(...) @ 0xffffff800169365a`
  - `handleSetBcnDTIMPeriodAsyncCallBack(...) @ 0xffffff800169370e`
- evidence:
  - disasm: HostAP path writes dword `0x6c`, sends IOVAR `pkt_filter_delete`, TX length `4`, no RX expected, callback cookie `0`.
  - disasm: delete callback status-nonzero path logs at level `2`, line `0x0ea0`, using error decode vtable `+0x780`.
  - disasm: beacon interval path uses IOCTL `0x4c`, payload size `4`, applied state `+0x68`, sync error line `0x106b`.
  - disasm: DTIM path uses IOCTL `0x4e`, payload size `4`, source `+0x16`, applied state `+0x6a`, sync error line `0x1091`.
  - disasm: callbacks use labels `BCNPRD IOCTL rxPayload bytestream: ` and `DTIMPRD IOCTL rxPayload bytestream: ` with RX data offset `0`, length offset `8`, telemetry flag `1`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_async_callback_telemetry_2026_04_27.md`.
- candidate causes:
  - confirmed: APSTA callback telemetry constants/strings were not compiled into the local scaffold.
  - rejected: execute HostAP filter/beacon/DTIM commands before APSTA owner lifecycle is complete.
- confirmed deviation: recovered APSTA async callback/telemetry operands existed only partially in YAML/prose, not in compiled local witnesses.
- root cause: confirmed for this APSTA callback-tail layer only. No final primary STA association/data or AP runtime root cause is claimed.
- fix:
  - add constants, strings, and static asserts for `pkt_filter_delete`, callback labels, log levels/lines, async RX payload offsets, telemetry flag, and delete-filter payload.
  - add reference/YAML/prose documentation.
  - keep APSTA runtime disabled.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-153.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-ASYNC-CALLBACK-TELEMETRY-CONTRACTS-079
- symptom: APSTA async callback telemetry contracts and HostAP IPv4 filter delete were incomplete in the local scaffold.
- expected system behavior: local APSTA scaffold records exact IOVAR name, payload value/size, callback log levels/lines, RX payload offsets, telemetry labels, and state/resource offsets.
- actual behavior: these facts were absent or prose-only.
- exact divergence point: APSTA functions listed in A-APSTA-ASYNC-CALLBACK-TELEMETRY-CONTRACTS-079.
- evidence from runtime: no new runtime claim; this is static APSTA callback-tail restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference uses fixed strings, payload values, callback metadata, and telemetry offsets; local scaffold did not encode them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA callback-tail layer, the recovered disassembly directly defines the missing ABI/semantic contracts. The fix does not claim final STA/AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants/string/static asserts/docs only; it does not execute callbacks/IOVARs, force state, synthesize success, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_async_callback_telemetry_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - call APSTA HostAP filter/beacon/DTIM IOVARs from primary STA runtime.
  - force applied beacon/DTIM state.
  - add fallback/retry/poll or suppress callback errors.
  - enable role-7 APSTA creation in this structural batch.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-153.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed APSTA disassembly directly shows IOVAR name, payload value/size, callback metadata, RX offsets, telemetry labels, log levels, and line constants.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA async callback scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, string witnesses, YAML, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered APSTA callback contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-MONITOR-POWER-STATS-CONTRACTS-078
- status: CONFIRMED_DEVIATION
- symptom: APSTA monitor/stats timers, power-state machine tail, assoc-list conversion, MFP command, datapath print, and RX-counter contracts were still missing from the local scaffold after CR-151.
- first visible manifestation: APSTA owner-layer audit after A-APSTA-POWER-OFFLOAD-DATAPATH-TAIL-077.
- expected system behavior:
  - `handleAPStatsUpdates(IO80211TimerSource*)` validates timer `state+0x70`, allocates `0x808`, calls APSTA vtable `+0xfd8`, updates activity baseline `state+0x88`, posts inactivity STA message `0x0d` at threshold `0x16e361`, and reschedules interval `0x1388`.
  - `monitorAPInterface(IO80211TimerSource*)` validates timer `state+0x78`, mirrors core-private `+0x4d59` bit 0 to `state+0x208`, refreshes Apple vendor IE when required, updates RX deltas at `state+0x1b8/+0x1c0`, drives low-traffic counter `state+0x64`, and reschedules interval `0x3e8`.
  - `setPowerSaveState(...)` is gated by `state+0x0e`, ignores reason `7`, records transition count at `state+0x1c8 + state * 0x10`, accumulates durations through timestamp `state+0x1a8`, and handles states `0..3` with the recovered beacon/power-assertion side effects.
  - assoc-list callback/conversion uses BCM count `+0x00`, first MAC `+0x04`, MAC stride `6`, Apple output size `0x808`, version `1`, count `+0x04`, entries `+0x08`, entry stride `0x10`, max count `0x80`, clamp threshold `0x81`.
  - MFP uses feature gate `0x26`, IOVAR `mfp`, payload size `4`, unsupported return `0`.
  - `printDataPath(userPrintCtx*)` uses userPrintCtx offsets `+0x18/+0x20/+0x24/+0x28` and vtable slots `+0x338/+0x320/+0x328/+0xc68`; `updateRxCounter(uint64_t)` adds to `state+0xa0`.
- actual behavior:
  - local APSTA scaffold lacked compiled constants/layout/static asserts for these contracts.
  - docs/YAML did not record the monitor/power/stats layer recovered after CR-151.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::handleAPStatsUpdates(...) @ 0xffffff8001685a36`
  - `monitorAPInterface(...) @ 0xffffff8001685e94`
  - `setPowerSaveState(...) @ 0xffffff8001686e62`
  - `getAssocListAsyncCallback(...) @ 0xffffff80016880fe`
  - `convertBCMAssocListToAppleAssocList(...) @ 0xffffff80016881f6`
  - `configureManagementFrameProtectionForSoftAP(...) @ 0xffffff800168c4fe`
  - `printDataPath(...) @ 0xffffff8001694176`
  - `updateRxCounter(...) @ 0xffffff8001694450`
- evidence:
  - disasm: stats timer shows `0x808` allocation, APSTA vtable `+0xfd8`, async failure `0xe00002d8`, inactivity thresholds `0x16e360/0x16e361/0x170a71`, timer interval `0x1388`.
  - disasm: monitor timer shows core-private byte `+0x4d59`, mirror `state+0x208`, vendor-IE refresh flag `state+0x62`, low-traffic counter `state+0x64`, RX counter vtable `+0xc38`, baselines `state+0x90/+0x98`, stats `state+0x1b8/+0x1c0`, interval `0x3e8`.
  - disasm: power-state machine shows reason gates, transition records, `modesw_bcns_wait`, `lphs_mode`, beacon duty-cycle and power assertion side effects.
  - disasm: assoc-list conversion shows exact BCM/Apple layout, clamp, and entry copy offsets.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_monitor_power_stats_2026_04_27.md`.
- candidate causes:
  - confirmed: APSTA monitor/power/stats constants and layout witnesses were not compiled into local scaffold.
  - rejected: execute timers/IOVARs before APSTA owner lifecycle is complete.
  - rejected: add fallback, forced state changes, or primary-STA substitutions.
- confirmed deviation: recovered APSTA monitor/power/stats ABI and semantic operands existed only in analysis, not in compiled local witnesses or YAML.
- root cause: confirmed for this APSTA layer restoration only. No final primary STA association/data or AP runtime root cause is claimed.
- fix:
  - add constants, state-field splits, layout witnesses, and static asserts for monitor/stats timers, assoc-list conversion, power-state records, MFP, datapath print, and RX counter.
  - add reference/YAML/prose documentation.
  - keep APSTA runtime disabled.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-152.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-MONITOR-POWER-STATS-CONTRACTS-078
- symptom: APSTA monitor/power/stats contracts after the power/offload/datapath tail were incomplete in the local scaffold.
- expected system behavior: local APSTA scaffold records exact timer, power-state, assoc-list, MFP, datapath-print, and RX-counter constants, layouts, state offsets, vtable offsets, and return semantics.
- actual behavior: these facts were absent or prose-only.
- exact divergence point: APSTA functions listed in A-APSTA-MONITOR-POWER-STATS-CONTRACTS-078.
- evidence from runtime: no new runtime claim; this is static APSTA layer restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference uses fixed state offsets, timers, payload layouts, IOVAR names, vtable offsets, thresholds, and return values; local scaffold did not encode them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA owner-layer segment, the recovered disassembly directly defines the missing ABI/semantic contracts. The fix does not claim final STA/AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants/layout/static asserts/docs only; it does not execute timers/IOVARs, force state, synthesize success, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_monitor_power_stats_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - call APSTA monitor/stats timers or MFP/power IOVARs from primary STA runtime.
  - force AP-up, low-power, association-list, or counter state.
  - add fallback/retry/poll or suppress callback errors.
  - enable role-7 APSTA creation in this structural batch.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-152.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed APSTA disassembly directly shows offsets, thresholds, IOVAR names, payload sizes, vtable offsets, state writes, and return values.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA monitor/power/stats scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, state fields, YAML, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered APSTA contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-ACTION-FRAME-LPHS-CONTRACTS-080
- status: CONFIRMED_DEVIATION
- symptom: APSTA event/station-table contracts existed, but action-frame LPHS state semantics were incomplete and one local constant pair inverted Apple sleep/awake meanings.
- first visible manifestation: APSTA action-frame audit after A-APSTA-ASYNC-CALLBACK-TELEMETRY-CONTRACTS-079.
- expected system behavior:
  - `handleEvent` dispatches event type `0x4b` into the action-frame path.
  - action-frame payload base is `event+0x30`; minimum accepted payload length is `0x12`.
  - raw version `0x0100` reads category/action at payload `+0x10/+0x11`; raw version `0x0200` requires length `0x1a` and reads category/action at payload `+0x18/+0x19`.
  - unknown version leaves category/action at sentinel `0xaa`; byte-swapped versions `>= 3` are rejected.
  - LPHS category is `0x7f`; accepted actions are `1` and `2`.
  - accepted action value is written directly to station sleep-state `state+0xb8 + index*0x30 + 0x10`.
  - new station entries initialize sleep-state to `2`; active entries with sleep-state `2` block `checkIfAllStaAreInLPM`.
  - when no active station remains in blocking state `2` and SoftAP concurrency is disabled, Apple calls `setPowerSaveState(3, 0x0b)`.
- actual behavior:
  - local scaffold had action-frame offsets but lacked event-offset, sentinel, reject-threshold, all-STA check, log-line, and power-save reason witnesses.
  - local constants identified action `1` as awake and action `2` as sleep, opposite of Apple station-table semantics.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::handleEvent(...)` action-frame branch `0xffffff80016904bf..0xffffff8001690f70`.
  - inlined `checkIfAllStaAreInLPM()` loop `0xffffff8001690d10..0xffffff8001690f24`.
  - all-STA transition tail `0xffffff8001690f24..0xffffff8001690f70`.
  - `AppleBCMWLANIO80211APSTAInterface::setPowerSaveState(...) @ 0xffffff8001686e62`.
  - `runPowerSaveStateMachine()` log site `0xffffff8001686214..0xffffff8001686234`.
- evidence:
  - disasm: action-frame branch reads event type `0x4b`, payload length `event+0x14`, and payload base `event+0x30`.
  - disasm: v1 branch reads category/action from absolute event offsets `+0x40/+0x41`; v2 branch reads from `+0x48/+0x49`.
  - disasm: category `0x7f` with action `1` or `2` writes the action byte directly to station entry sleep-state offset `+0x10`.
  - disasm: add-station path initializes entry sleep-state to `2`.
  - disasm: `checkIfAllStaAreInLPM` treats active entries with sleep-state `2` as blockers and only then suppresses all-LPM transition.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_action_frame_lphs_2026_04_27.md`.
- candidate causes:
  - confirmed: local APSTA LPHS action constants inverted sleep/awake semantics.
  - confirmed: local scaffold lacked compiled all-STA LPM transition and parse sentinels.
  - rejected: force low-power state without the Apple all-STA check.
  - rejected: synthesize or replay LPHS action frames.
  - rejected: alter primary STA power state from this APSTA-only path.
- confirmed deviation: reference maps action `1` to low-power/sleep and action `2` to awake/default through direct station-table writes; local constants said the opposite.
- root cause: confirmed for APSTA action-frame/LPHS scaffold correctness only. No final primary STA association/data or AP runtime root cause is claimed.
- fix:
  - correct LPHS action constants and station-table low-power/awake aliases.
  - add event-offset, reject-threshold, sentinel, all-STA blocker, power-save reason, and log-line witnesses.
  - add reference/YAML/prose documentation.
  - keep APSTA runtime disabled.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-154.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-ACTION-FRAME-LPHS-CONTRACTS-080
- symptom: APSTA action-frame LPHS semantics and all-STA LPM transition contracts were incomplete, with sleep/awake action constants inverted locally.
- expected system behavior: local APSTA scaffold records exact action-frame parse offsets, LPHS action semantics, station sleep-state aliases, all-STA checker state, power-save transition, and log-line constants.
- actual behavior: local constants had action `1` as awake and action `2` as sleep; several parse/check contracts were absent or prose-only.
- exact divergence point: APSTA functions listed in A-APSTA-ACTION-FRAME-LPHS-CONTRACTS-080.
- evidence from runtime: no new runtime claim; this is static APSTA action-frame/LPHS restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference writes LPHS action `1/2` directly into station sleep-state, where state `2` is initialized default/awake and blocks all-LPM; local constants treated `2` as low-power.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA action-frame layer, the recovered disassembly directly defines the missing ABI/semantic contracts. The fix does not claim final STA/AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants, aliases, static asserts, YAML, and docs only; it does not execute APSTA runtime, synthesize frames, force state, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_action_frame_lphs_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - force all-STA low-power without station-table evidence.
  - synthesize, replay, or re-emit LPHS action frames.
  - change primary STA power, association, key, or data paths from this APSTA-only layer.
  - add fallback/retry/poll or suppress invalid action-frame versions.
  - enable role-7 APSTA creation in this structural batch.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-154.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed APSTA action-frame branch directly shows event type, offsets, LPHS action writes, station-state blocker, and all-STA transition.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA action-frame/LPHS scaffold correctness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, aliases, static asserts, YAML, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered APSTA action-frame contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-EVENT-STATION-TABLE-CONTRACTS-076
- status: CONFIRMED_DEVIATION
- symptom: APSTA station/key public bodies are represented, but the producer-side event and station-table mutation layer was still partial.
- first visible manifestation: APSTA station table audit after A-APSTA-STATION-KEY-BODY-CONTRACTS-075.
- expected system behavior:
  - APSTA station table is a five-entry block from `state+0xb8` with stride `0x30`.
  - each station entry has active byte at entry `+0x00`, MAC at `+0x01`, sleep state at `+0x10`, AIHS flag at `+0x20`, sharing flag at `+0x24`, and Apple-station flag at `+0x28`.
  - `handleEvent(...) @ 0xffffff800168faa0` reads event type/status/reason/auth/data-length/address/data at `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x18/+0x30`.
  - association/reassociation events `8/10` with status/reason `0/0` write state metadata `+0x80/+0x84`, call `updateSTAAssocInfo`, parse RSNXE, and post STA message id `0x0c` with payload size `0x114`.
  - removal events `5/6/11/12` decrement associated count when nonzero, notify each APSTA TX subqueue via vtable `+0x358`, clear the table entry, and post STA message id `0x0d` with payload size `0x0c`.
  - `postMessageForSTA(...) @ 0xffffff8001691bb8` dispatches through APSTA vtable `+0xb18` and notifies core owner `state+0x218 -> +0x128 -> +0x2c20` with flag `1`.
  - `checkForAppleIE(...)`, `updateSTAAssocInfo(...)`, `parseRSNXE(...)`, `checkForStationListMismatch(...)`, and `removeStaFromStaTable(...)` use the fixed IE, RSNXE, station-list, and table offsets recorded in the reference note.
- actual behavior:
  - local station-table witness had the MAC at entry offset `0`, while reference uses active byte at `+0` and MAC at `+1`.
  - event producer contracts, STA message payloads, action-frame low-power updates, Apple IE flags, RSNXE parsing, firmware-list mismatch handling, and removal clear policy were not compiled witnesses.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::handleEvent(...) @ 0xffffff800168faa0`
  - `AppleBCMWLANIO80211APSTAInterface::postMessageForSTA(...) @ 0xffffff8001691bb8`
  - `AppleBCMWLANIO80211APSTAInterface::checkForAppleIE(...) @ 0xffffff8001691cc6`
  - `AppleBCMWLANIO80211APSTAInterface::updateSTAAssocInfo(...) @ 0xffffff8001691d6a`
  - `AppleBCMWLANIO80211APSTAInterface::parseRSNXE(...) @ 0xffffff800169217e`
  - `AppleBCMWLANIO80211APSTAInterface::checkForStationListMismatch(...) @ 0xffffff800169229a`
  - `AppleBCMWLANIO80211APSTAInterface::removeStaFromStaTable(...) @ 0xffffff800169252a`
- evidence:
  - disassembly: `updateSTAAssocInfo` writes active byte at `state+0xb8 + index*0x30`, MAC at `+0xb9`, sleep state at `+0xc8`, AIHS/sharing at `+0xd8/+0xdc`, and increments `state+0x00`.
  - disassembly: association path posts message id `0x0c` size `0x114`; removal path posts message id `0x0d` size `0x0c`.
  - disassembly: `removeStaFromStaTable` rejects indexes `>=5` with `0xe00002bc` and clears six qwords from the entry.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_event_station_table_2026_04_27.md`.
- candidate causes:
  - confirmed: local APSTA scaffold lacked producer-side station table/event witnesses.
  - confirmed: previous station-table entry witness modeled the MAC-relative view instead of the full active-byte entry.
  - rejected: infer station state from primary STA association state.
  - rejected: route event handling at runtime before APSTA owner/lifecycle is complete.
- confirmed deviation: APSTA event/station-table producer contracts were recovered but absent or partially wrong locally.
- root cause: confirmed for APSTA event/station-table scaffold completeness only; no final STA/AP runtime claim is made.
- fix:
  - change the local station-table entry witness to the full `0x30` byte entry with active/MAC/sleep/AIHS/sharing/Apple-station fields.
  - type `state+0xb8` as five station-table entries while preserving state size `0x338`.
  - add event header, STA association/removal message, action-frame, Apple IE, RSNXE, station-list mismatch, and removal constants/layouts.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-150 batch request.
- notes:
  - This batch is structural and supersedes CR-149. It does not invoke APSTA event handling at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-EVENT-STATION-TABLE-CONTRACTS-076
- symptom: APSTA event/station table contracts are recovered but not compiled into local layout witnesses.
- expected system behavior: local APSTA scaffold records exact station-table entry offsets, event header offsets, STA post-message payload sizes, Apple IE/RSNXE parse operands, action-frame low-power operands, and station-list mismatch behavior.
- actual behavior: these contracts remained absent or partially represented; the station-table entry witness had a MAC-relative offset instead of full entry offset.
- exact divergence point: APSTA methods listed in A-APSTA-EVENT-STATION-TABLE-CONTRACTS-076.
- evidence from runtime: no new runtime claim; this is static APSTA producer-layer restoration.
- evidence from decomp: APSTA disassembly from `/tmp/AppleBCMWLANCoreMac` at the exact addresses listed above.
- exact semantic mismatch between reference and our code: reference owns a typed five-entry station table with active byte, MAC, sleep state, station flags, STA event messages, and queue notifications; local scaffold did not encode that producer layer.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA scaffold layer, these fields are the producer-side state consumed by already recovered station/key public methods.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants, state aliases, carriers, and static asserts only; it does not execute APSTA event handling or alter primary STA behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_event_station_table_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - map APSTA station table to primary STA association state.
  - force associated-station counts or AP-up state.
  - add runtime event dispatch before APSTA owner lifecycle is restored.
  - guess Apple OUI byte values not needed for the local ABI/layout witness.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-150.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed method disassemblies directly show offsets, event ids, message sizes, table entry fields, return values, and helper calls.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA event/station-table scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, state aliases, message carriers, and static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records the recovered APSTA producer layer before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-HOSTAP-CONTROL-POWER-LAYER-072
- status: CONFIRMED_DEVIATION
- symptom: APSTA channel/CSA/STA-control carriers are represented, but HostAP control/power wrapper operands remain prose-only.
- first visible manifestation: APSTA `setHOST_AP_MODE` / HostAP power audit after A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071.
- expected system behavior:
  - `setHOST_AP_MODE(...)` reads network-data mode at input `+0x1c`.
  - it reads proximity owner from core-private `+0x2c28`, NAN owner from `+0x74f0`, and NAN data owner from `+0x74f8`.
  - for non-null input with mode `+0x1c != 0`, feature gate `0x46` controls pre-internal bringdown of those neighbouring owners before `setHostApModeInternal(input)`.
  - for null input or mode `+0x1c == 0`, it calls `setHostApModeInternal(input)` first and then, when feature gate `0x46` permits, brings neighbouring owners back up only if core-private `+0x2890 & 1` is set and core-private dword `+0x4d8c` is `4` or `1`.
  - `hostAPPowerOff()` returns `0` if AP-up state `state+0x26c` is zero.
  - when AP is up and associated station count `state+0x00` is zero, `hostAPPowerOff()` calls `setPowerSaveState(0, 0x0c)`, clears `state+0x0e`, calls `setHostApModeInternal(NULL)`, and notifies core event id `1` with null payload, payload size `0`, and flag `1`.
  - when AP is up, associated station count is nonzero, and SoftAP concurrency is disabled, it calls `setPowerSaveState(3, 3)`.
  - `isSoftAPConcurrencyEnabled()` requires feature `0x46` and core-private byte `+0x4d59 & 0x1b`.
  - `configureLowPowerModeExit()` returns when `state+0xb4 == 0`; otherwise it dispatches low-power exit through work-queue vtable `+0x130`, uses 4-byte command payloads, and successful low-power exit clears `state+0xb4`.
- actual behavior:
  - local APSTA scaffold has adjacent state fields, but not compiled witnesses for network-data mode `+0x1c`, NAN owner offsets, bring-up gates, HostAP power-off notification, concurrency mask, or low-power exit work-queue gate.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::setHOST_AP_MODE(...) @ 0xffffff80016884ae`
  - `AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff() @ 0xffffff8001692772`
  - `AppleBCMWLANIO80211APSTAInterface::isSoftAPConcurrencyEnabled() @ 0xffffff8001692896`
  - `AppleBCMWLANIO80211APSTAInterface::configureLowPowerModeExit() @ 0xffffff80016928e4`
- evidence:
  - decomp: `setHOST_AP_MODE(...)` reads core-private neighbouring owners `+0x2c28/+0x74f0/+0x74f8`, tests input `+0x1c`, and gates bringdown/bringup through feature `0x46`.
  - decomp: `setHOST_AP_MODE(...)` bringup path tests core-private `+0x2890 & 1` and dword `+0x4d8c == 4 || == 1`.
  - decomp: `hostAPPowerOff()` tests `state+0x26c`, checks `state+0x00`, calls `setPowerSaveState(0, 0x0c)`, clears `state+0x0e`, calls `setHostApModeInternal(NULL)`, and calls core notify with event id `1`, null payload, size `0`, flag `1`.
  - decomp: `isSoftAPConcurrencyEnabled()` tests feature `0x46` and core-private byte `+0x4d59 & 0x1b`.
  - decomp: `configureLowPowerModeExit()` returns on `state+0xb4 == 0`, uses work queue vtable `+0x130`, and success thunks clear `state+0xb4`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_hostap_control_power_2026_04_27.md`.
- candidate causes:
  - confirmed: local APSTA compiled witness lacks HostAP control/power owner/gate operands.
  - rejected: represent HostAP mode as a primary STA mode flag.
  - rejected: skip neighbouring owner offsets because APSTA runtime remains disabled.
  - rejected: force HostAP power-off or low-power exit state.
- confirmed deviation: HostAP control/power wrapper operands are recovered but not represented locally.
- root cause: confirmed for APSTA HostAP control/power scaffold completeness only; no AP/SoftAP runtime success claim is made.
- fix:
  - add constants for network-data mode/vendor offsets, feature gate, neighbouring owners, bring-up private gates, power-off paths, SoftAP concurrency mask, and low-power exit gate.
  - add `AirportItlwmAPSTAHostApModeNetworkDataLayout`.
  - extend the core-expansion layout witness through proximity/APSTA/NAN/NAN-data owner offsets.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-146 batch request.
- notes:
  - This is a static reference-alignment batch. It does not bring down/up neighbouring owners or run HostAP power/low-power code at runtime.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-HOSTAP-CONTROL-POWER-LAYER-072
- symptom: APSTA HostAP control/power wrapper contracts are recovered but not compiled as local witnesses.
- expected system behavior: local APSTA scaffold records exact HostAP mode input offsets, neighbouring owner offsets, feature/private gates, power-off semantics, concurrency mask, and low-power exit gate before enabling a real APSTA owner.
- actual behavior: these operands remained YAML/prose-only.
- exact divergence point: APSTA methods listed in A-APSTA-HOSTAP-CONTROL-POWER-LAYER-072.
- evidence from runtime: no new runtime claim; this is static APSTA HostAP control/power restoration.
- evidence from decomp: APSTA decomp for `setHOST_AP_MODE`, `hostAPPowerOff`, `isSoftAPConcurrencyEnabled`, and `configureLowPowerModeExit`.
- exact semantic mismatch between reference and our code: reference uses fixed owner offsets, gates, input offsets, return semantics, and low-power state transitions; local scaffold did not encode them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this layer, these are direct HostAP control/power operands. The fix does not claim final STA or AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants/layout offsets only; it does not execute HostAP runtime, force state, or add retry/fallback behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_hostap_control_power_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - implement HostAP control as primary STA mode switching.
  - skip proximity/NAN owner offsets.
  - force `state+0x26c`, `state+0x0e`, or `state+0xb4`.
  - add fallback/retry/polling around HostAP mode.
  - call HostAP power-off or low-power exit before APSTA owner runtime is complete.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-146.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the referenced methods directly show owner offsets, gates, input offsets, power-off sequence, concurrency mask, and low-power exit state.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for HostAP control/power scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records reference HostAP operands without enabling runtime.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-PUBLIC-SAP-SLOT-SURFACE-073
- status: CONFIRMED_DEVIATION
- symptom: APSTA HostAP control/power operands are represented, but the complete public SAP vtable surface still has only partial local concrete slot/byte-offset guards.
- first visible manifestation: APSTA public SAP ABI audit after A-APSTA-HOSTAP-CONTROL-POWER-LAYER-072.
- expected system behavior:
  - APSTA public getter methods occupy concrete slots `505..516` and byte offsets `0x0fc8..0x1020`.
  - APSTA public setter methods occupy concrete slots `517..531` and byte offsets `0x1028..0x1098`.
  - every public getter/setter from `getSSID` through `setMIS_MAX_STA` has a local AppleBCMWLAN APSTA slot constant and byte-offset static assert before a real APSTA owner class is introduced.
- actual behavior:
  - local SAP header recorded the typed method list but had concrete AppleBCMWLAN APSTA constants/asserts for only a subset of public slots.
- divergence point:
  - resolved APSTA vtable `0xffffff8001777508`, slots `505..531`, source `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt`.
- evidence:
  - vtable dump: slots `505..516` map to byte offsets `0x0fc8..0x1020`.
  - vtable dump: slots `517..531` map to byte offsets `0x1028..0x1098`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_public_sap_slots_2026_04_27.md`.
- candidate causes:
  - confirmed: local `IO80211SapProtocol.h` did not yet guard every concrete APSTA public getter/setter slot and byte offset.
  - rejected: rely only on typed typedef declarations without concrete APSTA alias asserts.
  - rejected: defer slot guards until the final C++ owner class exists.
- confirmed deviation: concrete APSTA public SAP vtable surface was only partially represented locally.
- root cause: confirmed for public SAP ABI scaffold completeness only; no runtime APSTA method implementation claim is made.
- fix:
  - add AppleBCMWLAN APSTA slot constants for every public getter/setter slot `505..531`.
  - add byte-offset constants and static asserts for the complete surface in `include/Airport/IO80211SapProtocol.h`.
  - update YAML/prose docs and save a local reference note.
- verification:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-147 batch request.
- notes:
  - This is a public ABI scaffold only. It does not define the final APSTA C++ owner class and does not route runtime calls through these slots.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-PUBLIC-SAP-SLOT-SURFACE-073
- symptom: complete APSTA public SAP slot surface is recovered but not fully compiled into local concrete slot guards.
- expected system behavior: local SAP/APSTA header records every public getter/setter slot and byte offset from `getSSID` through `setMIS_MAX_STA`.
- actual behavior: only a subset of the concrete AppleBCMWLAN APSTA aliases had constants and byte-offset static asserts.
- exact divergence point: APSTA vtable `0xffffff8001777508`, slots `505..531`.
- evidence from runtime: no new runtime claim; this is static ABI restoration.
- evidence from decomp: resolved APSTA vtable dump at `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt`.
- exact semantic mismatch between reference and our code: reference has fixed concrete public SAP slots and byte offsets; local code did not yet guard all of them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this ABI layer, the slot/byte-offset constants are the direct reference contract needed before a local APSTA owner class can be safely defined.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact slot numbers and byte offsets only; it does not add method implementations, runtime routing, or fallback behavior.
- files/functions to modify:
  - `include/Airport/IO80211SapProtocol.h`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_public_sap_slots_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - omit concrete APSTA aliases for methods whose bodies are not implemented yet.
  - define the final APSTA class before the full slot surface is guarded.
  - route runtime calls through guessed slots.
  - collapse APSTA public SAP slots into primary STA or reserved base slots.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-147.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the resolved APSTA vtable dump lists exact slots and byte offsets for the full public surface.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for ABI scaffold completeness; no runtime method behavior claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants/static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records the reference ABI surface before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: vtable dump path and reference note are listed above.

## ANOMALY

- id: A-APSTA-PUBLIC-SIMPLE-BODY-CONTRACTS-074
- status: CONFIRMED_DEVIATION
- symptom: APSTA public SAP slot surface is complete, but simple public method bodies still lack local compiled offset/return witnesses.
- first visible manifestation: APSTA method-body audit after A-APSTA-PUBLIC-SAP-SLOT-SURFACE-073.
- expected system behavior:
  - `getSSID(...)` reads length from `state+0x274`, rejects lengths greater than `0x20` with raw `0x16`, writes output length at `+0x04`, copies bytes from `state+0x278` to output `+0x08`, and returns `0`.
  - `getSTATE(...)` writes value `4` at output `+0x04` and returns `0`.
  - `getOP_MODE(...)` rejects null input with raw `0x16`, writes type `1` at output `+0x00`, writes mode `8` at output `+0x04` when `state+0x26c != 0`, otherwise writes `0`, and returns `0`.
  - `getPEER_CACHE_MAXIMUM_SIZE(...)` writes value `8` at output `+0x04` and returns `0`.
  - `getHOST_AP_MODE_HIDDEN(...)` rejects null input with raw `0x16`, writes value `1` at output base, and returns `0`.
  - `getSOFTAP_PARAMS(...)` copies fields from `state+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` to fixed output offsets and returns `0`.
  - `getSOFTAP_STATS(...)` copies `0x58` bytes from `state+0x1b0` and returns `0`.
  - `setSSID(...)` performs optional logging only, does not mutate SSID state, and returns `0`.
  - `setPEER_CACHE_CONTROL(...)` calls `AppleBCMWLANCore::completePeerCacheControl(input, self)` through `state+0x218`, ignores the helper result, and returns `0`.
  - `setSOFTAP_PARAMS(...)` has no null guard, uses input `+0x17` and `state+0x0e`, optionally calls `setBeaconInterval` for input `+0x14 != 0xffff`, copies input fields to state `+0x18/+0x1c/+0x20/+0x24/+0x28`, and returns `0`.
  - `setSOFTAP_EXTENDED_CAPABILITIES_IE(...)` clears state `+0x50/+0x58/+0x60`, copies input `+0x00/+0x01/+0x09` to state `+0x50/+0x51/+0x59`, and returns `0`.
  - `setMIS_MAX_STA(...)` calls `setMaxAssoc(*(uint32_t *)(input+0x00))` only when `state+0x26c != 0`, ignores the helper result, and returns `0`.
- actual behavior:
  - local APSTA scaffold represented some SoftAP carriers, but did not compile the SSID state fields, opmode/state/simple getter layouts, ext-cap input layout, MIS input layout, or exact simple setter return/mutation contracts.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::getSSID(...) @ 0xffffff8001687c84`
  - `AppleBCMWLANIO80211APSTAInterface::getOP_MODE(...) @ 0xffffff8001687e0e`
  - `AppleBCMWLANIO80211APSTAInterface::setPEER_CACHE_CONTROL(...) @ 0xffffff8001688490`
  - `AppleBCMWLANIO80211APSTAInterface::setSSID(...) @ 0xffffff800168dc92`
  - `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_PARAMS(...) @ 0xffffff800168e536`
  - `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_EXTENDED_CAPABILITIES_IE(...) @ 0xffffff800168e7b8`
  - `AppleBCMWLANIO80211APSTAInterface::setMIS_MAX_STA(...) @ 0xffffff8001693a80`
- evidence:
  - decomp: `getSSID` disassembly directly shows `state+0x274`, `state+0x278`, output `+0x04/+0x08`, max length `0x20`, and return `0x16`.
  - decomp: `getOP_MODE` disassembly directly shows null return `0x16`, output type `1`, source `state+0x26c`, and mode values `8` or `0`.
  - decomp: `setPEER_CACHE_CONTROL` disassembly directly shows core pointer `state+0x218`, helper call, ignored result, and return `0`.
  - decomp: `setSSID` disassembly directly shows logging-only behavior and return `0`.
  - decomp: `setSOFTAP_PARAMS` disassembly directly shows input offsets `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x17/+0x18`, state offsets `+0x0e/+0x18/+0x1c/+0x20/+0x24/+0x28/+0x68/+0x26c`, sentinel `0xffff`, power-save calls `(0,0)` and `(1,0)`, and return `0`.
  - decomp: `setSOFTAP_EXTENDED_CAPABILITIES_IE` disassembly directly shows clears at `state+0x50/+0x58/+0x60`, copies from input `+0x00/+0x01/+0x09`, and return `0`.
  - decomp: `setMIS_MAX_STA` disassembly directly shows AP-up gate `state+0x26c`, input dword `+0x00`, `setMaxAssoc`, ignored result, and return `0`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_public_simple_bodies_2026_04_27.md`.
- candidate causes:
  - confirmed: simple public APSTA body contracts were not fully represented as compiled local witnesses.
  - rejected: implement these bodies on the primary STA path.
  - rejected: force AP state, SSID, opmode, or helper success at runtime.
  - rejected: add null guards where reference has no null guard.
- confirmed deviation: local scaffold was missing compiled witnesses for exact simple public body offsets and fixed returns.
- root cause: confirmed for APSTA simple body scaffold completeness only; no final STA association/data root-cause claim is made.
- fix:
  - add constants and layout witnesses for SSID, state, opmode, peer-cache max, hidden mode, SoftAP stats, SoftAP ext-cap input, and MIS max-STA input.
  - split APSTA state block `reserved0274` into `softapSsidLength274`, `softapSsid278[0x20]`, and reserved tail before `rsnConfGate29b`.
  - add static asserts for recovered offsets, fixed values, copy sizes, and simple setter source/target offsets.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-148 batch request.
- notes:
  - `setCIPHER_KEY`, `getSTA_IE_LIST`, `getSTA_STATS`, and `getKEY_RSC` are intentionally left for the next station/key datapath body batch because their contracts include command buffers and IOVAR/IOCTL traffic.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-PUBLIC-SIMPLE-BODY-CONTRACTS-074
- symptom: simple public APSTA method bodies are recovered but not fully compiled into local offset/layout/return witnesses.
- expected system behavior: local APSTA scaffold records exact state/output offsets, input offsets, fixed values, return semantics, and no-null-guard behavior for the simple public methods listed above.
- actual behavior: local scaffold had only partial SoftAP carrier witnesses and lacked exact compiled witnesses for SSID/opmode/simple setter bodies.
- exact divergence point: APSTA methods listed in A-APSTA-PUBLIC-SIMPLE-BODY-CONTRACTS-074.
- evidence from runtime: no new runtime claim; this is static APSTA body-contract restoration.
- evidence from decomp: APSTA disassembly from `/tmp/AppleBCMWLANCoreMac` at the exact addresses listed above.
- exact semantic mismatch between reference and our code: reference uses fixed state/output offsets, fixed values, fixed raw returns, and specific absence of null guards in setters; local scaffold did not encode these body contracts.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA scaffold layer, these constants/layouts are direct ABI/body contracts consumed by public SAP methods. The fix does not claim to resolve the current STA join blocker by itself.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants, state fields, carriers, and static asserts only; it does not route runtime calls, force state, synthesize helper success, or add fallback behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_public_simple_bodies_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - implement these APSTA methods by borrowing primary STA state.
  - force `state+0x26c`, SSID length, opmode, or hidden state.
  - add null guards to `setSOFTAP_PARAMS`, `setSOFTAP_EXTENDED_CAPABILITIES_IE`, or `setMIS_MAX_STA`.
  - preserve helper return from `setPEER_CACHE_CONTROL` or `setMIS_MAX_STA`.
  - include station/key datapath bodies in this simple-body batch without completing their command-buffer audit.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - submit CR-148.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed method disassemblies directly show offsets, constants, return values, and helper-result policies.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for simple public body scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, state fields, and static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records recovered APSTA body contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-STATION-KEY-BODY-CONTRACTS-075
- status: CONFIRMED_DEVIATION
- symptom: APSTA simple public bodies are represented, but station/key public bodies with command buffers still lack compiled local selector/payload/layout witnesses.
- first visible manifestation: APSTA station/key body audit after A-APSTA-PUBLIC-SIMPLE-BODY-CONTRACTS-074.
- expected system behavior:
  - `getSTATION_LIST(...)` rejects null with raw `0x16`, rejects AP-down `state+0x26c == 0` with `0x39`, allocates a `0x100` byte maclist initialized with dword `0x2a`, uses virtual IOCTL get selector `0x9f`, returns `0xe00002bd` on allocation failure, returns `0xe00002d8` on async submit failure, and converts the BCM assoc list on sync success.
  - `setCIPHER_KEY(...)` rejects AP-down with `6`, has no null guard after AP-up passes, reads cipher type from input `+0x08`, accepts cipher types `3` and `5`, returns success for cipher type `0` and unsupported nonzero ciphers, maps to a `0xa4` byte `wl_wsec_key`, and uses virtual IOCTL set selector `0x2d`.
  - `getSTA_IE_LIST(...)` rejects null with raw `0x16`, scans station entries from `state+0xb9` to `state+0x1a9` with stride `0x30` and 6-byte MAC compares, returns `2` when not found, uses IOVAR `wpaie`, and updates output length from output `+0x11` plus `2` on success.
  - `getSTA_STATS(...)` rejects AP-down with `0x39`, rejects null with raw `0x16`, derives allocation size from core-private `+0x30c` with thresholds `7` and `0x0f`, uses IOVAR `sta_info`, copies RX fields `+0x58/+0x68/+0x54/+0x60` to output `+0x0c/+0x10/+0x14/+0x18`, and frees the allocation.
  - `getKEY_RSC(...)` has no null guard, reads key index from input `+0x0e`, uses virtual IOCTL get selector `0xb7`, 8-byte TX payload, RX range `0x0000000800040008`, and writes output length/value at `+0x50/+0x54` on success.
- actual behavior:
  - local APSTA scaffold did not compile these station/key selectors, payload sizes, allocation sizes, station-table offsets, IOVAR names, output offsets, or no-null-guard contracts.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::getSTATION_LIST(...) @ 0xffffff8001687e40`
  - `AppleBCMWLANIO80211APSTAInterface::setCIPHER_KEY(...) @ 0xffffff800168f2b6`
  - `AppleBCMWLANIO80211APSTAInterface::getSTA_IE_LIST(...) @ 0xffffff800168f59c`
  - `AppleBCMWLANIO80211APSTAInterface::getSTA_STATS(...) @ 0xffffff800168f808`
  - `AppleBCMWLANIO80211APSTAInterface::getKEY_RSC(...) @ 0xffffff800168f9e6`
- evidence:
  - decomp: `getSTATION_LIST` disassembly directly shows state gate `+0x26c`, allocation size `0x100`, initial dword `0x2a`, selector `0x9f`, async completion, sync RX range, and return values.
  - decomp: `setCIPHER_KEY` disassembly directly shows AP-up gate, cipher type offset `+0x08`, accepted values `3/5`, `0xa4` `wl_wsec_key`, selector `0x2d`, and unsupported-cipher success return.
  - decomp: `getSTA_IE_LIST` disassembly directly shows station-table scan `+0xb9..+0x1a9`, stride `0x30`, MAC size `6`, IOVAR `wpaie`, and output length rule.
  - decomp: `getSTA_STATS` disassembly directly shows allocation thresholds/sizes, IOVAR `sta_info`, TX MAC offset `+0x04`, output copy offsets, and return values.
  - decomp: `getKEY_RSC` disassembly directly shows key-index offset `+0x0e`, selector `0xb7`, payload sizes/range, and output `+0x50/+0x54`.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_station_key_bodies_2026_04_27.md`.
- candidate causes:
  - confirmed: local scaffold lacked station/key command-buffer body witnesses.
  - rejected: treat these as primary STA key/station methods.
  - rejected: add null guards to `setCIPHER_KEY` or `getKEY_RSC`.
  - rejected: guess station-table depth or key payload sizes from local structs instead of reference.
- confirmed deviation: station/key APSTA public bodies were recovered but not represented as compiled local contracts.
- root cause: confirmed for station/key body scaffold completeness only; no final STA association/data root-cause claim is made.
- fix:
  - add constants for selectors, payload sizes, allocation sizes, station table offsets/stride, IOVAR names, return values, and output offsets.
  - add layout witnesses for maclist, station-table entry, STA IE data, STA stats data, key RSC data, and `wl_wsec_key` size.
  - add static asserts tying state gates, resources, station-table bounds, names, and carriers to recovered offsets.
  - update YAML/prose docs and save a local reference note.
  - keep APSTA runtime disabled in this batch.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-149 batch request.
- notes:
  - This batch is still structural. It does not invoke the station/key methods at runtime and does not change primary STA key programming.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-STATION-KEY-BODY-CONTRACTS-075
- symptom: station/key APSTA public body contracts are recovered but not compiled into local selector/payload/layout witnesses.
- expected system behavior: local APSTA scaffold records exact selectors, payload sizes, allocation sizes, station-table offsets, IOVAR names, returns, and output offsets for `getSTATION_LIST`, `setCIPHER_KEY`, `getSTA_IE_LIST`, `getSTA_STATS`, and `getKEY_RSC`.
- actual behavior: these contracts remained absent or prose-only.
- exact divergence point: APSTA methods listed in A-APSTA-STATION-KEY-BODY-CONTRACTS-075.
- evidence from runtime: no new runtime claim; this is static APSTA station/key body restoration.
- evidence from decomp: APSTA disassembly from `/tmp/AppleBCMWLANCoreMac` at the exact addresses listed above.
- exact semantic mismatch between reference and our code: reference uses fixed command selectors, payload sizes, station-table bounds, output offsets, return values, and no-null-guard bodies; local scaffold did not encode them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA station/key body layer, these are direct command-buffer contracts consumed by public SAP methods. The fix does not claim to resolve the current STA join blocker by itself.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants, state aliases, carriers, names, and static asserts only; it does not execute APSTA station/key runtime or alter primary STA behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_station_key_bodies_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - map APSTA station/key methods to primary STA key/station state.
  - force AP-up state or command success.
  - add fallback/retry/polling around APSTA station/key commands.
  - add null guards not present in reference bodies.
  - invent station table size or key carrier size without disassembly evidence.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-149.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed method disassemblies directly show selectors, offsets, allocation sizes, payload sizes, return values, and IOVAR names.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for station/key body scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, state aliases, names, and static asserts only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it records recovered APSTA command-buffer contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-APSTA-POWER-OFFLOAD-DATAPATH-TAIL-077
- status: CONFIRMED_DEVIATION
- symptom: APSTA event/station-table producer contracts are represented, but the adjacent power/offload/datapath tail remained partially restored and contained one incorrect documented datapath return.
- first visible manifestation: APSTA tail audit after A-APSTA-EVENT-STATION-TABLE-CONTRACTS-076.
- expected system behavior:
  - `configureMPDUSize(uint32_t)` sends `ampdu_mpdu` with 4-byte payload only when core-private `+0x3fc == 2` and `+0x30c <= 4`.
  - low-power exit uses `lphs_mode` payload value `0`; beacon wait-period success uses `lphs_mode` payload value `1`; both payloads are 4 bytes.
  - ARP offload success sends `arp_hostip_clear`, then host-IP clear success reads `state+0xac` and sends `arp_hostip` with 4-byte payload.
  - `setBeaconDutyCycle` sends `rpsnoa` payload `0x10` with header `0x100100101`, mode word `2`, and enable word at `+0x0e`.
  - `configureBeaconDutyCycleParams` sends `rpsnoa` payload `0x18` with header `0x300180101`, mode word `2`, dynamic byte `0x0a - dynamicPSParams[level].byte8`, and rotated qword at `+0x10`.
  - `releaseSoftAPPowerAssertion()` clears `state+0x0c` and notifies event `0x8d` with payload value `0`, size `4`, flag `1`.
  - `softApStatsAccumulatePowerStateDuration(...)` adds duration to `state+0x1d0 + power_state * 0x10` and updates timestamp `state+0x1a8`.
  - `enable(uint32_t)` checks vtable `+0xd58`, calls superclass slot `+0x860` when running, and returns `0xe00002d5` when not running.
  - `disable(uint32_t)` calls vtable `+0xda0` and superclass slot `+0x868`.
  - `enableDatapath()` checks vtable `+0xcf0`; if interface is not enabled it returns `0xe00002bc`, not success.
  - APSTA accessors return state fields `+0x210/+0x2a4/+0x2e8/+0x2f0/+0x2b8->+0x300/+0x320/+0x2d8/+0x2e0`.
  - `setMacAddress(...)` sends `cur_etheraddr` only when interface id is not `-1` and AP-up state `state+0x26c` is zero.
  - `configureSoftAPPeerStats(bool)` is feature-gated by `0x7a`, sends `softap_stats` payload size `0x0e`, and successful callback writes `state+0x328 = cookie & 1`.
- actual behavior:
  - local scaffold lacked compiled witnesses for MPDU/offload/RPSNOA/release/stats/interface-tail/MAC/peer-stats operands.
  - YAML incorrectly stated that APSTA `enableDatapath()` returns success when the interface is not enabled.
- divergence point:
  - `AppleBCMWLANIO80211APSTAInterface::configureMPDUSize(...) @ 0xffffff80016925f6`
  - `configureLowPowerModeExit() @ 0xffffff80016928e4`
  - ARP/low-power callbacks `0xffffff8001692bee..0xffffff8001693195`
  - `setBeaconDutyCycle(...) @ 0xffffff800169319a`
  - `configureBeaconDutyCycleParams(...) @ 0xffffff80016934a0`
  - `releaseSoftAPPowerAssertion() @ 0xffffff80016937c2`
  - `softApStatsAccumulatePowerStateDuration(...) @ 0xffffff8001693892`
  - `enable(...) @ 0xffffff8001693980`
  - `disable(...) @ 0xffffff8001693aa0`
  - `enableDatapath(...) @ 0xffffff8001693b82`
  - `disableDatapath(...) @ 0xffffff8001693e80`
  - accessors `0xffffff8001694064..0xffffff8001694174`
  - `setMacAddress(...) @ 0xffffff8001694464`
  - `configureSoftAPPeerStats(...) @ 0xffffff800169456a`
- evidence:
  - disasm: `configureMPDUSize` tests core-private `+0x3fc` and `+0x30c`, uses IOVAR `ampdu_mpdu`, and sends a 4-byte payload.
  - disasm: low-power/ARP callbacks use `lphs_mode`, `arp_hostip_clear`, `arp_hostip`, payload size `4`, callback cookies `0`, and host IP source `state+0xac`.
  - disasm: RPSNOA methods build fixed `0x10` and `0x18` payloads with the constants and offsets listed above.
  - disasm: release power assertion writes `state+0x0c = 0` and notifies event `0x8d`.
  - disasm: power stats writes duration buckets at `state+0x1d0 + state*0x10` and timestamp `state+0x1a8`.
  - disasm: `enableDatapath` not-enabled branch jumps to the shared failure return `0xe00002bc`; previous YAML text saying success was wrong.
  - docs: `docs/reference/AppleBCMWLAN_APSTA_power_offload_datapath_tail_2026_04_27.md`.
- candidate causes:
  - confirmed: APSTA tail constants/layouts were not compiled into the local scaffold.
  - confirmed: the existing YAML had an incorrect APSTA `enableDatapath` not-enabled return.
  - rejected: execute these APSTA IOVARs at runtime before the APSTA owner class and lifecycle are enabled.
  - rejected: treat APSTA datapath not-enabled as success.
- confirmed deviation: APSTA power/offload/datapath tail operands are recovered in reference but not represented locally; one local doc statement contradicted the disassembly.
- root cause: confirmed for APSTA tail scaffold completeness only. No final primary STA association/data or AP runtime root cause is claimed.
- fix:
  - add constants, IOVAR names, state aliases, and layout witnesses for MPDU, low-power/ARP, RPSNOA, release assertion, power stats, enable/disable, datapath gate/accessors, MAC set, and SoftAP peer stats.
  - split APSTA state fields for `state+0xac` ARP host IP and `state+0x328` SoftAP peer-stats enabled state.
  - correct YAML `enableDatapath` not-enabled return to `0xe00002bc`.
  - add reference/YAML/prose documentation.
  - keep APSTA runtime disabled.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-151.

## FIX_CANDIDATE

- anomaly_id: A-APSTA-POWER-OFFLOAD-DATAPATH-TAIL-077
- symptom: APSTA tail contracts after station-table producer were incomplete and one datapath not-enabled return was documented incorrectly.
- expected system behavior: local APSTA scaffold records exact MPDU/offload/RPSNOA/release/stats/interface/datapath/MAC/peer-stats constants, payload layouts, state offsets, and return semantics.
- actual behavior: these facts were absent or prose-only; `enableDatapath` not-enabled path was incorrectly described as success.
- exact divergence point: APSTA functions listed in A-APSTA-POWER-OFFLOAD-DATAPATH-TAIL-077.
- evidence from runtime: no new runtime claim; this is static APSTA tail restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference uses fixed command names, payload sizes, state offsets, vtable gates, and return values; local scaffold did not encode them and had a wrong YAML return for one branch.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this APSTA owner-tail layer, the recovered disassembly directly defines the missing ABI/semantic contracts. The fix does not claim final STA/AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants/layout/static asserts/docs only; it does not execute IOVARs, force state, synthesize success, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_APSTA_power_offload_datapath_tail_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - call APSTA MPDU/low-power/ARP/RPSNOA/peer-stats IOVARs from primary STA runtime.
  - treat APSTA `enableDatapath` not-enabled path as success.
  - force AP-up, low-power, peer-stats, MAC address, or datapath state.
  - add fallback/retry/poll or suppress callback errors.
  - enable role-7 APSTA creation in this structural batch.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-151.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed APSTA disassembly directly shows offsets, IOVAR names, payload sizes, vtable offsets, return values, and state writes.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for APSTA tail scaffold completeness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, layout witnesses, state fields, YAML correction, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered APSTA contracts before runtime implementation.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-WCL-ACTION-FRAME-SEND-CONTRACTS-081
- status: CONFIRMED_DEVIATION
- symptom: WCL outbound action-frame sender preserved the visible oversized fail shape, but local V1 transport length and local cache capacity did not match Apple.
- first visible manifestation: WCL action-frame static audit after A-APSTA-ACTION-FRAME-LPHS-CONTRACTS-080.
- expected system behavior:
  - `AppleBCMWLANCore::setWCL_ACTION_FRAME(...)` rejects `NULL` with `0xe00002bc`.
  - the caller carrier uses category `+0x00`, channel `+0x04`, peer address `+0x08`, frame length `+0x0e`, and frame bytes `+0x10`.
  - V2 is selected when core-private firmware generation `+0x30c > 0x14`.
  - V1 `sendActionFrame(...)` accepts total action-frame bytes up to `0x707`, zeroes a `0x718` buffer, and sends fixed IOVAR CommandTxPayload length `0x724`.
  - V2 `sendActionFrameV2(...)` rejects total bytes `>= 0x708`, allocates `total + 0x34`, and uses issue-command dispatch.
- actual behavior:
  - local V1 dispatch used `frameLen` as the request length instead of the fixed `0x724` V1 payload size.
  - local last-action-frame cache was limited to `0x200` bytes while the recovered sender capacity is `0x708`.
  - V2 threshold and capacity values were literals rather than a single recovered contract.
- divergence point:
  - `AppleBCMWLANCore::setWCL_ACTION_FRAME(...) @ 0xffffff8001636ab4`.
  - `AppleBCMWLANNetAdapter::sendActionFrame(...) @ 0xffffff8001549050`.
  - `AppleBCMWLANNetAdapter::sendActionFrameV2(...) @ 0xffffff8001549322`.
- evidence:
  - disasm: core path tests input null and returns `0xe00002bc`.
  - disasm: core path reads firmware generation at core-private `+0x30c` and selects V2 when it is above `0x14`.
  - disasm: V1 path checks total bytes against `0x707`, zeroes `0x718`, and uses CommandTxPayload length `0x724`.
  - disasm: V2 path rejects total bytes `>= 0x708`, allocates `total + 0x34`, and sends the issue-command path.
  - docs: `docs/reference/AppleBCMWLAN_WCL_action_frame_send_2026_04_27.md`.
- candidate causes:
  - confirmed: local V1 commander dispatch length did not match the recovered Apple fixed payload length.
  - confirmed: local cached action-frame capacity was narrower than the recovered sender capacity.
  - rejected: synthesize action-frame send success without matching the transport contract.
  - rejected: implement real Broadcom adapter injection in this structural batch.
- confirmed deviation: reference V1 sends fixed `0x724` payload and both send paths use `0x708` capacity / `0x707` maximum; local V1 length/cache capacity did not encode that contract.
- root cause: confirmed for WCL action-frame sender contract correctness only. No final primary STA association/data or AP runtime root cause is claimed.
- fix:
  - add named action-frame capacity, maximum payload, V1 fixed payload, and V2 threshold constants.
  - route local V1 dispatch with request size `0x724`.
  - expand local cached action-frame buffer to `0x708`.
  - add reference/YAML/prose documentation.
- verification:
  - header syntax.
  - YAML parse for APSTA and new WCL action-frame YAML.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-155.

## FIX_CANDIDATE

- anomaly_id: A-WCL-ACTION-FRAME-SEND-CONTRACTS-081
- symptom: WCL outbound action-frame sender V1 request length and local cache capacity did not match Apple.
- expected system behavior: local Tahoe commander records V1 fixed payload size `0x724`, V2 dynamic request length, capacity `0x708`, maximum accepted payload `0x707`, and threshold `0x15`.
- actual behavior: local V1 request length was only `frameLen`, local cache was `0x200`, and threshold/capacity literals were not tied to the recovered contract.
- exact divergence point: WCL action-frame functions listed in A-WCL-ACTION-FRAME-SEND-CONTRACTS-081.
- evidence from runtime: no new runtime claim; this is static WCL sender contract restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference V1 dispatch sends fixed `0x724` bytes and both paths share `0x708` capacity; local V1 dispatch and cache capacity did not.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this sender layer, the recovered disassembly directly defines the transport payload contract. The fix does not claim final association or AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants and local dispatch/cache sizes only; it does not synthesize frames, force success, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/TahoePayloadBuilders.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `AirportItlwm/TahoeCommanderV2.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_WCL_action_frame_send_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/97_wcl_action_frame_send_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - synthesize action-frame success without matching V1/V2 transport shape.
  - force V2 for every path as an undocumented workaround.
  - keep truncating local action-frame cache to `0x200`.
  - implement Broadcom adapter injection in this batch without full backend evidence.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-155.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed WCL/core/net-adapter disassembly directly shows threshold, offsets, max length, fixed V1 payload size, and V2 allocation path.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for WCL action-frame sender contract correctness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, request-size routing, cache capacity, YAML, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered WCL sender contracts before deeper backend injection.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-WCL-ACTION-FRAME-PROGRESS-CONTRACTS-082
- status: CONFIRMED_DEVIATION
- symptom: WCL action-frame sender state existed without the adjacent Apple progress/overdue contract that gates scans during in-flight action-frame completion.
- first visible manifestation: WCL action-frame static audit after A-WCL-ACTION-FRAME-SEND-CONTRACTS-081.
- expected system behavior:
  - `AppleBCMWLANCore::setActionFrameProgress(bool)` stores the progress byte at core-private `+0x4478`.
  - `AppleBCMWLANCore::getActionFrameProgress()` calls `checkActionFrameCompleteOverdue()` and then returns bit 0 from `+0x4478`.
  - `checkActionFrameCompleteOverdue()` compares unsigned elapsed milliseconds against `0x12d`, using the start timestamp at `+0x4480`.
  - on overdue, Apple clears `+0x4478`, logs line `0x3b1d`, and emits status `0xe3ff852b` through line `0x3b1e`.
  - `AppleBCMWLANScanAdapter::startScan(...)` calls the overdue check and rejects scan with `0xe00002d5` / line `0x00a5` if progress remains set.
- actual behavior:
  - local Tahoe action-frame owner recorded the last action-frame payload but had no progress flag witness.
  - local code had no progress start-ms witness, overdue threshold, overdue status, scan reject status, or get-before-check helper semantics.
- divergence point:
  - `AppleBCMWLANCore::checkActionFrameCompleteOverdue() @ 0xffffff80015ba4d2`.
  - `AppleBCMWLANCore::setActionFrameProgress(bool) @ 0xffffff80016344aa`.
  - `AppleBCMWLANCore::getActionFrameProgress() @ 0xffffff80016344be`.
  - `AppleBCMWLANScanAdapter::startScan(...) @ 0xffffff80016ccc7a`.
- evidence:
  - disasm: `setActionFrameProgress` stores `%sil` to core-private `+0x4478`.
  - disasm: `getActionFrameProgress` calls `checkActionFrameCompleteOverdue` before reading `+0x4478` bit 0.
  - disasm: `checkActionFrameCompleteOverdue` reads `+0x4480`, compares against `0x12d`, clears `+0x4478`, logs line `0x3b1d`, and emits `0xe3ff852b` through line `0x3b1e`.
  - disasm: `setupDriver` clears `+0x4478`.
  - disasm: `startScan` calls the overdue check, tests `+0x4478`, and rejects with `0xe00002d5` / log line `0x00a5`.
  - docs: `docs/reference/AppleBCMWLAN_WCL_action_frame_progress_2026_04_27.md`.
- candidate causes:
  - confirmed: local owner state did not encode the progress/overdue contract adjacent to the recovered action-frame sender.
  - confirmed: local docs/YAML lacked the scan rejection edge for action-frame progress.
  - insufficient data: exact timestamp producer and completion-clear lifecycle outside these recovered functions.
  - rejected: enable local scan rejection using a guessed timestamp.
- confirmed deviation: reference has progress flag, start timestamp, overdue check, and scan rejection contracts; local owner registry had none of these witnesses.
- root cause: confirmed for WCL action-frame progress contract incompleteness only. No final association/data/AP runtime root cause is claimed.
- fix:
  - add named constants for progress flag/start-ms offsets, overdue threshold, overdue status/log lines, and scan reject status/log line.
  - add `progress` and `progressStartMs` witnesses to `TahoeOwnerRegistry::ActionFrameOwner`.
  - add pure helper methods for `setActionFrameProgress`, `checkActionFrameCompleteOverdue`, and `getActionFrameProgress`.
  - add reference/YAML/prose documentation.
- verification:
  - header syntax.
  - YAML parse for APSTA and WCL YAML.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-156.

## FIX_CANDIDATE

- anomaly_id: A-WCL-ACTION-FRAME-PROGRESS-CONTRACTS-082
- symptom: WCL action-frame progress/overdue state and scan reject contract were absent from the local recovered Tahoe owner layer.
- expected system behavior: local Tahoe owner layer records the Apple progress byte, start timestamp, overdue threshold, overdue clear, status/log constants, and get-before-check semantics.
- actual behavior: local code recorded action-frame payload send shape but not the adjacent progress/overdue state.
- exact divergence point: WCL action-frame progress functions listed in A-WCL-ACTION-FRAME-PROGRESS-CONTRACTS-082.
- evidence from runtime: no new runtime claim; this is static WCL progress contract restoration.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference stores progress at `+0x4478`, reads start-ms at `+0x4480`, clears progress after unsigned elapsed `>= 0x12d`, and scan rejects with `0xe00002d5` while progress remains set; local owner registry had no equivalent state or helper semantics.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this progress layer, the recovered disassembly directly defines the state contract and scan gate. The fix does not claim final association/data or AP runtime root cause.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants and pure owner helper semantics only; it does not synthesize timestamps, force completion, enable scan gating, or add fallback/retry/poll behavior.
- files/functions to modify:
  - `AirportItlwm/TahoePayloadBuilders.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_WCL_action_frame_progress_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/98_wcl_action_frame_progress_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - enable scan rejection from local scan path before recovering timestamp producer and completion-clear lifecycle.
  - synthesize a progress timestamp.
  - force action-frame completion or clear progress on local timeout.
  - suppress scans or add retry/poll logic around scan requests.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-156.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: the listed WCL/core/scan-adapter disassembly directly shows progress offset, start-ms offset, overdue threshold, clear, status, and scan reject status.
- Есть ли прямое подтверждение по runtime-данным? No runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for WCL action-frame progress contract correctness; no final STA/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes: exact constants, owner-state witnesses, pure helper semantics, YAML, and docs only.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores recovered WCL progress contracts before enabling runtime scan gating.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-TAHOE-CONTROLLER-QUEUE-MULTICAST-CAPACITY-083
- status: CONFIRMED_DEVIATION
- symptom: Tahoe controller queue/depth/capacity and promiscuous/multicast methods did not match recovered AppleBCMWLANCore and IO80211Family contracts.
- first visible manifestation: static audit after WCL action-frame progress recovery and the user-requested multicast/datapath/depth/capacity layer review.
- expected system behavior:
  - `requestQueueSizeAndTimeout` returns `0xe00002c7` unless both `wlan.coalesce.qsize` and `wlan.coalesce.timeout` low 16-bit values are nonzero, then writes both outputs and returns success.
  - `getDataQueueDepth(OSObject*)` returns the AppleBCMWLANCore ring depth at core-private `+0x1154`, initialized to `0x200`.
  - `getActionFramePoolCapacity()` returns `0x100`.
  - `setPromiscuousMode(bool)` stores the requested bool at core-private `+0x4778`.
  - multicast mode/list share reject gate `+0x2891` bit `0x80`, max list count `0x20`, count offset `+0x234`, list offset `+0x238`, 6-byte entries, payload fill `0xaa`, payload capacity `0xca`, and IOVAR `mcast_list`.
- actual behavior:
  - local `requestQueueSizeAndTimeout` returned success unconditionally and wrote no outputs.
  - local Tahoe controller inherited the IO80211 default data queue depth path instead of exposing AppleBCMWLANCore `0x200` ring depth.
  - local action-frame pool capacity was not explicit.
  - local promiscuous/multicast requested state and multicast-list Apple cache/limit had no Tahoe owner witnesses.
- divergence point:
  - `AppleBCMWLANCore::requestQueueSizeAndTimeout(...) @ 0xffffff8001583018`.
  - `AppleBCMWLANCore::fetchAndUpdateRingParameters() @ 0xffffff800159418a`.
  - `AppleBCMWLANCore::getDataQueueDepth(OSObject*) @ 0xffffff8001634388`.
  - `AppleBCMWLANCore::setPromiscuousMode(bool) @ 0xffffff80015e07cc`.
  - `AppleBCMWLANCore::setMulticastMode(bool) @ 0xffffff80015e07ec`.
  - `AppleBCMWLANCore::setMulticastList(ether_addr const*, unsigned int) @ 0xffffff80015e0930`.
  - `IO80211Controller::getActionFramePoolCapacity() @ 0xffffff800221a26e`.
  - `IO80211SkywalkInterface::getDataQueueDepth() @ 0xffffff8002276f66`.
- evidence:
  - disasm: `requestQueueSizeAndTimeout` reads the two coalesce DT parameters and returns `0xe00002c7` unless both are nonzero.
  - disasm: success path writes `*queue` and `*timeout` before returning `0`.
  - disasm: `fetchAndUpdateRingParameters` writes default `0x200` to core-private `+0x1154`.
  - disasm: `getDataQueueDepth` returns `movzwl 0x1154(%rax), %eax`.
  - disasm: IO80211 base `getDataQueueDepth` returns `0x400`.
  - disasm: IO80211SkywalkInterface dispatches `getDataQueueDepth` through the controller vtable.
  - disasm: IO80211 base `getActionFramePoolCapacity` returns `0x100`.
  - disasm: `setPromiscuousMode` stores `%sil` at core-private `+0x4778`.
  - disasm: multicast mode/list use reject gate `+0x2891` bit `0x80`, status `0xe0823804`, and IOVAR `mcast_list`.
  - disasm: multicast list rejects count `> 0x20` with `0xe00002bc`, stores count/list at `+0x234/+0x238`, and builds a `4 + count * 6` payload in a `0xca` byte buffer filled with `0xaa`.
  - docs: `docs/reference/AppleBCMWLAN_controller_queue_multicast_capacity_2026_04_27.md`.
- candidate causes:
  - confirmed: local queue-size method reported success without satisfying the output contract.
  - confirmed: local data queue depth could expose the IO80211 base default rather than AppleBCMWLANCore ring depth.
  - confirmed: local owner registry lacked controller promiscuous/multicast state witnesses.
  - insufficient data: exact Broadcom multicast IOVAR backend and APSTA virtual-interface multicast lifecycle.
  - rejected: add guessed queue sizes when DT/local properties are absent.
  - rejected: issue Broadcom `mcast_list` firmware IOVAR without recovered local commander owner path.
- confirmed deviation: reference queue/depth/capacity and multicast/promiscuous contracts above were absent or mismatched locally.
- root cause: confirmed for controller queue/depth/capacity and multicast/promiscuous contract completeness only. Final primary STA association/data and AP runtime success are not claimed.
- fix:
  - add Tahoe controller contract constants and static asserts.
  - add controller owner state for promiscuous mode, multicast mode/list, data depth, and coalesce outputs.
  - make `requestQueueSizeAndTimeout` return unsupported unless both local coalesce properties are nonzero, then write both outputs and cache them.
  - add `getDataQueueDepth` override returning owner default `0x200`.
  - add explicit `getActionFramePoolCapacity` override returning `0x100`.
  - cache promiscuous/multicast requests and reject multicast-list counts above `0x20`.
  - add reference/YAML/prose documentation.
- verification:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-157.

## FIX_CANDIDATE

- anomaly_id: A-TAHOE-CONTROLLER-QUEUE-MULTICAST-CAPACITY-083
- symptom: Tahoe controller queue/depth/capacity and promiscuous/multicast contracts diverged from Apple.
- expected system behavior: local controller exposes Apple queue-size return semantics, AppleBCMWLANCore data depth default, action-frame capacity, promiscuous state, and multicast limit/cache contracts.
- actual behavior: local queue-size method returned success without writes, local data depth/capacity was not explicit, and multicast/promiscuous owner witnesses were absent.
- exact divergence point: controller functions listed in A-TAHOE-CONTROLLER-QUEUE-MULTICAST-CAPACITY-083.
- evidence from runtime: no new runtime claim; this is static controller contract restoration adjacent to the active association/data blocker.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` and IO80211Family disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference queue-size returns unsupported unless both outputs can be written; reference data-depth returns Apple ring depth `+0x1154` default `0x200`; reference stores promiscuous/multicast/list owner state and enforces max count `0x20`; local code did not.
- fix justification path: REFERENCE_ALIGNMENT_FIX for constants, statuses, offsets, limits, return values, and owner-state witnesses; SYSTEM_CONTRACT_FIX for reading local coalesce values through IOService properties because the closed Apple `getDTParameter32` helper is not available locally.
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: `requestQueueSizeAndTimeout` output pointers and return value.
  - expected contract at each touchpoint: write both 16-bit outputs only when both values are nonzero; otherwise return `0xe00002c7`.
  - why no relevant touchpoints are missing: the caller-visible contract is limited to two output pointers and one return code; no firmware state or IO80211 event is emitted by this method.
  - why proposed path adds no extra system-visible side effects: it only reads local IOService properties and caches values in private owner state.
- why this is root cause and not just correlation: for this layer, the recovered disassembly directly defines framework-facing method contracts. The fix does not claim final association/data root cause.
- why proposed fix is 1:1 with reference architecture and semantics: exact constants, return statuses, queue-depth default, capacity, multicast limit, and owner-state witnesses are restored; unrecovered Broadcom multicast IOVAR dispatch is intentionally not synthesized.
- files/functions to modify:
  - `AirportItlwm/TahoeControllerContracts.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_controller_queue_multicast_capacity_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/99_controller_queue_multicast_capacity_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - keep returning queue-size success without writing outputs.
  - synthesize hardcoded coalesce values when no local property is present.
  - inherit IO80211 base `0x400` queue depth after AppleBCMWLANCore override was confirmed.
  - issue Broadcom `mcast_list` IOVAR or virtual IOVAR without recovered local owner path.
  - add retry, poll, fallback, forced state, or suppression.
- verification plan:
  - header syntax.
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-157.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: exact AppleBCMWLANCore and IO80211Family function addresses and constants are listed above.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for controller method contract correctness; no final association/data/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes for constants, offsets, statuses, capacity, depth default, and caller-visible queue semantics. Broadcom multicast IOVAR dispatch is explicitly not claimed.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores confirmed framework-facing contracts adjacent to the active blocker.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-TAHOE-HIDDEN-INTERFACE-FLOW-TIMESTAMP-084
- status: CONFIRMED_DEVIATION
- symptom: hidden `+0x1510` flow/timestamp/log-pipe/virtual-interface surface was only partially represented locally and one flow release path had a non-reference debug-log side effect.
- first visible manifestation: hidden-interface static audit after controller queue/multicast/capacity recovery.
- expected system behavior:
  - hidden interface-side owner offset `+0x1510` records flow queue delegation slots `+0xa68/+0xa70/+0xa78`.
  - unsupported flow IDs fall back to base request/release slots `+0xd60/+0xd68`.
  - hidden flow request operands come from metadata pointer, `metadata+0x06`, `metadata+0x0c`, and `metadata+0x10`.
  - packet timestamp enable/disable use base slots `+0xd90/+0xd98`, command-gate actions, and gated hidden slots `+0xaa8/+0xab0`.
  - log pipes come from hidden object `+0x88` at offsets `+0x218/+0x220/+0x230`.
  - virtual-interface lifecycle delegates through base slots `+0xe10/+0xd40/+0xd48`, null status `0xe00002bc`, proximity owner `+0x2c28`, role `6`, wake flag `0x10000`.
- actual behavior:
  - local code had no compiled constants for these hidden-interface slots and offsets.
  - local `flowIdSupported` was a literal false instead of a recoverable owner-state witness.
  - local `releaseFlowQueue` logged every call up to a local debug limit, which Apple does not do on the no-op fallback path.
- divergence point:
  - `AppleBCMWLANCore::flowIdSupported() @ 0xffffff80015b7a98`.
  - `AppleBCMWLANCore::releaseFlowQueue(IO80211FlowQueue*) @ 0xffffff80015b7ab4`.
  - `AppleBCMWLANCore::requestFlowQueue(FlowIdMetadata const*) @ 0xffffff80015b7b10`.
  - `AppleBCMWLANCore::enablePacketTimestamping() @ 0xffffff800162da9c`.
  - `AppleBCMWLANCore::enablePacketTimestampingGated() @ 0xffffff800162db4e`.
  - `AppleBCMWLANCore::disablePacketTimestamping() @ 0xffffff800162db6a`.
  - `AppleBCMWLANCore::disablePacketTimestampingGated() @ 0xffffff800162dc1c`.
  - `AppleBCMWLANCore::getLogPipes(CCPipe**, CCPipe**, CCPipe**) @ 0xffffff8001634230`.
  - `AppleBCMWLANCore::createVirtualInterface(...) @ 0xffffff80015fc952`.
  - `AppleBCMWLANCore::enableVirtualInterface(...) @ 0xffffff80015fc964`.
  - `AppleBCMWLANCore::disableVirtualInterface(...) @ 0xffffff80015fcb28`.
- evidence:
  - disasm: `flowIdSupported` loads core-private `+0x1510` and tail-calls hidden slot `+0xa68`.
  - disasm: `requestFlowQueue` tests hidden slot `+0xa68`, uses base fallback `+0xd60`, and calls hidden slot `+0xa70` with recovered metadata operands.
  - disasm: `releaseFlowQueue` uses hidden slot `+0xa78` when supported, otherwise base fallback `+0xd68`.
  - disasm: timestamp enable/disable call base slots `+0xd90/+0xd98` and gated hidden slots `+0xaa8/+0xab0`.
  - disasm: `getLogPipes` reads hidden object `+0x88`, then `+0x218/+0x220/+0x230`.
  - disasm: virtual-interface lifecycle delegates through base slots `+0xe10/+0xd40/+0xd48`; null path returns `0xe00002bc`; role `6` path involves `+0x2c28` and wake flag `0x10000`.
  - docs: `docs/reference/AppleBCMWLAN_hidden_interface_flow_timestamp_2026_04_27.md`.
- candidate causes:
  - confirmed: hidden-interface constants and owner witnesses were absent.
  - confirmed: local `releaseFlowQueue` added a debug-log side effect not present in the recovered fallback path.
  - insufficient data: exact local flow-queue backend, hidden timestamp backend, and APSTA/proximity virtual-interface runtime owner.
  - rejected: synthesize flow queues while `flowIdSupported` local owner is false.
  - rejected: enable hidden timestamping without a recovered timestamp owner backend.
- confirmed deviation: local structural layer did not encode hidden `+0x1510` flow/timestamp/log/virtual-interface slots and had a local-only debug side effect.
- root cause: confirmed for hidden-interface structural completeness only. No final association/data/AP runtime root cause is claimed.
- fix:
  - add `TahoeHiddenInterfaceContracts.hpp`.
  - add hidden-interface owner witnesses to `TahoeOwnerRegistry`.
  - make `flowIdSupported` return owner state, default false.
  - remove debug logging from `releaseFlowQueue` and keep only a private owner witness.
  - add reference/YAML/prose documentation.
- verification:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-158.

## FIX_CANDIDATE

- anomaly_id: A-TAHOE-HIDDEN-INTERFACE-FLOW-TIMESTAMP-084
- symptom: hidden `+0x1510` flow/timestamp/log-pipe/virtual-interface surface was not locally recoverable and release-flow logging introduced a non-reference side effect.
- expected system behavior: local layer records exact hidden-interface slots/offsets/statuses and does not add debug logging to the flow release fallback path.
- actual behavior: local code had no constants/owner witnesses for these slots and logged in `releaseFlowQueue`.
- exact divergence point: hidden-interface functions listed in A-TAHOE-HIDDEN-INTERFACE-FLOW-TIMESTAMP-084.
- evidence from runtime: no new runtime claim; this is static hidden-interface structural recovery.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference delegates through hidden `+0x1510` slots or base fallback slots; local code had no structural owner witnesses and added a debug-log side effect on release.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this layer, the recovered disassembly directly defines slots, operands, statuses, and fallback behavior. The fix does not claim final runtime association/data/AP success.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact hidden-interface constants and owner witnesses, keeps flow IDs disabled by default, preserves base request fallback by not overriding `requestFlowQueue`, and removes a local-only log side effect.
- files/functions to modify:
  - `AirportItlwm/TahoeHiddenInterfaceContracts.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_hidden_interface_flow_timestamp_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/100_hidden_interface_flow_timestamp_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - synthesize flow queues with no recovered local flow owner.
  - override `requestFlowQueue` while flow IDs are false instead of preserving base fallback.
  - enable packet timestamping hidden slots with no recovered timestamp owner backend.
  - enable APSTA/proximity virtual-interface runtime from this structural batch.
  - add retry, poll, fallback, forced state, or synthetic objects.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-158.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: exact AppleBCMWLANCore addresses and vtable offsets are listed above.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for hidden-interface structural contract correctness; no final association/data/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes for structural constants, owner witnesses, default flow-disabled state, and removal of local-only release logging.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No: it restores confirmed hidden-interface contracts without pretending to implement unrecovered backends.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes: exact function addresses and reference note are listed above.

## ANOMALY

- id: A-TAHOE-QOS-DYNSAR-OFFSETS-085
- status: CONFIRMED_DEVIATION
- symptom: Q11-C1 QoS / DynSAR / congestion-control helper offsets were not locally represented as recovered owner state.
- first visible manifestation: post-CR-158 static audit of nearby AppleBCMWLANCore helper methods.
- expected system behavior:
  - DynSAR fail-safe helper reads start ticks at core-private `+0x74e0` and returns `((now - start) >> 0x0a) < 0x9502f9`.
  - congestion-control helpers test core-private `+0x7584` bit `0`, returning `0` when set and `0xe00002c7` when clear.
  - AWDL AMPDU force flags are stored at `+0x3768/+0x3764`.
  - hardware feature flags are read from `+0x458c`.
  - split-TX status reads bit 0 from `+0x00dc`.
  - TX address resolution counters are read from `+0x2aa4/+0x2aa8`.
- actual behavior:
  - local code had no compiled constants or owner witnesses for these recovered offsets and helper statuses.
- divergence point:
  - `AppleBCMWLANCore::wasDynSARInFailSafeMode() @ 0xffffff8001632052`.
  - `AppleBCMWLANCore::configureCongestionControlMechanisms(...) @ 0xffffff8001632144`.
  - `AppleBCMWLANCore::configureAggregationCongestionControlMechanism(...) @ 0xffffff8001632162`.
  - `AppleBCMWLANCore::forceAwdlAmpdu() @ 0xffffff800163428c`.
  - `AppleBCMWLANCore::setForceAwdlAmpdu(...) @ 0xffffff80016342a0`.
  - `AppleBCMWLANCore::forceDisableAwdlAmpdu() @ 0xffffff80016342b4`.
  - `AppleBCMWLANCore::setForceDisableAwdlAmpdu(...) @ 0xffffff80016342c8`.
  - `AppleBCMWLANCore::getHwFeatureFlags() const @ 0xffffff80016342dc`.
  - `AppleBCMWLANCore::isSplitTxStatusEnabled() @ 0xffffff800163434a`.
  - `AppleBCMWLANCore::getTxAddrResolveReqV4() @ 0xffffff8001634360`.
  - `AppleBCMWLANCore::getTxAddrResolveReqV6() @ 0xffffff8001634374`.
- evidence:
  - disasm: DynSAR helper reads `+0x74e0`, shifts elapsed by `0x0a`, compares against `0x9502f9`, and logs line `0xdea9`.
  - disasm: congestion helpers test `+0x7584` bit 0 and return success or `0xe00002c7`.
  - disasm: AWDL AMPDU accessors read/write `+0x3768/+0x3764`.
  - disasm: feature/split/address helpers read `+0x458c`, `+0x00dc`, `+0x2aa4`, and `+0x2aa8`.
  - docs: `docs/reference/AppleBCMWLAN_qos_dynsar_offsets_2026_04_27.md`.
- candidate causes:
  - confirmed: local owner registry lacked the Q11-C1 helper state container.
  - confirmed: local docs/YAML lacked these recovered offsets.
  - rejected: enable QoS IOVARs without full backend recovery.
  - rejected: force DynSAR/congestion/AMPDU/split-TX/address-resolution state.
- confirmed deviation: recovered Q11-C1 offsets/statuses were absent from local compiled witnesses.
- root cause: confirmed for QoS/DynSAR structural completeness only. No final association/data/AP runtime root cause is claimed.
- fix:
  - add `TahoeQosDynsarContracts.hpp`.
  - add `QosDynsarOwner` witnesses to `TahoeOwnerRegistry`.
  - add pure helper semantics for DynSAR fail-safe and congestion feature gate.
  - add reference/YAML/prose documentation.
- verification:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-159.

## FIX_CANDIDATE

- anomaly_id: A-TAHOE-QOS-DYNSAR-OFFSETS-085
- symptom: Q11-C1 QoS/DynSAR helper offsets and statuses were not locally recoverable.
- expected system behavior: local owner layer records exact Apple offsets, bit masks, threshold, and unsupported status for the recovered helpers.
- actual behavior: local code had no constants or owner witnesses for these helpers.
- exact divergence point: QoS/DynSAR helper functions listed in A-TAHOE-QOS-DYNSAR-OFFSETS-085.
- evidence from runtime: no new runtime claim; this is static owner-layer recovery.
- evidence from decomp: Tahoe `AppleBCMWLANCoreMac` disassembly listed above and summarized in the new reference note.
- exact semantic mismatch between reference and our code: reference has concrete core-private offsets and status semantics; local code did not encode them.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: for this layer, the recovered disassembly directly defines offsets/statuses. The fix does not claim final runtime behavior.
- why proposed fix is 1:1 with reference architecture and semantics: it records exact constants and pure owner witnesses only; it does not synthesize policy state or issue QoS IOVARs.
- files/functions to modify:
  - `AirportItlwm/TahoeQosDynsarContracts.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_qos_dynsar_offsets_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/101_qos_dynsar_offsets_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
- forbidden alternative fixes considered and rejected:
  - call QoS IOVARs without backend recovery.
  - force DynSAR fail-safe state.
  - force congestion-control support.
  - force AWDL AMPDU, split-TX, or TX address-resolution counters.
  - add retry, poll, fallback, forced state, or synthetic policy.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-159.

## SELF-CHECK

- Есть ли у меня прямое подтверждение по декомпилу? Yes: exact AppleBCMWLANCore addresses and offsets are listed above.
- Есть ли прямое подтверждение по runtime-данным? No new runtime claim is made.
- Доказал ли я причинность, а не просто корреляцию? Yes for Q11-C1 owner-layer completeness; no final association/data/AP runtime claim is made.
- Повторяет ли мой фикс архитектуру и семантику эталона 1:1? Yes for constants, helper semantics, statuses, and owner witnesses.
- Не добавляю ли я эвристику, fallback, workaround, suppression, forced synchronization, guessed state correction? No.
- Не закрываю ли я симптом вместо причины? No.
- Могу ли я показать конкретные ссылки на reference decomp, наш код, точку расхождения, тест / лог / trace? Yes.

## ANOMALY

- id: A-ASSOC-RSN-CARRIER-OWNER-160
- status: FIX_IMPLEMENTED
- symptom: primary STA join reaches WCL join/RX-EAPOL boundary, but RSN/key/data completion remains absent; the hidden association carrier and local RSN IE compatibility handoff still had unrecovered owner details.
- first visible manifestation: current post-CR-114/CR-159 static audit of the active WCL association layer.
- expected system behavior:
  - hidden association carriers use selectors `0x45/0x46` and exact assoc-candidates payload length `0x3ad8`.
  - WCL association carrier fields are parsed from exact offsets: auth `+0x10/+0x14/+0x18`, SSID `+0x20/+0x1c`, RSN IE `+0xd6/+0xd4`, Instant Hotspot `+0x1e0/+0x1e1`, PMF `+0x217`, BSS info `+0x214`, and candidate list `+0x218/+0x220/+0x226/+0x22c`.
  - JoinAdapter RSN programming uses pointer+length semantics.
  - Direct `AppleBCMWLANCore::setRSN_IE(...)` returns success and does not copy a fixed local stack buffer.
- actual behavior:
  - local active WCL association code still carried several hidden-assoc constants as local magic offsets.
  - local `setRSN_IE(...)` copied `APPLE80211_MAX_RSN_IE_LEN` bytes from `apple80211_rsn_ie_data::ie`, even when the caller populated only `len` bytes.
- divergence point:
  - `AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS(...)`
  - `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)`
  - `AirportItlwmSkywalkInterface::setRSN_IE(...)`
  - `AirportItlwm::setRSN_IE(...)`
- evidence:
  - decomp: `AppleBCMWLANCore::setWCL_ASSOCIATE(...) @ 0xffffff80015fbacc` reads auth/SSID/RSN/candidate fields and delegates the carrier to JoinAdapter.
  - decomp: `AppleBCMWLANJoinAdapter::performJoin(...) @ 0xffffff8001576df8` consumes RSN IE pointer `+0xd6` and length `+0xd4`, candidate count `+0x218`, and first candidate fields `+0x220/+0x226/+0x22c`.
  - decomp: `AppleBCMWLANJoinAdapter::setAssocRSNIE(...) @ 0xffffff80015795b8` builds the command payload from pointer+length.
  - decomp: `AppleBCMWLANCore::setRSN_IE(...) @ 0xffffff800160433e` returns success immediately.
  - runtime boundary: previous evidence shows WCL join and RX EAPOL progress without final EAPOL TX/key/RSN completion; no final root-cause claim is made here.
- candidate causes:
  - confirmed: missing compiled association carrier constants/owner witness.
  - confirmed: local fixed-size RSN override copy is not equivalent to reference pointer+length semantics.
  - rejected: force EAPOL TX, synthesize keys, force RSN done, rewrite AKM/auth bits, or add retries/replays/delays.
- confirmed deviation: active local carrier/RSN handoff did not fully encode the recovered Apple hidden-assoc owner contract.
- root cause: confirmed for hidden association / RSN carrier layer completeness only. No final RSN/data runtime root cause is claimed.
- fix:
  - add `TahoeAssociationContracts.hpp`.
  - add `AssociationOwner` witnesses to `TahoeOwnerRegistry`.
  - replace hidden-assoc selectors, payload length, and WCL carrier offsets with recovered constants.
  - retain selected-BSSID semantics from candidate count `+0x218` and first BSSID `+0x220`.
  - zero local RSN override and copy only bounded caller-provided IE length in both Skywalk and legacy setters.
- verification:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-160.

## ANOMALY

- id: A-RSN-SUPPLICANT-VIRTUAL-OWNER-161-REJECTED
- status: REJECTED
- symptom: proposed CR-161 attempted to restore RSN supplicant ownership by
  adding `AirportItlwmV2::useAppleRSNSupplicant(IO80211VirtualInterface *)`.
- first visible manifestation: Tahoe build failed with
  `AirportItlwmV2.hpp:296: 'useAppleRSNSupplicant' marked 'override' but does
  not override any member functions`.
- expected system behavior: Tahoe code must only override virtual methods
  actually present in `IO80211ControllerV3`; remaining RSN/EAPOL work must be
  localized at a real IO80211Infra/Skywalk/RSN producer or consumer boundary.
- actual behavior: `IO80211ControllerV3` has no
  `useAppleRSNSupplicant(IO80211VirtualInterface *)` slot, while the recovered
  Apple method is `AppleBCMWLANCore::useAppleRSNSupplicant()` with no interface
  argument and a `featureFlagIsBitSet(0)` body.
- divergence point: the CR-161 candidate confused the older V2 controller seam
  with the Tahoe V3 controller ABI.
- evidence:
  - build: full `xcodebuild` log showed the non-overriding method error.
  - decomp: `/tmp/AppleBCMWLANCoreMac` exports
    `AppleBCMWLANCore::useAppleRSNSupplicant()` at `0xffffff80015905f4`.
  - source: `include/Airport/IO80211ControllerV3.h` enumerates Tahoe controller
    slots `[396]-[469]` without that method.
- rejected causes:
  - missing Tahoe V3 virtual supplicant overload: rejected.
  - leaving the method without `override`: rejected as dead local code with no
    proven system-facing call path.
- confirmed deviation: none for this proposed layer.
- root cause: rejected as a root-cause candidate for the current no-EAPOL-TX
  symptom.
- fix: remove the invalid CR-161 code/docs/YAML from the active batch and
  continue with the real EAPOL/RSN/key boundary.
- verification:
  - no `tahoeOwnerRegistry.supplicant` or CR-161 YAML/reference remains in the
    active tree.
  - Tahoe build must return to the CR-160-derived baseline.

## FIX_CANDIDATE

- anomaly_id: A-ASSOC-RSN-CARRIER-OWNER-160
- symptom: association reaches WCL join/RX-EAPOL boundary but RSN/key/data completion remains absent; hidden-assoc carrier and RSN IE handoff still had unrecovered owner details.
- expected system behavior: local owner layer records exact hidden-assoc selectors, payload size, carrier offsets, candidate list layout, and pointer+length RSN IE semantics.
- actual behavior: local code used hardcoded active offsets and copied a fixed RSN IE buffer length from partially populated stack carriers.
- exact divergence point: WCL association bridge/parser and local RSN IE override setters listed above.
- evidence from runtime: existing CR-113/CR-114 boundary proves this is the active post-join layer; no final runtime root cause is claimed.
- evidence from decomp: AppleBCMWLANCore/JoinAdapter addresses and offsets listed in A-ASSOC-RSN-CARRIER-OWNER-160.
- exact semantic mismatch between reference and our code: reference hands RSN IE as explicit pointer+length through JoinAdapter, while local compatibility storage copied the entire 257-byte backing array regardless of caller length.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: hidden selector gate, slot-470 hidden-assoc bridge, WCL association carrier parser, `setAUTH_TYPE(...)`, `setRSN_IE(...)`, net80211 RSN override, association request IE generation.
  - expected contract at each touchpoint: only exact `0x3ad8` hidden carrier routes to WCL associate; carrier fields are read from recovered offsets; selected BSSID comes from the candidate list; RSN override contains only the caller-provided IE bytes and a zeroed tail.
  - why no relevant touchpoints are missing: scope ends at association request RSN IE handoff before EAPOL/key/DHCP; those downstream producers are explicitly not modified.
  - why proposed path adds no extra system-visible side effects: no callback, retry, replay, delay, forced key, forced RSN state, forced link, or AKM rewrite is added.
- why this is root cause and not just correlation: for this layer, the decomp directly proves the exact carrier layout and RSN pointer+length contract; the fix restores that layer without claiming downstream RSN completion.
- why proposed fix is 1:1 with reference architecture and semantics: constants and owner witnesses mirror the recovered carrier; local bounded RSN copy is the compatibility equivalent of JoinAdapter pointer+length semantics.
- files/functions to modify:
  - `AirportItlwm/TahoeAssociationContracts.hpp`
  - `AirportItlwm/TahoeOwnerRegistry.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportSTAIOCTL.cpp`
  - docs/reference/YAML/protocol docs
- forbidden alternative fixes considered and rejected:
  - force EAPOL TX/key/RSN done.
  - fabricate PMK/PTK/GTK.
  - rewrite AKM/auth bits.
  - add sleep/retry/replay/poll.
  - suppress disconnect/deauth.
- verification plan:
  - YAML parse.
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-160.

## ANOMALY

- id: A-SKYWALK-PACKET-POOL-NETWORK-TYPE-161
- status: FIX_IMPLEMENTED
- symptom:
  - Visible networks remain available, but association stalls after RX EAPOL
    enqueue; no EAPOL TX, no cipher key install, and no RSN completion are
    observed.
- first visible manifestation:
  - Runtime evidence from CR-112/CR-113 shows `ITLWM_EAPOL path=rx
    stage=enqueue-ok`, followed by WCL/RSN failure and no key install.
- expected system behavior:
  - Apple creates Wi-Fi Skywalk packet pools as network packet pools
    (`kIOSkywalkPacketTypeNetwork`, value `1`).
  - The downstream IO80211 RX/TX path consumes
    `IO80211NetworkPacket*` / `IOSkywalkNetworkPacket` objects.
- actual behavior:
  - Local `AirportItlwm::start` created both TX and RX pools with packet type
    `0`, i.e. generic Skywalk packet pools.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp` STEP 8b pool creation.
- evidence:
  - panic logs:
    - none for this anomaly.
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-112-afterfix-kernel-focused-20260426-1935.log`
    - `commit-approval/runtime_evidence/CR-113-focused-live-log-20260426-194658.log`
    - RX EAPOL enqueue succeeds, but repository runtime evidence contains no
      `ITLWM_IO80211_INPUT` marker for those failing association runs.
  - ioreg:
    - not required for this packet-pool class divergence.
  - packet traces:
    - no EAPOL TX/key-install progression after RX EAPOL enqueue.
  - firmware traces:
    - AP deauth/RSN failure remains after RX EAPOL enqueue; no local TX/key
      proof.
  - decomp:
    - `AppleBCMWLANSkywalkPacketPool::initWithName(...) @
      0xffffff80016e033c` copies pool options and calls parent init with
      `movl $0x1, %ecx`.
    - BootKC exports `IOSkywalkNetworkPacket::withPool(...)`.
    - BootKC/AppleBCMWLAN symbols show IO80211 consumers taking
      `IO80211NetworkPacket*`, including `IO80211InfraInterface::inputPacket`
      and `IO80211PeerManager::skywalkInputPacket`.
  - docs:
    - `docs/reference/AppleBCMWLAN_skywalk_packet_pool_network_type_2026_04_27.md`
    - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/103_skywalk_packet_pool_network_type_2026_04_27.yaml`
- candidate causes:
  - confirmed: generic local packet pool prevents RX completion from
    satisfying the IO80211 network-packet input contract.
- rejected causes:
  - Manual `inputPacket(...)` call is rejected: reference requires a real
    network-packet object reaching the boundary, not a forced callback.
  - Forced EAPOL TX, forced RSN done, forced key install, retry, delay, and
    replay are rejected: none are reference producer-side fixes for this
    packet-class mismatch.
  - A guessed custom `IO80211NetworkPacket` subclass is rejected for this batch
    because the complete Tahoe base layout/vtable and ownership lifecycle are
    not yet locally recovered.
- confirmed deviation:
  - Reference parent pool init receives packet type `1`; local code used `0`.
- root cause:
  - For the currently confirmed missing boundary, local RX enqueue used generic
    packet-pool objects while the reference IO80211 datapath is network-packet
    typed. This is the first confirmed semantic mismatch between local RX
    enqueue success and absent IO80211 input.
- fix:
  - `AirportItlwm/AirportItlwmV2.cpp` now creates both TX and RX pools with
    `kIOSkywalkPacketTypeNetwork`.
  - `AirportItlwm/AirportItlwmV2.hpp` now includes
    `<IOKit/skywalk/IOSkywalkPacket.h>` for the named packet-type constant.
- verification:
  - structural build pending after current batch.
  - after-fix runtime must confirm whether `ITLWM_IO80211_INPUT` appears after
    RX enqueue and whether EAPOL TX/key/RSN progression advances.
- notes:
  - This fix does not implement the deeper Apple PCIe custom packet subclass.
    That remains a separate layer only if runtime still proves a packet-class
    mismatch after network packet pools are restored.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-PACKET-POOL-NETWORK-TYPE-161
- symptom: association reaches RX EAPOL enqueue, but IO80211 input, EAPOL TX,
  key install, and RSN completion remain absent.
- expected system behavior: Apple creates Wi-Fi Skywalk TX/RX pools with
  `kIOSkywalkPacketTypeNetwork` and downstream IO80211 consumes network-packet
  objects.
- actual behavior: local TX/RX pools used packet type `0`
  (`kIOSkywalkPacketTypeGeneric`).
- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp` STEP 8b pool
  creation.
- evidence from runtime: CR-112/CR-113 show RX EAPOL enqueue success with no
  `ITLWM_IO80211_INPUT` marker and no EAPOL TX/key/RSN completion.
- evidence from decomp: `AppleBCMWLANSkywalkPacketPool::initWithName(...)`
  passes `1` to parent `IOSkywalkPacketBufferPool::initWithName`; BootKC and
  AppleBCMWLAN IO80211 consumers are typed as `IO80211NetworkPacket*`.
- exact semantic mismatch between reference and our code: reference packet
  pools produce network packet objects for this datapath; local code requested
  generic packet objects.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the first missing boundary
  after confirmed local RX enqueue is IO80211 network input, and the reference
  object contract at that boundary is network-packet typed.
- why proposed fix is 1:1 with reference architecture and semantics: the same
  local pool factory calls are retained, but their packet-type argument is
  changed to the reference value `kIOSkywalkPacketTypeNetwork`.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-23.md`
  - `docs/reference/AppleBCMWLAN_skywalk_packet_pool_network_type_2026_04_27.md`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/85_bsd_attach_chain_xref_checked.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/103_skywalk_packet_pool_network_type_2026_04_27.yaml`
  - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/MANIFEST_V11.txt`
- forbidden alternative fixes considered and rejected:
  - manual `inputPacket(...)` callback.
  - forced EAPOL TX, key install, or RSN success.
  - retry, delay, replay, or state masking.
  - guessed custom `IO80211NetworkPacket` subclass without full ABI recovery.
- verification plan:
  - `git diff --check`.
  - `bash -n scripts/build_tahoe.sh`.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - Stage 1 request for structural review.
  - after approval, runtime evidence must check for `ITLWM_IO80211_INPUT`,
    EAPOL TX, key install, and RSN progression.

## ANOMALY

- id: A-SKYWALK-NETWORK-PACKET-TAG-ABI-162
- status: FIX_IMPLEMENTED
- symptom:
  - RX EAPOL reaches the local enqueue boundary, but IO80211 input, EAPOL TX,
    key install, and RSN completion remain absent.
  - A direct producer-side input restoration is blocked until the local packet
    and tag ABI matches the reference consumers.
- first visible manifestation:
  - CR-112/CR-113 runtime evidence shows RX EAPOL enqueue success and no
    `ITLWM_IO80211_INPUT` marker.
- expected system behavior:
  - `IOSkywalkNetworkPacket` is an `IOSkywalkPacket` subclass.
  - RX completion passes `IO80211NetworkPacket*`, a valid packet scratch/tag,
    and an ethernet header to the IO80211 interface input path.
- actual behavior:
  - local `IOSkywalkNetworkPacket` inherited from `IOService` and declared
    generic packet methods in the wrong class.
  - local `packet_info_tag` was empty.
- divergence point:
  - `include/Airport/IOSkywalkNetworkPacket.h`
  - `include/Airport/apple_private_spi.h`
- evidence:
  - panic logs:
    - none for this structural ABI anomaly.
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-112-afterfix-kernel-focused-20260426-1935.log`
    - `commit-approval/runtime_evidence/CR-113-focused-live-log-20260426-194658.log`
    - RX EAPOL enqueue succeeds, but no IO80211 input marker follows.
  - ioreg:
    - not required for this ABI declaration mismatch.
  - packet traces:
    - no EAPOL TX/key-install progression after RX EAPOL enqueue.
  - firmware traces:
    - not required for this declaration mismatch.
  - decomp:
    - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
      0xffffff80014ca8e4` passes packet scratch/tag and ethernet header into
      the interface input slot for normal roles.
    - The RX producer reads scratch `+0x18` and writes mapped service class to
      scratch `+0x29`.
    - `IO80211InterfaceMonitor::logRxCompletionPacket(...) @
      0xffffff80022f633e` reads tag `+0x18` and tag `+0x14`.
    - `AppleBCMWLANPCIeSkywalkPacket::free()` frees scratch at packet `+0x78`
      with size `0x98`; `prepare()` clears the first `0x30` bytes.
  - docs:
    - `docs/reference/AppleBCMWLAN_network_packet_tag_abi_2026_04_27.md`
    - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/104_network_packet_tag_abi_2026_04_27.yaml`
- candidate causes:
  - confirmed: local network-packet/tag ABI declarations do not satisfy
    proven IO80211 RX input consumers.
- rejected causes:
  - Passing a null or empty tag into `inputPacket(...)` is rejected because the
    monitor dereferences fixed tag offsets.
  - A guessed stack tag is rejected because it would not be tied to packet
    scratch ownership.
  - Forced EAPOL TX/key/RSN, retry, delay, replay, and poll are rejected.
- confirmed deviation:
  - Reference `IOSkywalkNetworkPacket` is an `IOSkywalkPacket` subclass.
  - Reference consumers dereference packet tag/scratch offsets absent from the
    local empty struct.
- root cause:
  - For the ABI-restoration blocker, the local declarations cannot represent
    the reference RX input tuple safely. This is a deterministic contract
    violation at the exact boundary that must be restored before direct input
    delivery is safe.
- fix:
  - `IOSkywalkNetworkPacket` declaration now matches the Tahoe Skywalk header
    shape.
  - `packet_info_tag` now records proven offsets `+0x14`, `+0x18`, `+0x29`
    and size `0x98`, with compile-time assertions.
- verification:
  - structural build pending after current batch.
  - after the later RX handoff fix, runtime must verify IO80211 input,
    EAPOL TX, key install, and RSN progression.
- notes:
  - Field names in `packet_info_tag` are local documentation names. Offsets,
    size, and consumer use are the decompile-proven facts.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-NETWORK-PACKET-TAG-ABI-162
- symptom: RX EAPOL reaches local enqueue, but IO80211 input and downstream
  RSN/key/data progression remain absent; a direct handoff fix is unsafe until
  packet/tag ABI is restored.
- expected system behavior: `IOSkywalkNetworkPacket` inherits
  `IOSkywalkPacket`; RX completion supplies a valid `packet_info_tag` with
  proven offsets used by IO80211 consumers.
- actual behavior: local network-packet class and tag struct did not represent
  those contracts.
- exact divergence point: local network-packet declaration and empty
  `packet_info_tag`.
- evidence from runtime: CR-112/CR-113 show RX EAPOL enqueue success without
  `ITLWM_IO80211_INPUT`.
- evidence from decomp: Apple RX completion producer and IO80211 monitor
  dereference the exact offsets listed in the anomaly.
- exact semantic mismatch between reference and our code: local declarations
  described a different class hierarchy and zero-sized tag storage for a path
  that consumes fixed packet scratch/tag fields.
- fix justification path: REFERENCE_ALIGNMENT_FIX plus SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: RX completion producer,
    `IO80211InfraInterface::inputPacket(...)`, IO80211 monitor,
    `IO80211PeerManager::skywalkInputPacket(...)`.
  - expected contract at each touchpoint: network packet object plus non-empty
    scratch/tag layout for offsets `+0x14`, `+0x18`, and `+0x29`.
  - why no relevant touchpoints are missing: this claim is limited to ABI
    declarations and does not claim the later packet delivery edge.
  - why proposed path adds no extra system-visible side effects: no runtime
    callbacks or state changes are added.
- why this is root cause and not just correlation: the referenced consumers
  deterministically read fields absent from the local ABI.
- why proposed fix is 1:1 with reference architecture and semantics: class
  declaration follows the Tahoe Skywalk header; tag layout includes only
  decompile-proven offsets and size.
- files/functions to modify:
  - `include/Airport/IOSkywalkNetworkPacket.h`
  - `include/Airport/apple_private_spi.h`
  - docs/reference/YAML/protocol docs
- forbidden alternative fixes considered and rejected:
  - direct input with null/empty tag.
  - guessed stack tag.
  - forced EAPOL/key/RSN.
  - retry, delay, replay, poll, or masking.
- verification plan:
  - `git diff --check`.
  - YAML parse for the new file.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - Stage 1 structural request CR-162.

## ANOMALY

- id: A-SKYWALK-RX-COMPLETION-INPUT-HANDOFF-163
- status: FIX_IMPLEMENTED
- symptom:
  - RX EAPOL reaches local enqueue, but IO80211 input, EAPOL TX, key install,
    RSN completion, and stable data path remain absent.
- first visible manifestation:
  - CR-112/CR-113 runtime logs show `ITLWM_EAPOL path=rx stage=enqueue-ok`
    with no `ITLWM_IO80211_INPUT`.
- expected system behavior:
  - RX completion action performs producer-side handoff to the IO80211 input
    slot with packet, tag/scratch, ethernet header, null accepted pointer, and
    final bool `false`.
- actual behavior:
  - local `skywalkRxAction(...)` incremented `rxCbCnt` and returned `count`
    without delivering packets to IO80211 input.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction`
- evidence:
  - panic logs:
    - none for this anomaly.
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-112-afterfix-kernel-focused-20260426-1935.log`
    - `commit-approval/runtime_evidence/CR-113-focused-live-log-20260426-194658.log`
    - RX EAPOL enqueue succeeds, but no IO80211 input marker follows.
  - ioreg:
    - not required for this completion-action divergence.
  - packet traces:
    - no EAPOL TX/key-install progression after RX EAPOL enqueue.
  - firmware traces:
    - AP deauth/RSN failure remains after RX enqueue without downstream local
      input proof.
  - decomp:
    - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
      0xffffff80014ca8e4` calls the interface input slot from the RX completion
      callback.
    - The call passes packet scratch/tag, `dataVirtualAddress + dataOffset`,
      null accepted pointer, and `false`.
    - `IO80211InfraInterface::inputPacket(...) @ 0xffffff80022e3f20`
      synchronously calls monitor and peer-manager consumers.
  - docs:
    - `docs/reference/AppleBCMWLAN_rx_completion_input_handoff_2026_04_27.md`
    - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/105_rx_completion_input_handoff_2026_04_27.yaml`
- candidate causes:
  - confirmed: local RX completion action acknowledged packets without the
    reference IO80211 input handoff.
- rejected causes:
  - Calling input from `skywalkRxInput(...)` is rejected because the reference
    edge is the completion callback, not the earlier mbuf-copy producer.
  - Passing a forced accepted pointer/success is rejected because reference
    passes null accepted pointer.
  - Packet replay, retry, delay, forced key/RSN, and disconnect suppression are
    rejected.
- confirmed deviation:
  - Reference RX completion callback calls input; local callback returned
    without input.
- root cause:
  - The only runtime edge reached before the stall is RX enqueue, and the
    corresponding reference completion action is the missing IO80211 input
    producer. Local omission directly explains absent `ITLWM_IO80211_INPUT`.
- fix:
  - `skywalkRxAction(...)` now validates packet data, derives ethernet header,
    initializes recovered `packet_info_tag`, and calls
    `AirportItlwmSkywalkInterface::inputPacket(...)` with null accepted pointer
    and `false`.
- verification:
  - structural build passed locally before request preparation.
  - after-fix runtime must verify whether `ITLWM_IO80211_INPUT`, EAPOL TX, key
    install, and RSN completion progress.
- notes:
  - This does not synthesize a downstream result. The next runtime run must
    show whether IO80211 input accepts the packet and whether RSN advances.

## FIX_CANDIDATE

- anomaly_id: A-SKYWALK-RX-COMPLETION-INPUT-HANDOFF-163
- symptom: RX EAPOL enqueue succeeds, but IO80211 input and downstream
  EAPOL/key/RSN progression are absent.
- expected system behavior: RX completion action calls interface input from the
  completion boundary.
- actual behavior: local completion action only returned `count`.
- exact divergence point: `skywalkRxAction(...)`.
- evidence from runtime: CR-112/CR-113 show enqueue-ok without input marker.
- evidence from decomp: Apple PCIe RX completion callback calls interface
  input with packet/tag/header/null-accepted/false.
- exact semantic mismatch between reference and our code: local completion
  callback omitted the producer-side handoff.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: RX completion action, IO80211 input,
    monitor, peer manager.
  - expected contract at each touchpoint: valid packet data/header and tag
    tuple; null accepted pointer; no forced downstream state.
  - why no relevant touchpoints are missing: scope is bounded to the missing
    post-enqueue input edge.
  - why proposed path adds no extra system-visible side effects: no replay,
    retry, duplicate enqueue, forced success, or key/RSN fabrication.
- why this is root cause and not just correlation: decomp proves the exact
  missing producer-side call at the runtime-stalled edge.
- why proposed fix is 1:1 with reference architecture and semantics: input is
  called from RX completion action, not the earlier producer, with reference
  null accepted/final false semantics.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction`
  - docs/reference/YAML/protocol docs
- forbidden alternative fixes considered and rejected:
  - direct input from `skywalkRxInput(...)`.
  - packet replay/duplication.
  - forced accepted success.
  - forced EAPOL/key/RSN/link/DHCP success.
  - retry, delay, poll, or masking.
- verification plan:
  - `git diff --check`.
  - YAML parse for 105.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - Stage 1 structural request CR-163.

## ANOMALY

- id: A-CR165-RX-COMPLETION-PRODUCER-STAGING
- status: FIX_IMPLEMENTED
- symptom:
  - CONTROL_STA_NETWORK association reaches a brief connected UI state, but there is no
    internet/data path and the interface later leaves the network.
- first visible manifestation:
  - CR-164 after-fix runtime shows repeated RX EAPOL enqueue success without
    `skywalkRxAction(...)` or `ITLWM_IO80211_INPUT`.
- expected system behavior:
  - The RX completion owner maintains pending RX packets and rings
    `IOSkywalkRxCompletionQueue::requestEnqueue(...)`.
  - The registered producer action drains pending packets, performs the
    IO80211 input handoff, fills the Skywalk-provided output array, and returns
    produced count.
- actual behavior:
  - Local `skywalkRxInput(...)` called base
    `IOSkywalkRxCompletionQueue::enqueuePackets(...)` directly.
  - Base enqueue returned success but did not invoke the registered action.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction`
- evidence:
  - runtime logs:
    - `commit-approval/runtime_evidence/CR-164-afterfix-control_sta_network-focused-20260427-173915.log`
    - `ITLWM_EAPOL path=rx stage=enqueue-ok` appears repeatedly.
    - `skywalkRxAction` is absent.
    - `ITLWM_IO80211_INPUT` is absent.
    - WCL reports `assocTimerAction@1177:Update Bss fail`.
    - WCL leaves due to `<Update Bss fail>`.
  - decomp:
    - `IOSkywalkRxCompletionQueue::requestEnqueue(...) @ 0xffffff8002a59c4c`
      calls the registered action and uses its return as produced count.
    - `IOSkywalkRxCompletionQueue::enqueuePackets(...) @ 0xffffff8002a59cda /
      0xffffff8002a59d84` publishes packets to the base networking path and
      does not call the registered action.
    - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
      0xffffff80014ca8e4` drains an owner-side pending list, calls the
      interface input slot, writes produced packets into the output array, and
      returns produced count.
- candidate causes:
  - confirmed: missing local pending RX producer queue and wrong use of base
    `enqueuePackets` as the action boundary.
- rejected causes:
  - direct duplicate `inputPacket(...)` after base enqueue is rejected because
    the reference handoff happens before base publish from the producer action.
  - forced RSN/key/link/DHCP success and deauth masking are rejected.
  - retry/poll/delay loops are rejected.
- confirmed deviation:
  - Reference owner action is reached through `requestEnqueue` and produces the
    batch; local code bypassed that action by calling base `enqueuePackets`.
- root cause:
  - This exactly explains CR-164 runtime: RX packets are prepared and accepted
    by base enqueue, but the only code that calls IO80211 input is never
    entered.
- fix:
  - Added fixed-capacity local pending RX packet ring.
  - `skywalkRxInput(...)` stages prepared packets and calls
    `fRxQueue->requestEnqueue(nullptr, 0)`.
  - `skywalkRxAction(...)` pops pending packets, calls IO80211 input, fills the
    Skywalk-provided packet array, and returns produced count.
  - Pending prepared packets are drained during stop/free before RX pool
    release.
- verification:
  - `git diff --check`: PASS.
  - `./scripts/build_tahoe.sh`: PASS, BootKC symbol check PASS.
  - `./scripts/build_regdiag.sh`: PASS.
  - after-fix runtime must verify `producer-input` and
    `ITLWM_IO80211_INPUT`.
- notes:
  - This is not a packet replay. The packet is produced once from the local
    pending queue, matching the Apple action architecture.

## FIX_CANDIDATE

- anomaly_id: A-CR165-RX-COMPLETION-PRODUCER-STAGING
- symptom: RX EAPOL reaches local RX path, but IO80211 input remains absent and
  WCL later leaves the network after BSS update failure.
- expected system behavior: pending RX owner plus `requestEnqueue` producer
  action performs the IO80211 input handoff and returns produced packets.
- actual behavior: local code directly called base `enqueuePackets`, bypassing
  the registered action.
- exact divergence point: `skywalkRxInput(...)` direct base enqueue and
  `skywalkRxAction(...)` never reached.
- evidence from runtime: CR-164 after-fix CONTROL_STA_NETWORK log has repeated
  `ITLWM_EAPOL stage=enqueue-ok`, no `skywalkRxAction`, no
  `ITLWM_IO80211_INPUT`, then WCL Update Bss fail / leave.
- evidence from decomp: IOSkywalk `requestEnqueue` calls the action;
  IOSkywalk base `enqueuePackets` does not; AppleBCMWLAN action drains pending
  RX list, calls IO80211 input, fills output array, returns produced count.
- exact semantic mismatch between reference and our code: missing owner-side
  pending RX producer and wrong boundary for input handoff.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: it explains both sides of
  the runtime contradiction: packets were enqueued successfully but the only
  local input hook was never called.
- why proposed fix is 1:1 with reference architecture and semantics: local
  producer action now drains pending RX packets, calls input, fills the
  provided array, and returns produced count, matching the recovered Apple
  action role.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxInput`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction`
  - RX completion docs/YAML/manifests
- forbidden alternative fixes considered and rejected:
  - forced accepted success.
  - forced EAPOL/key/RSN/DHCP/link success.
  - direct duplicate input after base enqueue.
  - retry/poll/delay loops.
  - deauth masking.
- verification plan:
  - `git diff --check`.
  - YAML parse for 105/106.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - Stage 1 structural request CR-165.

## ANOMALY

- id: A-CR166-TX-COMPLETION-PRODUCER-OWNERSHIP
- status: FIX_IMPLEMENTED
- symptom: after association the UI reaches partial connected/no-internet state
  and later disconnects; once CR-165 restores RX producer input, TX submission
  is expected to start and every consumed packet must be completed.
- first visible manifestation: local static path shows `skywalkTxAction(...)`
  returns consumed packets without any completion producer; current CR-164
  runtime still does not reach TX because RX producer action is bypassed.
- expected system behavior:
  - consumed TX packets are returned through a TX completion producer.
  - produced TX completion packets reach IOSkywalk completion accounting and
    `completeWithQueue(queue, kIOSkywalkPacketDirectionTx, 0)`.
- actual behavior:
  - `skywalkTxAction(...)` copies bytes into an mbuf and returns consumed count.
  - `skywalkTxCompletionAction(...)` returns `0`.
  - no local pending TX completion producer exists.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxCompletionAction`
- evidence:
  - decomp:
    - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::stagePacket(...) @
      0xffffff80014c91c0` links packet scratch/list nodes into the completion
      queue owner list.
    - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::requestEnqueue(...) @
      0xffffff80014c920c` delegates to the IOSkywalk producer boundary.
    - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::enqueuePackets(...) @
      0xffffff80014c8d62` drains staged packets into the output array and
      returns produced count.
    - IOSkywalk TX completion enqueue path near `0xffffff8002a3fa9e` calls
      packet `completeWithQueue(queue, kIOSkywalkPacketDirectionTx, 0)`.
  - runtime:
    - current CR-164 focused CONTROL_STA_NETWORK runtime does not reach TX logs because
      RX producer/input is still absent.
    - this candidate is therefore an adjacent confirmed ownership divergence,
      not an independently proven pre-CR165 current root cause.
- confirmed deviation:
  - reference has a TX completion producer and ownership-return path.
  - local consumed packets had no completion producer.
- root cause:
  - confirmed for downstream TX queue ownership leak/stall once TX submission
    starts; not claimed as the CR-164 pre-RX-producer root cause.
- fix:
  - add fixed-capacity TX completion pending ring.
  - stage every non-null packet consumed by `skywalkTxAction(...)`.
  - ring `fTxCompQueue->requestEnqueue(nullptr, 0)` after a TX batch.
  - make `skywalkTxCompletionAction(...)` pop staged packets into the provided
    array and return produced count.
  - drain pending TX completion packets before queue/pool release.
- verification:
  - `git diff --check`.
  - YAML parse for 105/106/107.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - Stage 1 structural request CR-166.

## FIX_CANDIDATE

- anomaly_id: A-CR166-TX-COMPLETION-PRODUCER-OWNERSHIP
- symptom: consumed TX Skywalk packets have no completion producer, which would
  stall/leak TX ownership once TX submission begins after RX producer restore.
- expected system behavior: consumed TX packets are staged, produced through TX
  completion queue `requestEnqueue`, and completed by IOSkywalk.
- actual behavior: local TX action returned consumed count while the TX
  completion action returned zero.
- exact divergence point: local `skywalkTxAction(...)` and
  `skywalkTxCompletionAction(...)` versus Apple TX completion
  stage/request/produce path.
- evidence from runtime: CR-164 runtime still cannot reach this path because
  RX producer is bypassed; no independent current-root claim is made.
- evidence from decomp: Apple `stagePacket`, `requestEnqueue`,
  `enqueuePackets`, and IOSkywalk completion path prove the ownership-return
  contract.
- exact semantic mismatch between reference and our code: missing TX
  completion producer and missing completion ownership return for consumed
  packets.
- fix justification path: SYSTEM_CONTRACT_FIX
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints: TX submission consumed count, mbuf
    output handoff, TX completion pending producer, requestEnqueue, completion
    action packet array, completeWithQueue, teardown drain.
  - expected contract at each touchpoint: every non-null consumed packet is
    completed exactly once, without forcing packet delivery success or link
    state.
  - why no relevant touchpoints are missing: scope is limited to ownership
    closure for packets already consumed by the local TX action.
  - why proposed path adds no extra system-visible side effects: no retry,
    replay, poll, forced success, synthetic packet, key/link/RSN/DHCP state, or
    deauth masking.
- why this is root cause and not just correlation: confirmed root for TX
  ownership leak/stall after TX begins; not claimed as the current CR-164 root.
- why proposed fix is 1:1 with reference architecture and semantics: it
  restores stage + requestEnqueue + producer-array completion architecture.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxCompletionAction`
  - docs/reference/YAML/inventory/signal-chain analysis
- forbidden alternative fixes considered and rejected:
  - direct packet deallocation without completion.
  - base enqueue shortcut without producer evidence.
  - retry/poll/delay.
  - forced TX/EAPOL/key/RSN/link/DHCP success.
- verification plan:
  - `git diff --check`.
  - YAML parse for 105/106/107.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-166 Stage 1 structural batch request.

## Current Active Batch Note

- id: A-CR167-RX-PRODUCER-TAG-STATS-CLOSURE
- status: implemented locally, pending Stage 1 structural review as CR-167.
- scope: closes the RX producer tag carrier and post-batch accounting edge
  found during the active Skywalk RX producer audit.
- detailed evidence: see the full `A-CR167-RX-PRODUCER-TAG-STATS-CLOSURE`
  entry above and
  `docs/reference/AppleBCMWLAN_rx_producer_tag_stats_2026_04_27.md`.
- verification:
  - `git diff --check` passed.
  - selected YAML parse for 105/106/107/108 passed; full bundle parse is
    blocked by the pre-existing `81_event_payload_schemas.yaml` syntax issue.
  - `./scripts/build_tahoe.sh` passed.
  - `./scripts/build_regdiag.sh` passed.

- id: A-CR168-TX-QUEUE-SPACE-PENDING-CLOSURE
- status: implemented locally, pending Stage 1 structural review as CR-168.
- scope: closes the TX queue admission visibility mismatch adjacent to the
  active RX/TX producer restoration.
- evidence:
  - Apple primary `getTxSubQueue(...) @ 0xffffff800155fb5a` and APSTA
    `getTxSubQueue(...) @ 0xffffff80016940b4` return queue objects through
    owner-vector lookup.
  - Apple `getTxQueueDepth()` reads live queue capacity.
  - IO80211SkywalkInterface `pendingPackets(...) @ 0xffffff80022780ac` and
    `packetSpace(...) @ 0xffffff8002278134` query queue object virtuals.
- local divergence:
  - local `getTxSubQueue(...)` exposed `fTxQueue`.
  - local `pendingPackets(...)` and `packetSpace(...)` returned unconditional
    zero.
- fix:
  - make local single-TX-queue lookup return `fTxQueue` consistently.
  - return `fTxQueue->getPacketCount()` from `pendingPackets(...)`.
  - return `fTxQueue->getFreeSpace()` from `packetSpace(...)`.
- non-claims:
  - no forced TX success, EAPOL/key/RSN/DHCP/link success, retry, replay,
    delay, poll loop, synthetic packet, or deauth masking.

## ANOMALY

- id: A-CR169-TX-OUTPUT-ACCOUNTING-CLOSURE
- status: FIX_IMPLEMENTED
- symptom: after CONTROL_STA_NETWORK association the UI reaches partial
  connected/no-internet state and later disconnects; active TX submission has
  a confirmed missing IO80211 output-accounting edge.
- first visible manifestation: local static TX path shows
  `skywalkTxAction(...)` returning consumed packets and updating
  `sRT.txPktSent` without calling IO80211 output accounting.
- expected system behavior:
  - TX submission dequeue accumulates accepted packet and byte totals.
  - the interface output-accounting edge is called once for the batch.
- actual behavior:
  - local `skywalkTxAction(...)` called `outputPacket(...)` for copied mbufs.
  - accepted packet count was recorded only in local `sRT.txPktSent`.
  - no `recordOutputPacket(...)` call followed the TX batch.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
- evidence:
  - decomp:
    - `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(...) @
      0xffffff80014c611c` accumulates packet and byte totals.
    - tail call site near `0xffffff80014c7944` invokes the interface virtual
      slot matching `recordOutputPacket(...)`.
    - `IO80211SkywalkInterface::recordOutputPacket(apple80211_wme_ac,int,int)
      @ 0xffffff8002277cc6` delegates to the interface monitor when present.
  - runtime:
    - current after-fix runs still fail after partial association, so this is
      an active-layer confirmed divergence being removed before the next
      reboot cycle; it is not claimed as the sole remaining connection root.
- confirmed deviation:
  - reference records TX output accounting after dequeue.
  - local code skipped that IO80211 accounting edge.
- root cause:
  - confirmed for missing IO80211 TX output monitor/accounting state after
    local accepted TX frames; not claimed as the only remaining CONTROL_STA_NETWORK
    connection root cause.
- fix:
  - accumulate delivered packet bytes for frames where `outputPacket(...)`
    returns `kIOReturnOutputSuccess`.
  - call `recordOutputPacket({ APPLE80211_WME_AC_BE }, delivered,
    deliveredBytes)` after the TX batch.
- verification:
  - `git diff --check`.
  - targeted YAML parse for 105/106/107/108/109/110.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-169 Stage 1 structural batch request.

## FIX_CANDIDATE

- anomaly_id: A-CR169-TX-OUTPUT-ACCOUNTING-CLOSURE
- symptom: accepted TX packets are not reflected through IO80211 output
  accounting.
- expected system behavior: Apple TX submission dequeue calls
  `recordOutputPacket(ac, packetCount, byteCount)` after the batch.
- actual behavior: local `skywalkTxAction(...)` updates only local counters
  after accepted `outputPacket(...)` frames.
- exact divergence point: local `skywalkTxAction(...)` lacks the Apple tail
  output-accounting call.
- evidence from runtime: association reaches a partial connected state then
  drops; current work is removing confirmed active-layer divergences before
  the next reboot cycle.
- evidence from decomp: Apple TX submission dequeue call site near
  `0xffffff80014c7944`; IO80211 output-accounting implementation at
  `0xffffff8002277cc6`.
- exact semantic mismatch between reference and our code: missing post-batch
  IO80211 TX output accounting.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: it is the direct cause of
  missing IO80211 output monitor/accounting state after accepted local TX
  frames; no broader connection-success claim is made.
- why proposed fix is 1:1 with reference architecture and semantics: use the
  existing IO80211 accounting method once after the TX batch with packet and
  byte totals; no synthetic packet, scratch, retry, replay, or forced state.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkTxAction`
  - TX output-accounting docs/YAML/manifests
- forbidden alternative fixes considered and rejected:
  - calling `logTxPacket(...)` / `logSkywalkTxReqPacket(...)` with synthetic
    scratch.
  - forced EAPOL/TX/key/RSN/DHCP/link/data success.
  - retry, replay, delay, poll loop, or deauth masking.
- verification plan:
  - `git diff --check`.
  - targeted YAML parse.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-169 Stage 1 structural request.

## Current Active Batch Note

- id: A-CR169-TX-OUTPUT-ACCOUNTING-CLOSURE
- status: implemented locally, pending Stage 1 structural review as CR-169.
- scope: closes the confirmed TX output-accounting mismatch adjacent to the
  active TX submission/completion restoration.
- evidence:
  - Apple TX submission dequeue accumulates packet and byte totals and calls
    `recordOutputPacket(...)` after the batch.
  - IO80211 output accounting is monitor/accounting only, not forced datapath
    success.
- local divergence:
  - local `skywalkTxAction(...)` updated only `sRT.txPktSent`.
- fix:
  - record delivered packet and byte totals after accepted `outputPacket(...)`
    frames through `fNetIf->recordOutputPacket(...)`.
- non-claims:
  - no forced TX/EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
    poll loop, synthetic scratch, synthetic packet, or deauth masking.

## ANOMALY

- id: A-CR170-IO80211-NETWORK-PACKET-POOL-CLASS
- status: FIX_IMPLEMENTED
- symptom: CONTROL_STA_NETWORK association reaches IO80211 RX input with EAPOL frames, but
  EAPOL TX, `setCIPHER_KEY`, and `IO80211RSNDone` remain absent before AP
  deauth reason 15.
- first visible manifestation:
  - 2026-04-27 regdiag runtime after CR-169-class driver: `eapol_rx=8`,
    `eapol_tx=0`, `IO80211RSNDone=No`, no cipher key install.
- expected system behavior:
  - AppleBCMWLAN packet pools allocate packet objects in the
    `IO80211NetworkPacket` family.
  - IO80211 input receives a real `IO80211NetworkPacket*`.
  - `IO80211NetworkPacket::getPacketType(...)` classifies EtherType `0x888e`
    as EAPOL packet type `2`.
- actual behavior:
  - local pools were base `IOSkywalkPacketBufferPool` objects with
    `kIOSkywalkPacketTypeNetwork`.
  - local RX handoff passed the allocated packet through
    `reinterpret_cast<IO80211NetworkPacket *>(pkt)`.
  - runtime shows IO80211 input success for RX EAPOL, but no downstream
    EAPOL/key/RSN progression.
- divergence point:
  - `AirportItlwm/AirportItlwmV2.cpp::start(...)` TX/RX pool creation.
  - `AirportItlwm/AirportItlwmV2.cpp::skywalkRxAction(...)` input handoff.
- evidence:
  - runtime logs:
    - 2026-04-27 CONTROL_STA_NETWORK window: RX EAPOL reaches producer input and returns
      success; no TX EAPOL/key/RSN progression; AP deauth reason 15.
  - ioreg:
    - `IO80211SSID=CONTROL_STA_NETWORK`
    - `IO80211RSNDone=No`
  - decomp:
    - `AppleBCMWLANPCIeSkywalkPacketPool::newPacketWithDescriptor(...) @
      0xffffff80014cb250` calls
      `AppleBCMWLANPCIeSkywalkPacket::withPool(...)`.
    - `AppleBCMWLANPCIeSkywalkPacketPool::allocatePacket(...) @
      0xffffff80014cb8ae` validates the returned packet against the Apple
      packet metaclass.
    - `IO80211NetworkPacket::getPacketType(...) @ 0xffffff80022cf000`
      returns `2` for EtherType `0x888e`.
    - IO80211 consumers are typed as `IO80211NetworkPacket*`.
  - docs:
    - `docs/reference/AppleBCMWLAN_io80211_network_packet_pool_class_2026_04_27.md`
    - `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/111_io80211_network_packet_pool_class_2026_04_27.yaml`
- candidate causes:
  - confirmed: local pool allocation returned the wrong packet class for the
    IO80211 input contract.
- rejected causes:
  - forced EAPOL TX/key/RSN/DHCP/link success.
  - retry, delay, replay, poll loop, duplicate notification, or deauth masking.
  - raw writes to Apple PCIe packet scratch at `packet+0x78`.
  - scratch-dependent TX log methods without an Apple packet scratch object.
- confirmed deviation:
  - reference allocates packet objects in the IO80211 packet class family.
  - local code allocated base Skywalk network packets and cast them at handoff.
- root cause:
  - confirmed for the active EAPOL/RSN handoff mismatch: IO80211 input receives
    a packet pointer whose allocation class is not reference-equivalent, so the
    downstream packet-type/supplicant path is not proven satisfied even though
    the input call returns success.
- fix:
  - add a local `IO80211NetworkPacket` declaration inheriting
    `IOSkywalkNetworkPacket`.
  - add `AirportItlwmIO80211PacketPool`, an `IOSkywalkPacketBufferPool`
    subclass.
  - override `newPacket(...)` to allocate the system
    `IO80211NetworkPacket` metaclass and call inherited `initWithPool(...)`.
  - use this pool for both Tahoe TX and RX packet pools.
- verification:
  - `git diff --check`: pass.
  - `./scripts/build_tahoe.sh`: pass, BootKC symbol verification pass with 884
    resolved undefined symbols.
  - `./scripts/build_regdiag.sh`: pending in current CR-170 batch.

## FIX_CANDIDATE

- anomaly_id: A-CR170-IO80211-NETWORK-PACKET-POOL-CLASS
- symptom: RX EAPOL reaches IO80211 input, but EAPOL TX, key install, and RSN
  completion remain absent.
- expected system behavior: packet pools allocate an
  `IO80211NetworkPacket`-family object before IO80211 input; that class parses
  packet type and returns EAPOL type `2` for EtherType `0x888e`.
- actual behavior: local pool allocation returned base Skywalk network packets
  and local RX handoff relied on `reinterpret_cast`.
- exact divergence point: local TX/RX pool allocation hook.
- evidence from runtime: `eapol_rx=8`, `eapol_tx=0`, no `setCIPHER_KEY`, no
  `IO80211RSNDone`, AP deauth reason 15 after successful RX input.
- evidence from decomp: Apple packet pool creates Apple packet objects in the
  `IO80211NetworkPacket` family; `IO80211NetworkPacket::getPacketType(...)`
  classifies `0x888e` as EAPOL type `2`; IO80211 input consumers are typed as
  `IO80211NetworkPacket*`.
- exact semantic mismatch between reference and our code: reference satisfies
  the packet object class contract at allocation; local code only satisfied the
  C++ pointer signature by cast.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the current runtime proves
  the RX producer and input call return success while the next EAPOL/key/RSN
  path remains absent; the first remaining confirmed reference mismatch at that
  boundary is packet allocation class and packet-type semantics.
- why proposed fix is 1:1 with reference architecture and semantics: restore a
  packet-pool allocation hook that returns an `IO80211NetworkPacket` object
  before queue/input handoff; do not force consumer state or packet outcomes.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `include/Airport/IO80211NetworkPacket.h`
  - docs/reference/YAML/audit/inventory/analysis files
- forbidden alternative fixes considered and rejected:
  - forced EAPOL TX, key install, RSN done, DHCP, or link success.
  - retry, replay, delay, poll loop, or deauth masking.
  - raw Apple packet scratch synthesis at `packet+0x78`.
  - scratch-dependent log methods without Apple packet scratch ownership.
- verification plan:
  - `git diff --check`.
  - targeted YAML parse.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-170 Stage 1 structural request superseding CR-169.

## Current Active Batch Note

- id: A-CR170-IO80211-NETWORK-PACKET-POOL-CLASS
- status: implemented locally, preparing Stage 1 structural review as CR-170.
- scope: supersedes CR-169 by adding the next confirmed packet-class allocation
  restoration adjacent to the active EAPOL/RSN blocker.
- evidence:
  - Apple packet pool owns packet allocation and returns
    `IO80211NetworkPacket`-family objects.
  - IO80211 packet type parsing is class behavior, not a generic pointer cast.
  - runtime proves RX input success without EAPOL TX/key/RSN progression.
- local divergence:
  - base Skywalk network packet pool plus `reinterpret_cast`.
- fix:
  - allocate real system `IO80211NetworkPacket` objects from a local pool
    subclass via `newPacket(...)`.
- non-claims:
  - no forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
    poll loop, synthetic scratch, synthetic packet, or deauth masking.

## ANOMALY

- id: A-CR173-PACKET-SCRATCH-FIELD-MAP
- status: FIX_IMPLEMENTED
- symptom: Apple packet scratch owner restoration needs a complete field map;
  local `packet_info_tag` had only the early IO80211 monitor/input fields
  named.
- first visible manifestation:
  - 2026-04-27 packet scratch audit found additional decompile-proven Apple
    scratch fields at `+0x48`, `+0x50`, `+0x74`, `+0x80`, `+0x8a`, and
    `+0x90`.
- expected system behavior:
  - Apple packet scratch is size `0x98`.
  - packet methods use bus/virtual address, signature, TX status, flow queue,
    and AC/duplicate fields at fixed offsets.
- actual behavior:
  - local `packet_info_tag` preserved the size and early fields but left these
    later offsets anonymous.
- divergence point:
  - `include/Airport/apple_private_spi.h::packet_info_tag`
- evidence:
  - decomp:
    - `AppleBCMWLANPCIeSkywalkPacket::complete(...)` clears scratch `+0x48`
      and `+0x50`.
    - `setPktSignature(...)` writes scratch `+0x74`.
    - `setStatus/getStatus` use scratch `+0x80`.
    - `setFlowQueueIdx/getFlowQueueIdx` use scratch `+0x8a`.
    - `setAc/getAc` and `setPktDup` use scratch `+0x90`.
    - `free()` releases scratch size `0x98`.
- rejected implementation path:
  - a direct local C++ subclass of `IO80211NetworkPacket` was tested and
    rejected before CR submission.
  - BootKC verification failed on generated vtable references to non-exported
    `IOSkywalkPacket::*` symbols:
    `getDataOff`, `getDataLength`, `getDataOffset`, `getPacketBuffers`,
    `setDataOffAndLen`, `getMemoryDescriptor`, `getPacketBufferCount`,
    `getDataVirtualAddress`, `getDataIOVirtualAddress`.
- confirmed deviation:
  - local field map was incomplete.
- root cause:
  - confirmed for the field-map layer; not claimed as final connection root
    cause.
- fix:
  - add named fields and offset assertions while preserving total size `0x98`.
- verification:
  - pending CR-173 structural build evidence.

## Current Active Batch Note

- id: A-CR173-PACKET-SCRATCH-FIELD-MAP
- status: implemented locally, preparing Stage 1 structural review as CR-173.
- scope: supersedes CR-172 with a safe scratch field-map restoration and a
  documented rejection of direct C++ packet subclassing.
- evidence:
  - decomp proves scratch offsets and direct subclass build failed BootKC
    symbol verification.
- local divergence:
  - packet_info_tag had anonymous later scratch fields.
- fix:
  - name fields and assert offsets; no behavior change.
- non-claims:
  - no forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
    poll loop, synthetic scratch, synthetic packet, or deauth masking.

## ANOMALY

- id: A-CR172-IO80211-NETWORK-PACKET-VIRTUAL-SURFACE
- status: FIX_IMPLEMENTED
- symptom: exact restoration of the Apple packet subclass/scratch layer is
  blocked because the local `IO80211NetworkPacket` declaration lacks the
  exported intermediate packet surface.
- first visible manifestation:
  - 2026-04-27 decomp/static audit after CR-171 found that local
    `IO80211NetworkPacket` was still an empty subclass while Apple packet
    constructors enter a real IO80211 packet class layer.
- expected system behavior:
  - `IO80211NetworkPacket` declares metaclass state, `getPacketType`,
    virtual packet helpers, firmware TX status helpers, `getBufferSize`, and
    both `prepareWithQueue(...)` overloads.
- actual behavior:
  - local `IO80211NetworkPacket` only inherited `IOSkywalkNetworkPacket` and
    declared no methods of its own.
- divergence point:
  - `include/Airport/IO80211NetworkPacket.h`
- evidence:
  - decomp:
    - `kdk_symbols.txt` exports the full IO80211NetworkPacket method list.
    - `IO80211Family_decompiled.c` shows constructor chain, deallocation size
      `0x78`, EAPOL packet-type classification, and method stub/tailcall
      semantics.
    - `AppleBCMWLANBusInterfacePCIeMac_decompiled.c` shows
      `AppleBCMWLANPCIeSkywalkPacket` constructors calling the
      `IO80211NetworkPacket` constructor chain before installing the Apple
      vtable.
  - runtime context:
    - current CONTROL_STA_NETWORK runs still localize the active blocker to the
      packet/IO80211 handoff after RX EAPOL input succeeds without EAPOL
      TX/key/RSN progression.
- candidate causes:
  - confirmed: local header did not model the decompile/export-proven
    intermediate packet class surface.
- rejected causes:
  - guessed Apple subclass.
  - raw writes to `packet+0x78`.
  - forced EAPOL/key/RSN/DHCP/link success, retry, replay, delay, poll loop,
    packet synthesis, or deauth masking.
- confirmed deviation:
  - reference has a concrete `IO80211NetworkPacket` method/vtable layer.
  - local declaration was empty.
- root cause:
  - confirmed for the layer-restoration blocker: a future local Apple packet
    subclass cannot be declared against the reference base ABI if the
    intermediate packet surface is missing locally.
- fix:
  - add `OSDeclareDefaultStructors(IO80211NetworkPacket)`.
  - add export/decompile-proven method declarations and opaque
    `IO80211NetworkTXStatus`.
  - assert `sizeof(IO80211NetworkPacket) == 0x78`.
- verification:
  - `git diff --check`: pass.
  - `./scripts/build_tahoe.sh`: pass, BootKC symbol verification pass.

## FIX_CANDIDATE

- anomaly_id: A-CR172-IO80211-NETWORK-PACKET-VIRTUAL-SURFACE
- symptom: Apple packet subclass/scratch layer cannot be represented 1:1
  because local `IO80211NetworkPacket` is an empty declaration.
- expected system behavior: `IO80211NetworkPacket` has its own exported
  metaclass/method surface before Apple packet subclasses install their
  vtables.
- actual behavior: local header declared no IO80211 packet surface.
- exact divergence point: `include/Airport/IO80211NetworkPacket.h`.
- evidence from runtime: current runtime keeps packet/IO80211 handoff as the
  active layer; RX EAPOL succeeds, but EAPOL TX/key/RSN progression is absent.
- evidence from decomp: symbols and decomp prove the method surface,
  constructor chain, size `0x78`, packet-type parser, and Apple subclass
  constructor dependency.
- exact semantic mismatch between reference and our code: reference has a real
  intermediate packet class; local code modeled only an empty shell.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: this directly prevents
  compiling the next decompile-proven Apple packet subclass ABI without
  guessing.
- why proposed fix is 1:1 with reference architecture and semantics: add only
  proven declarations and size assertion; do not instantiate or alter runtime.
- files/functions to modify:
  - `include/Airport/IO80211NetworkPacket.h`
  - docs/reference/YAML/audit/inventory/analysis files
- forbidden alternative fixes considered and rejected:
  - guessed subclass/vtable.
  - raw scratch writes.
  - forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay, poll
    loop, packet synthesis, or deauth masking.
- verification plan:
  - `git diff --check`.
  - targeted YAML parse.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-172 Stage 1 structural request superseding CR-171.

## Current Active Batch Note

- id: A-CR172-IO80211-NETWORK-PACKET-VIRTUAL-SURFACE
- status: implemented locally, preparing Stage 1 structural review as CR-172.
- scope: supersedes CR-171 by adding the next confirmed packet ABI header
  surface required before Apple packet subclass/scratch restoration.
- evidence:
  - export/decomp proves the IO80211NetworkPacket method surface and Apple
    subclass constructor dependency.
- local divergence:
  - local IO80211NetworkPacket header was empty.
- fix:
  - add proven method declarations, ABI enum, and size assertion.
- non-claims:
  - no forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
    poll loop, synthetic scratch, synthetic packet, or deauth masking.

## ANOMALY

- id: A-CR171-IOSKYWALK-NETWORK-PACKET-SIZE
- status: FIX_IMPLEMENTED
- symptom: exact restoration of the Apple packet scratch/datapath layer is
  blocked because the local base packet declaration consumes the subclass
  scratch-pointer slot.
- first visible manifestation:
  - 2026-04-27 decomp/static audit after CR-170 found a class-size conflict:
    reference base packets are `0x78`, while the local declaration included an
    extra pointer.
- expected system behavior:
  - `IOSkywalkNetworkPacket` and `IO80211NetworkPacket` are `0x78` byte class
    instances.
  - `AppleBCMWLANPCIeSkywalkPacket` is the `0x80` subclass that owns the
    scratch pointer at packet offset `+0x78`.
- actual behavior:
  - tracked local `IOSkywalkNetworkPacket` declared a protected
    `void *mReserved` member.
  - any local subclass would place its first field at `+0x80`, not at the
    Apple scratch-pointer offset `+0x78`.
- divergence point:
  - `include/Airport/IOSkywalkNetworkPacket.h`
- evidence:
  - decomp:
    - `IOSkywalkFamily_decompiled.c`: `IOSkywalkNetworkPacket` metaclass size
      `0x78`.
    - `IO80211Family_decompiled.c`: `IO80211NetworkPacket` constructor chain
      enters `IOSkywalkNetworkPacket`; deallocation size is `0x78`.
    - `AppleBCMWLANBusInterfacePCIeMac_decompiled.c`:
      `AppleBCMWLANPCIeSkywalkPacket` size is `0x80`; scratch pointer is read
      at packet `+0x78`; scratch size is `0x98`.
  - runtime context:
    - current CONTROL_STA_NETWORK runs still localize the active blocker to the
      packet/IO80211 handoff after RX EAPOL input succeeds without EAPOL
      TX/key/RSN progression.
- candidate causes:
  - confirmed: local base-size declaration does not match the reference base
    packet class and would break the next scratch-bearing subclass layer.
- rejected causes:
  - raw writes to `packet+0x78` on a base packet.
  - forcing EAPOL TX, key install, RSN done, DHCP, link, retry, replay, delay,
    poll loop, packet synthesis, or deauth masking.
- confirmed deviation:
  - reference reserves packet offset `+0x78` for the Apple PCIe subclass, not
    for `IOSkywalkNetworkPacket`.
- root cause:
  - confirmed for the layer-restoration blocker: local declarations made the
    proven Apple subclass layout impossible to represent 1:1.
- fix:
  - remove the non-reference `mReserved` member from the tracked local
    declaration.
  - add `sizeof(IOSkywalkNetworkPacket) == 0x78` assertions.
- verification:
  - pending CR-171 structural build evidence.

## FIX_CANDIDATE

- anomaly_id: A-CR171-IOSKYWALK-NETWORK-PACKET-SIZE
- symptom: packet-class/scratch ABI layer cannot be restored 1:1 while active
  STA EAPOL/RSN work depends on `IO80211NetworkPacket*` handoff.
- expected system behavior: Tahoe registers `IOSkywalkNetworkPacket` and
  `IO80211NetworkPacket` as `0x78` byte classes; Apple PCIe packet subclass is
  `0x80` and owns scratch pointer storage at `+0x78`.
- actual behavior: the tracked local `IOSkywalkNetworkPacket` declaration adds
  `void *mReserved`, extending the base layout beyond the reference size.
- exact divergence point: tracked local `IOSkywalkNetworkPacket` declaration.
- evidence from runtime: current runtime still shows RX EAPOL input success
  with no EAPOL TX/key/RSN progression, keeping packet/IO80211 handoff as the
  active layer under repair.
- evidence from decomp: `IOSkywalkNetworkPacket` and `IO80211NetworkPacket`
  are size `0x78`; `AppleBCMWLANPCIeSkywalkPacket` is size `0x80` and uses
  `packet+0x78` for scratch.
- exact semantic mismatch between reference and our code: local base class
  reserved storage that reference assigns to the first subclass layer.
- fix justification path: REFERENCE_ALIGNMENT_FIX
- why this is root cause and not just correlation: the mismatch directly
  prevents exact implementation of the decompile-proven subclass scratch
  layout; no runtime hypothesis is needed for this ABI contradiction.
- why proposed fix is 1:1 with reference architecture and semantics: remove
  the absent field and assert the proven base class size.
- files/functions to modify:
  - `include/Airport/IOSkywalkNetworkPacket.h`
  - docs/reference/YAML/audit/inventory/analysis files
- forbidden alternative fixes considered and rejected:
  - raw scratch writes on base packets.
  - synthetic Apple packet scratch without class-size/vtable proof.
  - forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay, poll
    loop, packet synthesis, or deauth masking.
- verification plan:
  - `git diff --check`.
  - targeted YAML parse.
  - `./scripts/build_tahoe.sh`.
  - `./scripts/build_regdiag.sh`.
  - submit CR-171 Stage 1 structural request superseding CR-170.

## Current Active Batch Note

- id: A-CR171-IOSKYWALK-NETWORK-PACKET-SIZE
- status: implemented locally, preparing Stage 1 structural review as CR-171.
- scope: supersedes CR-170 by adding the next confirmed packet ABI foundation
  fix required before Apple packet scratch/datapath restoration.
- evidence:
  - decomp proves base packet size `0x78` and Apple subclass scratch pointer
    at `+0x78`.
- local divergence:
  - local base class had an extra pointer, shifting subclass storage.
- fix:
  - remove non-reference storage and add class-size assertions.
- non-claims:
  - no forced EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
    poll loop, synthetic scratch, synthetic packet, or deauth masking.
