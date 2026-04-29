# AppleBCMWLAN APSTA Power, Offload, And Datapath Tail Reference

Source: Tahoe `AppleBCMWLANCoreMac` disassembly.

## MPDU Override

Function: `AppleBCMWLANIO80211APSTAInterface::configureMPDUSize(uint32_t)`
Range: `0xffffff80016925f6..0xffffff8001692770`.

Recovered contract:

- Read core-private block through `state+0x218 -> +0x128`.
- Send `ampdu_mpdu` only when core-private dword `+0x3fc == 2` and
  core-private dword `+0x30c <= 4`.
- Payload is the caller value, size `4`.
- Commander source is `state+0x228`.
- Failure is logged; success has verbose mask `0x80`.

## Low-Power And ARP Offload Chain

Functions:

- `configureLowPowerModeExit()` at `0xffffff80016928e4`
- `handleSetARPOffloadAsyncCallBack(...)` at `0xffffff8001692bee`
- `handleSetARPHostIPClearAsyncCallBack(...)` at `0xffffff8001692d80`
- `handleSetARPHostIPAsyncCallBack(...)` at `0xffffff8001692f2c`
- `handleSetBcnWaitPeriodAsyncCallBack(...)` at `0xffffff8001692fbc`
- `handleSetLowPowerModeAsyncCallBack(...)` at `0xffffff800169310a`

Recovered contract:

- Low-power exit returns immediately when `state+0xb4 == 0`.
- Active low-power exit uses `lphs_mode`, payload value `0`, payload size `4`,
  no RX expected, callback `handleSetLowPowerModeAsyncCallBack`, cookie `0`.
- Beacon wait-period success enters low power by sending `lphs_mode`, payload
  value `1`, payload size `4`, no RX expected, callback
  `handleSetLowPowerModeAsyncCallBack`.
- ARP offload callback success sends `arp_hostip_clear` with no TX payload, no
  RX expected, callback `handleSetARPHostIPClearAsyncCallBack`, cookie `0`.
- ARP host-IP clear success reads the IPv4 value from `state+0xac` and sends
  `arp_hostip`, payload size `4`, no RX expected, callback
  `handleSetARPHostIPAsyncCallBack`, cookie `0`.

## RPSNOA And Beacon Duty Cycle

Functions:

- `setBeaconDutyCycle(uint32_t)` at `0xffffff800169319a`
- `handleSetRpsNoaAsyncCallBack(...)` at `0xffffff8001693338`
- `handleSetScbProbeAsyncCallBack(...)` at `0xffffff80016933ec`
- `configureBeaconDutyCycleParams(apple80211_softap_ps_dynamic_level)` at
  `0xffffff80016934a0`

Recovered contract:

- `setBeaconDutyCycle` sends `rpsnoa` with payload size `0x10`.
- The `0x10` payload has qword header `0x100100101` at `+0x00`, zero dword at
  `+0x08`, word `2` at `+0x0c`, and word `(requested != 0)` at `+0x0e`.
- `configureBeaconDutyCycleParams` sends `rpsnoa` with payload size `0x18`.
- The `0x18` payload has qword header `0x300180101` at `+0x00`, zero dword at
  `+0x08`, word `2` at `+0x0c`, byte `0x0a - dynamicPSParams[level].byte8`
  at `+0x0e`, zero byte at `+0x0f`, and a 32-bit-rotated qword from
  `dynamicPSParams[level]` at `+0x10`.
- Both methods use async or sync virtual IOVAR set depending on the work-queue
  gate vtable `+0x130`.
- RPSNOA and scb-probe callbacks return on status `0`; nonzero status logs an
  error and emits the `CommandRxPayload` bytestream via `state+0x210`.

## Power Assertion And Power Stats

Functions:

- `releaseSoftAPPowerAssertion()` at `0xffffff80016937c2`
- `softApStatsAccumulatePowerStateDuration(...)` at `0xffffff8001693892`
- `powerStateChangeReasonToString(...)` at `0xffffff800169391c`

Recovered contract:

- Release clears `state+0x0c`, sends core notification event `0x8d` through
  `state+0x218 -> +0x128 -> +0x2c20`, payload value `0`, payload size `4`,
  flag `1`, then logs release when enabled.
- Power-state duration adds the supplied duration to
  `state+0x1d0 + state * 0x10`.
- The same method updates `state+0x1a8` with the current time converted through
  the recovered constant multiply/shift path.
- Power-state reason strings cover reasons `0..0x0e`; larger values return
  `"Unknown"`.

## Interface And Datapath Tail

Functions:

- `enable(uint32_t)` at `0xffffff8001693980`
- `disable(uint32_t)` at `0xffffff8001693aa0`
- `enableDatapath()` at `0xffffff8001693b82`
- `disableDatapath()` at `0xffffff8001693e80`
- queue/pool accessors at `0xffffff8001694064..0xffffff8001694174`

Recovered contract:

- `enable` logs, checks vtable `+0xd58`; if true it calls the superclass
  enable slot `+0x860` and then vtable `+0xd98`; otherwise returns
  `0xe00002d5`.
- `disable` logs, calls vtable `+0xda0`, superclass disable slot `+0x868`,
  then logs completion.
- `enableDatapath` first calls vtable `+0xcf0`. If the interface is not
  enabled, it returns `0xe00002bc`; it does not return success.
- Enabled datapath calls owner `state+0x2d0` vtable `+0x120`, starts TX/RX
  completion queues via vtable `+0x150`, then arms RX completion queue via
  vtable `+0x298` with arguments `0,0`.
- `disableDatapath` calls owner `state+0x2d0` vtable `+0x128`, stops RX
  completion queue first and TX completion queue second via vtable `+0x158`.
- Accessors return fixed APSTA state fields: logger `+0x210`, TX queue count
  `+0x2a4`, TX completion `+0x2e8`, RX completion `+0x2f0`, multicast
  `+0x320`, TX pool `+0x2d8`, RX pool `+0x2e0`, and TX subqueues via
  `state+0x2b8` AC map into `state+0x300`.

## MAC Address And SoftAP Peer Stats

Functions:

- `setMacAddress(ether_addr&)` at `0xffffff8001694464`
- `configureSoftAPPeerStats(bool)` at `0xffffff800169456a`
- `handleSoftAPStatsConfigAsyncCallback(...)` at `0xffffff80016947a2`

Recovered contract:

- `setMacAddress` sends `cur_etheraddr` with 6-byte payload only when the
  interface id is not `-1` and AP-up state `state+0x26c` is zero.
- If the interface id is invalid while AP is down, it returns success without
  sending. If AP is already up or the interface id is otherwise rejected, it
  returns `0xe00002bc`.
- SoftAP peer stats are feature-gated by feature `0x7a`.
- The `softap_stats` payload is `0x0e` bytes: dword `0x80002` at `+0x00`,
  word `2 - enabled` at `+0x0a`, and zeros elsewhere.
- The async cookie is one byte carrying the requested enable state.
- Successful callback writes `state+0x328 = cookie & 1`; allocation failure
  returns `0xe00002bc`.

## Local Scope

The local scaffold records constants, offsets, carriers, and static asserts
only. It does not enable APSTA runtime, does not send APSTA IOVARs, and does
not change primary STA behavior.
