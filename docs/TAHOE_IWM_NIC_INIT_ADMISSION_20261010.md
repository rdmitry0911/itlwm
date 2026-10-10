# IWM NIC initialization stops after a failed MAC clock admission

IWM now preserves an APM initialization error before configuring NIC
peripherals, RX and TX rings or starting firmware. This protects initial and
repeated radio startup, including recovery after sleep, from continuing after
the MAC clock fails to stabilize. It does not unblock the laboratory 9260
hardware RFKILL or establish a successful on air recovery.

## Reproduced error propagation failure

The complete production `iwm_apm_init`, `iwm_nic_init` and `iwm_start_fw`
functions from `9f192fdd` execute with actual register definitions. A failed
MAC clock poll returns ETIMEDOUT from APM, but the old NIC body ignores it,
configures the NIC, invokes RX and TX initialization, enables shadow registers
and reaches the firmware loader. With independently successful downstream
boundaries, it returns zero. An independent downstream error can also replace
the original APM failure.

All three family controls, 7000, 8000 and 9000, compile and fail the required
ETIMEDOUT assertion with exit 134 on Linux and macOS. The macOS frozen source
is `/private/var/tmp/nic-init-baseline-9f192fdd.9Y5B4z/source`; it remains
separate from the candidate. The first Linux adapter compile lacked the
transitive IWM_PRPH_BASE definition. Adding that actual definition makes the
historical controls executable; the compile error is not a behavioral result.

## Hardware and lifecycle contracts

The [Intel NIC initialization path](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/trans.c)
returns its APM error before NIC configuration or RX and TX initialization.
The [CSR contract](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/iwl-csr.h)
requires MAC clock readiness before accessing internal resources, while CSR
access itself does not require that wake admission. The existing early
interrupt acknowledgement in `iwm_start_fw` therefore remains unchanged.

The existing `iwm_start_fw` already propagates NIC initialization errors.
Preserving APM failure at the NIC boundary lets that existing chain reach
the owned radio initialization cleanup and retry policy. No successful
firmware, radio ready, DMA idle or authentication receipt is manufactured.

The separate stop audit finds that the ring reclaim ordering is inherited
from [OpenBSD IWM](https://raw.githubusercontent.com/openbsd/src/master/sys/dev/pci/if_iwm.c).
Intel's [TX stop implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/tx.c)
also proceeds to unmapping after a failed NIC access or channel idle wait.
Those sources do not justify treating an elapsed timeout as DMA idle. This
correction does not change stop waits, ring reclaim ordering or reset fencing.

## Executable software qualification

The new fixture extracts all three complete production bodies and checks
108 scenarios under ASan and UBSan. Coverage includes all three device
families, the oscillator workaround on and off, direct NIC initialization
and the complete firmware start caller, APM timeout with independent
downstream failures, unchanged RX and TX error propagation, firmware error
propagation, and a fresh successful clock admission after a failed attempt.

MMIO outcomes, PCI configuration, downstream ring setup and firmware delivery
remain explicit doubles. Reaching the firmware boundary is a control flow
observation, not an observed firmware load or radio connection. The full
payload aggregate includes the new gate; current IWM GUI, DHCP, traffic and
WPA3 qualification still requires unblocked hardware.

The full candidate payload aggregate passes on Linux and macOS. The macOS
candidate stage is `/private/var/tmp/nic-init-wip-20261010.RpwICU/source`.
The adjacent IWM init and stop lifetime and SAE transport checks retain their
passing results. The frozen historical stage is not overwritten.

## Exact build and loaded sleep regression

Production `cb59642142e0ea202e926d7014a3b5fdc6d41a74` is committed, pushed
and independently matched to the remote. Exact committed Linux and macOS
payload gates pass. The ordinary Tahoe build succeeds and all 1088 imports
resolve against the running guest BootKC. The isolated checkout is clean and
both complete build bundles compare equal.

- Source identity: `5e87aa562af0`.
- Mach O UUID: `935EBF38-91F0-3575-98BE-2F4E6A75C014`.
- Mach O SHA256: `6c8c420aa271fce82ba3a85ef342e91259c201ac57627b43d5e9103109b1a5b1`.
- Loaded boot: `080BF3D4-EE4B-412A-A72D-704D3354D2F3`.

Private activation `activation-20261010T142128Z` reaches readiness for guest
reboot, preserves four companion kexts and rollback, and verifies installed
full bundle equality. The first reboot guard names a nonexistent JSON summary
and exits before requesting reboot. The actual protected TXT summary is
independently reconciled; one guarded reboot then loads the exact UUID. The
first postboot SSH banner observation times out; the next verifies the new
boot, image, bundle, en2 management and actual NIC, APM and firmware start
entry probes. No activation, reboot or QEMU restart is replayed.

The durable guest observer is verified live with its DTrace child and READY
before cold native Off and On. Public power reads On, but en1 remains inactive
and actual hardware RFKILL blocks initialization. Two independent monitor
observations confirm paused suspended state; serial confirms System Sleep
and ACPI SLEEP. One exact private monitor wake resumes the same boot and image.
Power history records a 46 second interval and WakeTime 1.266 seconds, not
the full physical paused duration. The WindowServer 30 second acknowledgement
timeout remains separate from Wi Fi service recovery.

The first postwake SSH observation times out before executing controls. The
next verifies image and management and executes four native Off and On
controls. The durable observer in
`/private/var/tmp/iwm-sleep-stop-local.zU2j6H` survives real S3 and ends with
status zero and TRACE_COMPLETE, but completes before those four controls.
Its actual disable takes 669.183 milliseconds, with two failed NIC access
waits of 325.305 and 329.972 milliseconds and a 3.586 microsecond worker drain.
This fix does not reduce stop delay or establish DMA idle.

A separate bounded postwake observer completes with status zero and its own
COMPLETE marker. Four additional native Off and On controls return actual
NotReady in 2.013, 1.873, 1.842 and 1.883 milliseconds. Every RFKILL check
reports blocked. Wi Fi ends Off and independent en2 management remains intact.
The changed NIC, APM and firmware start functions do not execute under this
RFKILL refusal; probe presence is not execution or on air qualification.

Physical host 10.90.10.22, the original dirty guest checkout and host side
base and overlay files remain untouched. The owned QEMU unit remains active
with the same PID 517226.

## Additional laboratory release

The additional unsigned Debug asset is
[AirportItlwm-Tahoe-NicInitAdmission-cb596421.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-NicInitAdmission-cb596421.kext.zip).
Built, installed, snapshot and extracted complete bundles compare equal
without a packaging rebuild.

- Asset ID: `628178182`; size: 15,719,269 bytes.
- ZIP SHA256: `e5b65d27898e86e6e0c7cc5df2813cc4e1c5024d45777dbb481071ac07872f1b`.

A fresh API check verifies 19 assets, all eighteen older asset records and
the default archive unchanged. The complete old release notes remain the
exact suffix below the new LAB entry. An independent release download
compares byte for byte with the local verified archive. The label and notes
retain hardware RFKILL and the absent on air qualification.

Repeated native GUI open, WPA2 and WPA3 transitions among saved networks,
automatic recovery without Off and On, DHCP and restored traffic after sleep
remain the operational priority. Current IWM cells, AP service and new IWX
hardware are not qualified by this correction. The analogous ignored APM
return in IWX is the next software admission audit.
