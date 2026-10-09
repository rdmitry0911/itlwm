# IWM 9260 runtime qualification 20261009

The Thunderbolt radio is now Intel Wireless-AC 9260, PCI `8086:2526`,
subsystem `8086:0010`. The macOS guest boots with this device assigned to
IWM, but on-air qualification is blocked before initialization by RFKILL.
Separately, the complete extracted IWM and IWX SAE workers still reproduce
the failed-join retirement gap documented on September 11. Successful SAE
association does not establish correctness of this failure/recovery path.

## Source and build

The audited branch is `tahoe-iwn-sae-bridge-runtime`, HEAD `e1b18deb`.
Relevant existing fixes include `a810f34d` (IWM ADD_STA queue mask ordering
for firmware error `0x21a0`), `7176a211` (SAE watchdog queue accounting),
`8930fc58` (IWM/IWX orphan-node TX ring drain), and `41422763` (scan must
not supersede roam). These fixes were already present, not implemented again
during this qualification.

A separate clean macOS worktree built the ordinary Tahoe target against the
running guest's `/System/Library/KernelCollections/BootKernelExtensions.kc`.
The preexisting dirty guest worktree was preserved.

- Source identity: `cfa6c05d6117`.
- Candidate UUID: `9667BCBC-9B26-30E7-9133-EA745F18A377`.
- Candidate Mach-O SHA-256:
  `a79baba9f476e5171d64dc0896f22320dfacc49d884b2df20bd4e79c710fe60a`.
- Build succeeded; all 1088 undefined symbols resolve against the guest BootKC.
- No `_thread_call_cancel_wait` dependency.
- This candidate was built, not installed or released.

## Actual IWM startup

The preceding AX211 guest was paused with QEMU `io-error`; its system disk
reported `nospace`. Available host space had recovered. Resuming the same
guest restored SSH and its saved WPA3 connection. It then shut down normally.
The next guest uses separate byte-verified offline copies of the active
overlay, build disk and UEFI variables; the old files remain available.

The new 9260 guest runs Tahoe 26.2 / 25C56, boot UUID
`758ECA1C-A7B1-4E00-B5F0-524CC771092C`, loaded AirportItlwm UUID
`A50C4C43-044F-37E2-A674-9A267EBA11ED`. Its Wi-Fi interface is `en1`;
independent USB network management is `en2`.

Before VFIO assignment, Linux iwlwifi identified the 9260, loaded firmware
`46.7e3e4b69.0`, and logged `reporting RF_KILL (radio disabled)`. Its rfkill
state was soft-blocked **no**, hard-blocked **yes**. In macOS the card is
identified with firmware `46.4e1ceb39.0`, but serial reports:

```text
iwm_init_task SKIP iwm_init: fatal=2 IFF_UP=1 IFF_RUNNING=0
```

`2` is `IWM_FLAG_RFKILL`, set from the hardware RF-kill bit read by
`iwm_check_rfkill`. One normal `networksetup` off/on cycle returned logical
power to On but reproduced the same initialization skip. Management remained
reachable and no connection was established. This was a native power-control
test, not GUI qualification.

Both drivers observing the block narrows this to the radio/platform path;
the exact physical cause, such as adapter W_DISABLE wiring or a switch, has
not been established. Do not bypass the RFKILL guard or count this as an SAE,
DHCP, GUI, sleep/wake, or AP test. The adapter model and disable controls need
inspection before meaningful on-air qualification can continue.

When the old AX211 QEMU exited, its VFIO FLR timed out after 65535 ms and
Linux reported that AX211 inaccessible. It remains outside the new IWM VM.
Neither the physical host nor host 10.90.10.22 was rebooted.

## Executed software tests

- IWM station-command completion: 254 scenarios PASS.
- Primary RX BA teardown: 84 scenarios PASS.
- SAE engine peer retry: 30 scenarios PASS.
- IWM firmware context owner: 59 scenarios PASS.

These execute production-source fixtures; they are not on-air tests.

## SAE rejection remains open

`test_mvm_sae_peer_failure.sh` had stopped compiling after the gated
AUTH-to-ASSOC continuation was introduced. The fixture now declares that
boundary with an assertion if invoked: a rejection must never take the PMK
success path. It does not invent an accepted join generation in the extracted
MVM owner and does not stub the failing retirement logic into success.

Both families now compile and execute on Linux and macOS. All four executions
reach the original required assertion and exit 134:

```text
actual MVM SAE peer rejection: phase=2 generic_scan=1 owned_cleanup=0 producer_ack=0 destroyed=1 published=0
```

The accepted fresh attempt remains AUTH instead of entering FAILING and
retiring its owned producer, lower hardware and SAE resources. The test is
intentionally red and remains separate from the passing aggregate. The
test-harness correction is not a driver fix or a closed functional layer.

The next implementation must follow the existing
[failed-join ownership requirements](TAHOE_JOIN_CLEANUP_CAPABILITY_20260911.md)
and the recovered
[reference lifecycle](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md).
Do not enroll `ic_wcl_join_failure_scan` until real asynchronous retirement
participants exist; do not substitute `cleanup_done(ALL)` or a synthetic join
generation. Fresh JoinAdapter attempts and accepted roaming remain distinct.

## Qualification order after RFKILL is resolved

First qualify IWM scanning and initial open/WPA2/WPA3 association with DHCP
and bidirectional traffic. Then prioritize repeated GUI selections among
saved networks, rejection-to-valid-network recovery, off/on and sleep/wake.
Keep failed first attempts in the result; a later toggle is not a pass for
automatic recovery. AP open/WPA2/WPA3, external-client DHCP/traffic and AP
sleep/wake remain separate runtime requirements. The September inventory
banner about obsolete association-path theories does not close these tests.
