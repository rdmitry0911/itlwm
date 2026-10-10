# Bounded release and power/SAE audit

The requested current driver is published. The bounded work then covers
bootstrap POWER, the IWX terminal-owner audit, native SAE callback admission
and the final regression/build checks. Work stops at this checkpoint; it is
not a declaration of full driver equivalence.

## Published driver

Release: [v2.4.0-alpha](https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha).
Additional LAB ONLY artifact:
[AirportItlwm-Tahoe-IwxNicInitAdmission-0ba9dc59.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-IwxNicInitAdmission-0ba9dc59.kext.zip).

Asset ID `628393762`, size `15719267`, archive SHA256
`b309b57c8b4f25a7c21b3b35dbae88ed2e17a5fa0637b15942f71dfafa8a3d4d`.
An independent release download matches the saved archive. All 19 previous
assets, including default `568074766`, are preserved, as is the complete
previous release-note suffix. The additional artifact is not promoted as an
on-air-qualified replacement for the default asset.

The production source is `0ba9dc59`; the bounded commits change tests and
documentation only. Source identity remains `e64157f36cc3`.

## Completed software work

`1ec62909` executes the complete production bootstrap/get/set POWER path in
the IWM/IWX admission fixture. Cached requests, boot failure, cached Off and
the ordinary fresh-RFKILL transition are covered. The reference confirms
bootstrap caching and later consumption, but a full hardware bootstrap
equivalence claim is still unwarranted. See
[the bootstrap audit](TAHOE_BOOTSTRAP_POWER_AUDIT_20261010.md).

`9296c88d` adds complete IWX terminal dispatcher, task lease, controller
mailbox and native callback CAS admission bodies to an executable fixture.
Each run covers 160 controlled races plus routing/retirement cases. The
historical complete dispatcher `d2d8f1d8^` fails the early-reclaim assertion
with exit 134 on Linux and macOS. The two obsolete source contracts now
enforce the correct retained-owner ordering, invoke that executable fixture
and run in the complete payload aggregate. See
[the terminal lifetime audit](TAHOE_IWX_SAE_TERMINAL_LIFETIME_20261010.md).

`5f351162` supplies the macOS host-test scrub adapter. The first macOS
negative attempt failed to compile because host libc lacks the kernel's
`explicit_bzero`; that log is preserved and is not counted as a behavioral
negative result. The corrected historical body compiles and fails at the
intended lifetime assertion. No production callback is moved or weakened.

The complete Linux payload aggregate passes at `9296c88d`; the final
Apple-only adapter also passes its Linux target fixture. The complete
macOS aggregate passes at clean detached `5f351162`, including IWN/IWM/IWX
software paths and both formerly red IWX contracts. These are host fixtures,
not hardware execution of all three families.

## Build and actual loaded image

The ordinary Tahoe target builds at clean detached `5f351162` in
`/Users/devops/Projects/itlwm-iwm9260-20261009`. All 1088 undefined symbols
resolve against the explicitly supplied guest BootKC. Its binary matches
the published and loaded production image exactly:

- UUID: `1A4E2AAD-09ED-3D4C-A105-E258B0B09EDC`.
- Mach-O SHA256: `4f8307436a6a0990ff2151258ea2b54d9f6b214a8c679e13db73af8c0a50d3b2`.

A root-authorized whole-bundle comparison also confirms built and installed
bundles are equal. An earlier unprivileged comparison printed permission
denied and a misleading completion marker; only the independent root
comparison is accepted. No reinstall or reboot is needed for unchanged
production bytes. The original dirty guest checkout remains untouched.

## Runtime and recovery boundary

The retained overlay runs under the exact owned unit
`aiam-iwm-9260-runtime-20261009`, PID `35176`, with guest boot
`A4F40186-4298-4E1E-83F5-62BAEEF55103`. The first cold On triggers bootstrap:
the getter reports logical On, but the actual lower RFKILL check reports
blocked. The initial script expecting an ordinary refusal stops at that
getter assertion; it is not counted as a passed power sequence.

After bootstrap settles, four ordinary native Off/On cycles pass with
readback Off and independent management on en2. All four traced On requests
return `0xe00002d8` in 1.826–1.917 ms. The real bootstrap On-to-Off drain
takes 2.497 seconds in this run, including two failed NIC-access waits of
1.240 and 1.243 seconds; the task-owner drain itself takes 2.881 microseconds.
This slow hardware-stop path is not shortened or reported as fixed.

One actual `pmset sleepnow` is issued at 16:33:30 UTC. Two independent
private-monitor observations show `paused (suspended)` and the serial log
contains `ACPI SLEEP`. One private `system_wakeup` resumes the same guest.
Power history records sleep at 16:34:00, wake at 16:34:25, and WakeTime
1.288 seconds. These power-history times are not the entire request-to-SSH
interval. The first immediate SSH retry times out during banner exchange;
the next succeeds with the same boot/image and en2 route.

Four post-wake native cycles pass, but the first observer's fixed window
ends before all four controls finish. A separate 45-second observer and a
new four-cycle post-wake sequence qualify the complete post-wake set.
All four final traced requests return NotReady in 1.791–1.916 ms with actual
RFKILL blocked. Both observers finish with TRACE_COMPLETE and terminal
exit 0. Wi-Fi ends Off; the management route remains en2. The observer is read-only
and does not manufacture a ready event, ACK or DMA-idle receipt.

Raw guest logs are under `/private/var/tmp/aiam-bounded-20261010.yjbLae`;
host copies are under
`scratch/iwm-9260-runtime-20261009.mo5CXe/bounded-final-macos.Dx6f9X/aiam-bounded-20261010.yjbLae`.
The final release-note update at 16:38:26 UTC is independently verified:
all 20 current asset metadata records are unchanged and all preceding notes
are an exact suffix. Physical host
`10.90.10.22`, the source P620 and other QEMU instances are not changed.

## Remaining functional surface

Hardware RFKILL still prevents physical IWM radio service. Physical IWX
execution and GUI combinations of repeated open/WPA2/WPA3 joins, saved
networks, reconnect/roam, DHCP/traffic, AP and working service after sleep
remain unqualified. This run closes verification gaps and publishes the
current image; it does not add a new user-facing feature or prove WPA3/AP
operation with a blocked radio.
