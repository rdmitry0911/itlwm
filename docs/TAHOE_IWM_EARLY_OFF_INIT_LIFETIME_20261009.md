# IWM Off during firmware init: lifetime and hardware quiescence

## Reproduced failure

An IWM firmware init starts hardware before setting `IFF_RUNNING`. The old
`disable` cleared `IFF_UP`, but `DVACT_QUIESCE` called `iwm_stop` only when
`IFF_RUNNING` was already set. Off could therefore return while that init
still owned live firmware/DMA. A replacement On restored `IFF_UP`; the old
init could reopen its unchanged scan lease and emit ready with the replacement
request epoch.

The full-function historical control from `9dab1748` reproduces both cases.
Early Off exits 134 because it returns before quiescence. Early Off/On exits
134 with `result=0 premature=1 hardwareLive=1 stops=0 scans=1 readyEpoch=84`:
the firmware attempt began under request 42, not request 84. Firmware and
ready production are explicit doubles; this is not a measured on-air race.
Initial fixture compile failures were retained, then corrected before either
authoritative behavioral negative was run.

## Lower ownership change

IWM now admits one firmware-init owner under its existing sleeping TX
lifecycle lock. That owner also holds an existing detach lease until every
init exit. Another init cannot reset mutable BA state or begin hardware while
an init or stop owns the lifetime.

Stop closes `SHUTDOWN` and advances the hardware generation before scan/state
invalidation. It cancels queued work, wakes the first-scan waiter and drains
the init owner before `iwm_stop_device` erases DMA/rings. Quiesce no longer
depends on `IFF_RUNNING`. Init rechecks its owner after hardware init and
before/after the ready wait, and leaves through one release tail. It cannot
reopen an old scan lease after Off or report success after stop wins.

A timeout originating inside init retains its one self-reference while
stopping, so it does not wait on itself. If an external stop already owns the
reset, the init caller exits instead of waiting for that stop. A second
external Off waits for the first stop's actual erase. New enable is refused
while shutdown owns admission.

This uses the existing IWX init/stop owner-count pattern, not Broadcom private
field offsets. The framework contract remains the reference powerOff quiesce
boundary and its separate system/radio availability carriers; see
`docs/reference/CR-480-system-pm-state-word-20260711.md`. No new carrier,
RFKILL bypass or forced ready is introduced. Firmware waits remain bounded by
their existing timeouts; this change does not invent a firmware-loader abort.

## Verification scope

`scripts/test_iwm_radio_init_stop.sh` executes the complete production
`iwm_init`, `iwm_stop`/`iwm_stop_internal`, `disable`, `enable`, `iwm_activate`,
all six new owner helpers, and the actual scan-reopen/ready validators.
`ItlScanCommandLease` is the real header. Hardware, task queues, net80211
soft-state and IOKit locks are explicit doubles. Two threaded controls pause
hardware init while actual Off runs; a third pauses hardware erase while a
second actual Off waits.

Linux ASan/UBSan passes early Off, early Off/On with a fresh replacement init,
normal, missing ready/timeout, hardware failure, overlapping Off and admission
controls. The old full init/stop/disable/activate bodies compile, then fail
both intended historical assertions. The existing full IWM/IWX ready-init
fixtures remain separate and retain their explicit ownership doubles.

The Linux payload aggregate, physical/standard scan, power-off/link-down,
unexpected AP reset replay and resident IWM SAE owner contracts pass. The
static stop checks now follow the real `iwm_stop_internal` body and separately
verify the external wrapper, rather than dropping the old cancellation check.

Build, macOS fixture, loaded-image and runtime evidence are pending at this
source checkpoint. Evidence logs are in the current laboratory control root
under `radio-stop-*`.

## Still open

This closes the init owner, not every IWM task producer or command-queue
serialization path. It does not claim that `task_del` drains an already
dequeued state worker. Nor does it move PMF/TX/SAE hook reopening before the
first ready consumer: the separate security-reopen fixtures remain WIP and
their SHUTDOWN negatives remain red.

The physical 9260 is still hardware RFKILL-blocked. Successful firmware init,
GUI repeated open/WPA2/WPA3, saved networks, DHCP/traffic, AP and operational
Wi-Fi after sleep remain unqualified on this card. Physical host `.22` is
untouched.
