# IWM 9260 hardware RFKILL register evidence

The current IWM 9260 remains blocked before radio initialization. Four native
On controls observe the actual CSR read inside `iwm_check_rfkill`, rather
than inferring the block from its cached flag. Each returns
`CSR_GP_CNTRL=0x00040000`, with RFKILL clear bit 27 unset, followed by blocked 1.
PCI command is `0x0006`; PMCSR is `0x0008`, whose power state is D0. These
observations do not establish the adapter's physical disable cause or full
MMIO health.

The [Intel CSR definitions](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/iwl-csr.h)
describe CSR access as available under platform power even when the MAC is
reset or asleep. A zero MAC clock ready bit therefore does not itself
invalidate this CSR RFKILL observation. Earlier Linux also reported a hard
block, as retained in the [9260 qualification](TAHOE_IWM_9260_RUNTIME_20261009.md).
Adapter W_DISABLE wiring or a switch remains a hypothesis, not a proven
cause. No disable bypass or new MMIO operation was introduced.

The observer completed with status zero and TRACE_COMPLETE in
`/private/var/tmp/iwm-sleep-stop-local.h0HRQV`. It records existing register
read returns only. The loaded image remains source `c9f1d745`, UUID
`8A5BF340-BFC5-3C2E-8FF8-E27D52B48868`, boot
`CA1DAC07-E228-41F6-A8E3-7488217F4D1E`. Wi Fi ends Off; en2 management and
its default route remain intact. Utility exit zero is not radio admission.

## Sleep observation transport

The separate second real S3 control preserves the same boot, loaded image,
management and Wi Fi Off. Its power history records an 85 second interval
and WakeTime 1.337 seconds. The host streamed observer lost its SSH transport
and had no terminal marker or usable lower stop timings. Missing records do
not prove that the HAL stop was skipped or explain the previously measured
668 millisecond disable.

The replacement root private observer writes its log and terminal status in
the guest independently of SSH. The first idle run in
`/private/var/tmp/iwm-sleep-stop-local.8g3O6V` completes with status zero and
TRACE_COMPLETE after SSH detach. That qualifies ordinary detach durability,
not survival of a subsequent real S3. Both observers are terminal. Physical
host 10.90.10.22 and the QEMU base or overlay files were not replaced.
