# AppleBCMWLAN — IO80211SkywalkInterface non-virtual helper surface (CR-187)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-186

## Scope

Twenty additional public direct-call methods on
`IO80211SkywalkInterface` were recovered from the BootKC symbol table.
Each helper takes only primitive arguments or already-known opaque
pointers (`IO80211PeerManager *`, `IO80211Peer *`, `ether_addr *`,
`ether_addr &`, `void *`, `char const *`).

The local kext does not call any of these helpers in CR-187; the
declarations are added inside the existing
`class IO80211SkywalkInterface` body so future caller-wiring CRs can
reference the BootKC symbols without per-caller extern shims.

## Deferred from this batch

Five exports were excluded from CR-187 because declaring them would
implicitly override a parent-class virtual and introduce new vtable
references — breaking the bit-identical kext invariant. They will be
addressed in a future CR that explicitly approves vtable references:

| Address              | Symbol                                                    | Parent virtual                                  |
|----------------------|-----------------------------------------------------------|-------------------------------------------------|
| `0xffffff8002274c9e` | `getBSDName()`                                            | `IOSkywalkNetworkInterface::getBSDName() const` |
| `0xffffff8002277082` | `getHardwareAddress(ether_addr*)`                         | `IOSkywalkEthernetInterface::getHardwareAddress`|
| `0xffffff800227711a` | `setHardwareAddress(ether_addr*)`                         | `IOSkywalkEthernetInterface::setHardwareAddress`|
| `0xffffff8002276f86` | `stringFromReturn(int)`                                   | `IOService::stringFromReturn`                   |
| `0xffffff800227862e` | `errnoFromReturn(int)`                                    | `IOService::errnoFromReturn`                    |

## Recovered exports

| Address              | Symbol                                                                                         |
|----------------------|------------------------------------------------------------------------------------------------|
| `0xffffff800227849c` | `IO80211SkywalkInterface::pidLockPid()`                                                       |
| `0xffffff80022772e6` | `IO80211SkywalkInterface::setPidLock(bool)`                                                   |
| `0xffffff8002276fcc` | `IO80211SkywalkInterface::getWorkQueue()`                                                     |
| `0xffffff8002274c8e` | `IO80211SkywalkInterface::getInterfaceId()`                                                   |
| `0xffffff8002274c7c` | `IO80211SkywalkInterface::getPeerManager()`                                                   |
| `0xffffff8002276fde` | `IO80211SkywalkInterface::getPeerMonitor(IO80211Peer*)`                                       |
| `0xffffff80022770fa` | `IO80211SkywalkInterface::setInitMacAddress(ether_addr&)`                                     |
| `0xffffff8002278916` | `IO80211SkywalkInterface::getMacAddressAgent()`                                               |
| `0xffffff8002278466` | `IO80211SkywalkInterface::getParentInterface()`                                               |
| `0xffffff8002277cb4` | `IO80211SkywalkInterface::getInterfaceMonitor()`                                              |
| `0xffffff8002274bbc` | `IO80211SkywalkInterface::getInterfaceRoleStr()`                                              |
| `0xffffff800227848a` | `IO80211SkywalkInterface::isLowLatencyEnabled()`                                              |
| `0xffffff80022772b2` | `IO80211SkywalkInterface::postMessageInternal(unsigned int, void*, unsigned long, bool)`      |
| `0xffffff800227776e` | `IO80211SkywalkInterface::postMessageSync(unsigned int, void*, unsigned long)`                |
| `0xffffff80022788a4` | `IO80211SkywalkInterface::routeIoctlToWcl(unsigned int, unsigned int, void*, unsigned long)`  |
| `0xffffff8002278414` | `IO80211SkywalkInterface::getDeviceType()`                                                    |
| `0xffffff8002278428` | `IO80211SkywalkInterface::setDeviceType(unsigned int)`                                        |
| `0xffffff80022771f0` | `IO80211SkywalkInterface::getMediumType()`                                                    |
| `0xffffff80022774f2` | `IO80211SkywalkInterface::getPowerState()`                                                    |
| `0xffffff80022783cc` | `IO80211SkywalkInterface::getPropertyTable()`                                                 |
| `0xffffff8002276868` | `IO80211SkywalkInterface::isCommandAllowed()`                                                 |

## Non-claims

CR-187 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211SkywalkInterface`, and does not change runtime behavior. The
kext is bit-identical to CR-186 (sha256/UUID unchanged), confirming
the change is purely structural reference alignment.
