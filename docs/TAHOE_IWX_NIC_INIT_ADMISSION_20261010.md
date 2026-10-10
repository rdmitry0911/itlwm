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

## Exact build and private activation

Production `0ba9dc5975ea6d6d3fb8dece46aa1b8642073e05` is committed, pushed
and independently matched to the remote. The exact Linux and macOS payload
aggregates pass. The ordinary Tahoe build succeeds with all 1088 imports
resolved against the running guest BootKC. The isolated checkout is clean;
both complete build bundles compare equal.

- Source identity: `e64157f36cc3`.
- Mach O UUID: `1A4E2AAD-09ED-3D4C-A105-E258B0B09EDC`.
- Mach O SHA256: `4f8307436a6a0990ff2151258ea2b54d9f6b214a8c679e13db73af8c0a50d3b2`.

Private activation `activation-20261010T145135Z` preserves four companions
and rollback, reaches READY_FOR_GUEST_REBOOT and verifies the complete
installed bundle. An independent check of the actual protected TXT summary
confirms both collection member sets and readiness before one guarded guest
reboot. Physical host 10.90.10.22 and the original dirty guest checkout are
untouched; the owned QEMU stays at PID 517226 without restart or host side
base or overlay replacement.

## Loaded IWM sleep regression

The exact new image loads in boot `8B68E81F-7422-4270-B215-2CB286E0E4D7`.
Installed and built full bundles compare equal and independent en2 management
remains available. The first postboot SSH banner observation times out; the
next confirms the new boot and exact UUID, without replaying the reboot.
All three changed IWX path entry probes are present in the loaded image.

The root private observer in
`/private/var/tmp/iwm-sleep-stop-local.B3ksZx` is verified live with its DTrace
child and READY before native cold Off and On and one sleep request. Public
power reads On, but en1 is inactive and an actual IWM RFKILL check reports
blocked. Two independent private monitor observations confirm suspended
state; serial records System Sleep and ACPI SLEEP. One exact monitor wake
resumes the same boot and image. Power history records a 43 second interval
and WakeTime 1.273 seconds, not the full physical paused duration. The
WindowServer 30000 millisecond acknowledgement timeout remains recorded.

Four native postwake Off and On controls finish with Wi Fi Off and en2
management intact. The durable observer survives real S3 and covers all four
controls before completing with status zero and TRACE_COMPLETE. Actual
sleep disable takes 671.258 milliseconds: NIC access failures account for
330.250 and 326.721 milliseconds, while task drain takes 5.714 microseconds.
The first postwake Off takes another 670.829 milliseconds. Actual postwake
On returns NotReady in 1.928, 1.818, 1.867 and 2.045 milliseconds; every RFKILL
check reports blocked. Utility exit zero is not radio admission.

No IWX initialization executes on this IWM device. This loaded regression
does not qualify the changed path on physical IWX, on air GUI selections,
WPA3, DHCP, restored traffic or AP operation. Stop latency and cold public
On under lower RFKILL remain observations for the next recovery audit.

## Saved artifact before host reboot

The verified installed bundle is packaged without rebuilding as
`AirportItlwm-Tahoe-IwxNicInitAdmission-0ba9dc59.kext.zip`. Build, installed,
snapshot and extracted complete bundles compare equal. The archive has
15,719,267 bytes and SHA256
`b309b57c8b4f25a7c21b3b35dbae88ed2e17a5fa0637b15942f71dfafa8a3d4d`.
The package and runtime logs are retained in
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.

The user requested a saved checkpoint and guest shutdown before rebooting
the laboratory host. This additional archive has not been uploaded or added
to release notes; the existing nineteen release assets remain unchanged.
Publication requires a fresh release baseline and an independent download
comparison after resumption. Do not replay the completed activation or use
the preceding boot as a post reboot guard. First revalidate PCI binding,
loaded identity and hardware RFKILL. If radio admission becomes available,
the repeated native GUI and saved network matrix remains the first runtime
qualification task.

One guarded guest shutdown completes normally with CPU halted and power off.
The owned QEMU unit reports inactive and dead, MainPID zero and success.
No forced QEMU termination or other VM action is used. The host copy of the
archive verifies against the recorded SHA256 before shutdown. Laboratory
cycles are held for the user requested host reboot.
