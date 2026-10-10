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
