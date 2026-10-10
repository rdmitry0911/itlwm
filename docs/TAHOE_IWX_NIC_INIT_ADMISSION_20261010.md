# IWX NIC initialization stops after a failed MAC clock admission

IWX now preserves APM failure before NIC peripheral and RX initialization.
The existing firmware start caller returns that original error rather than
continuing after the MAC clock failed to stabilize. This supports initial
and repeated startup and recovery after sleep. Software checks do not
establish successful service on a physical IWX device.

## Reproduced error propagation failure

The complete `iwx_apm_init`, `iwx_nic_init` and `iwx_start_fw` bodies from
`84511a48` execute with actual register and family definitions on Linux and
macOS. A failed MAC clock poll returns ETIMEDOUT from APM, but the old NIC
caller ignores it. NIC configuration and RX initialization run, shadow
registers enable, and execution reaches the firmware loader. Independently
successful downstream boundaries make the old full caller return zero.

Both 22000 and AX210 controls compile and fail the required ETIMEDOUT
assertion with exit 134 on both systems. The independent full firmware start
control for prepare_card_hw failure passes, localizing this error to the
ignored APM return. The first Linux fixture compile lacked initializer_list;
adding that standard header fixes only the adapter, not the driver.

The frozen macOS baseline is
`/private/var/tmp/iwx-nic-init-baseline-84511a48.UFZwLC/source`. Its source
archive SHA256 is
`2d81f829dc28571a05837ddcfe633cf538c8a1e1bb98eb8ab3a11ebbc38301fb`;
the fixture archive is
`70a4a1ec456dbe62c8a61b73f3afc40a2ea70ebc1c43c98f9dfc2b9b5bc20187`.
MMIO results, PCI preparation, downstream RX setup and firmware delivery
are explicit boundaries, not firmware ready or radio success receipts.

## Admission and cleanup contracts

The [Intel gen2 NIC initialization path](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/trans-gen2.c)
returns an APM error before NIC configuration or RX initialization. IWX
now follows that admission rule. The existing full firmware start body
already returns a NIC error to its initialization owner. The correction
does not change firmware commands, register operations, RFKILL handling,
queue completion ownership or DMA reset fencing.

The initial CSR interrupt acknowledgement and RFKILL handshake precede
the clock admission in the existing caller. Those CSR operations remain
unchanged; failure prevents later NIC and RX setup, not the already executed
CSR handshake. A later attempt must obtain its own actual clock admission.

## Executable software checks

The fixture extracts all three complete production bodies and exercises
40 scenarios under ASan and UBSan: direct NIC and full firmware startup,
clock timeout with independent RX or firmware errors, normal clock admission,
preserved RX and firmware failures, a fresh attempt after a failed admission,
and early PCI preparation errors. The family definitions select fixture
configuration; no physical IWX execution is simulated by that selection.

The candidate passes all 40 cases and the complete payload aggregate on
Linux and macOS. The separate macOS candidate source remains at
`/private/var/tmp/iwx-nic-init-wip-20261010.qvmEyt/source`; the frozen baseline
is untouched. Adjacent IWM initialization and IWX init and stop controls
also retain their passing results. The macOS wrapper first finishes the
aggregate and then returns 127 for an incorrectly named adjacent script;
the actual adjacent transport gates are checked separately before committing.

The IWM SAE transport gate passes. The separate IWX SAE transport source gate
returns one on both the committed `cb596421` baseline and the candidate. It
requires the deferred TX worker to release its task lease before the controller
callback, while `d2d8f1d8` retains that lease through callback and join retirement.
The NIC correction does not change that worker. This existing contract conflict
is retained for an executable callback and stop lifetime audit; neither the gate
nor the worker is weakened to make this candidate appear fully green.

The gate is included in the complete payload aggregate. Current laboratory
hardware is IWM 9260 with hardware RFKILL. Whole kext loading and IWM power
regression cannot qualify the changed IWX path, repeated GUI joins, DHCP,
traffic, WPA3 or AP service on a physical IWX device.
