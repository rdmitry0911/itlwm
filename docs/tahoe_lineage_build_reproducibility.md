# Tahoe Lineage And Build Reproducibility

This note defines the non-runtime source/build surface used to audit the Tahoe
AirportItlwm build envelope from the current source repository. It is not a
Wi-Fi runtime procedure and it does not capture final runtime evidence.

## Source Boundary

The source checkout is the current git worktree:

```bash
cd "$(git rev-parse --show-toplevel)"
```

Source `git`, patch, build, install, and validation commands for this project
must run from that worktree. The worktree's `.git` entry must remain inside that worktree. For a standard linked worktree, that entry is a `gitdir:` pointer and
Git-managed administrative metadata normally lives under the common
`.git/worktrees` directory; that layout is valid. Host paths, logs, and
evidence mirrors are not source checkouts. Do not clone, copy, mirror, rsync,
or move the project `.git` entry or Git metadata to another path to prove
lineage or satisfy a preflight.

## Lineage Anchors

The bounded lineage audit uses only worktree-local committed sources and git
metadata:

```bash
cd "$(git rev-parse --show-toplevel)"
git remote -v
git log --oneline -1
grep -n "OpenIntelWireless/itlwm\|OpenBSD" README.md
grep -n "GNU GENERAL PUBLIC LICENSE\|Version 2" LICENSE
grep -n "com.zxystd.AirportItlwm\|AirportItlwm" AirportItlwm/Info.plist
```

The expected anchors are:

- committed README links and credits the OpenIntelWireless/itlwm lineage and
  the OpenBSD-derived driver base;
- committed LICENSE is GNU GPL version 2;
- committed `AirportItlwm/Info.plist` identifies the AirportItlwm bundle;
- git remote/history are read from the current worktree only.

A direct configured upstream remote is not required by this audit. Do not fetch
from the network or substitute a host mirror to fill that gap.

## Smoke Check

Run the worktree-local smoke verifier when the task is only to prove the lineage
and Tahoe command surface are present:

```bash
cd "$(git rev-parse --show-toplevel)"
./scripts/tahoe_reproducibility_smoke.sh
```

The smoke check verifies the source boundary, committed lineage anchors,
Tahoe target/build-script text, staged kext path, BootKC symbol-check logic, and
the presence of this install-envelope document. It does not build, install,
reboot, load, unload, join Wi-Fi, capture runtime evidence, or claim AP/client
success.

## Historical Report Check

The deterministic report check is:

```bash
cd "$(git rev-parse --show-toplevel)"
timeout 30s python3 scripts/tahoe_lineage_build_report.py --check evidence/build/tahoe_lineage_build_report.json
```

The tracked JSON is a historical source/build snapshot, not a current-candidate
or activation gate. Its `--check` mode deliberately accepts only the report's
recorded HEAD when it is the current commit or its direct parent; it therefore
fails after unrelated later source commits. Do not regenerate or use that
historical report to decide private AuxKC materialization, installation, or
next-boot activation. Use the smoke check for current source presence and the
separate private preflight for the candidate's exact AuxKC boundary.

## Tahoe Build Command

The default Tahoe build command in the current git worktree is:

```bash
cd "$(git rev-parse --show-toplevel)"
timeout 420s ./scripts/build_tahoe.sh
```

For an explicit Tahoe BootKC, pass the kernel collection path:

```bash
cd "$(git rev-parse --show-toplevel)"
timeout 420s ./scripts/build_tahoe.sh /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc
```

The script builds the `AirportItlwm-Tahoe` target and stages the default kext at:

```bash
Build/Debug/Tahoe/AirportItlwm.kext
```

The opt-out exploration variant is separate and never overwrites the default
staged kext:

```bash
cd "$(git rev-parse --show-toplevel)"
timeout 420s ./scripts/build_tahoe.sh --opt-out /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc
```

That variant stages:

```bash
Build/Debug/Tahoe-OptOut/AirportItlwm.kext
```

## BootKC Symbol Gate

`scripts/build_tahoe.sh` compares undefined symbols from the staged
AirportItlwm binary against exports from the selected BootKC with `nm` and
`comm`. A build is install-ready only when the build output contains the success
line:

```text
OK: all <count> undefined symbols resolve against BootKC
```

If the script prints `WARNING: BootKC not found`, the build may have succeeded
but the symbol gate did not run. Do not install that kext as a Tahoe-approved
artifact until the same source has passed the BootKC symbol check.

## Private AuxKC Admission Preflight

Before an activation decision, test the candidate's AuxKC link/materialization
boundary with the project-owned private-only helper:

```bash
scripts/tahoe_auxkc_admission_preflight.sh \
  --candidate /private/tmp/aiam-candidate/AirportItlwm.kext \
  --out /private/tmp/aiam-auxkc-preflight-evidence
```

The candidate and temporary collection must physically resolve beneath `/private`.
The canonical AirportItlwm bundle and canonical AuxKC remain read-only throughout this check.
`--out` must name a non-existent private directory; the helper creates it once
and refuses a reusable directory or symlink, so stale evidence children cannot
redirect a new run. Once it has captured the canonical before-witnesses, its
exit path always records and verifies the canonical after-witnesses, including
after a failed `kmutil create` or private inspection. A PASS therefore requires
both successful private materialization and a complete unchanged canonical
postflight.
The helper uses the successful 25C56 explicit-five-member form: the private
candidate plus HighPointRR, HighPointIOP, RemoteVirtualInterface, and
AppleMobileDevice are passed separately with `--explicit-only`. It compares
the canonical bundle SHA-256, canonical AuxKC SHA-256, and exact five-member
inventory before and after creation. The raw `kmutil inspect` member rows are
kept as evidence; each raw multiset must contain exactly one of every required
identity and no malformed or unknown row before its complete rows are
canonically sorted for comparison. This deliberately treats a display-order
permutation from `kmutil inspect` as unchanged while still detecting a
version, path, UUID, duplicate, missing, or unknown-member difference.
The accepted Tahoe 25C56 row schema is exact: every required row has four
tab-separated fields for identity, version, absolute bundle path, and UUID
position. The four UUID-bearing members require a parenthesized UUID; the
observed `AppleMobileDevice` row requires that same fourth field to be empty.
Extra fields and empty identity, version, or path values fail before sorting.

The helper does not call `--no-authorization`: it uses the retained 25C56
five-path private materialization form with normal `kmutil create` checks. Its
PASS proves only that the exact five-bundle private collection
linked/materialized; it does not prove a live load, next-boot activation, or
runtime behavior. Do not replace that private check by changing
`/Library/Extensions/AirportItlwm.kext`: such a change can trigger
`kernelmanagerd` collection work even without an explicit load command.

## Full AuxKC Install And Reboot Envelope

Install and reboot are allowed only after the relevant auditor approval. Do not
unload the currently loaded driver or direct-load it. `kmutil install --update-all`
is not a substitute on this Tahoe guest: its release collection volume is
read-only. The guest also does not provide GNU `timeout`; bound this transaction
from its caller rather than placing an unavailable wrapper inside it.

Activation is separate from the private preflight and is allowed only after a
passing private-materialization result plus a separate approval. It deliberately
changes the installed bundle and can wake `kernelmanagerd`; it is therefore not
a harmless reversible test. The collection must be rebuilt from **both**
extension repositories. Do not use an explicit one-bundle `--bundle-path` collection: it can silently omit the other installed auxiliary kexts. Before any replacement, save the current AirportItlwm bundle and verify the current AuxKC has exactly these five members:

```text
com.zxystd.AirportItlwm
com.apple.nke.rvi
com.apple.driver.AppleMobileDevice
com.highpoint-tech.kext.HighPointIOP
com.highpoint-tech.kext.HighPointRR
```

The following is an approved activation transaction, not a preflight. The
`kmutil create` output is a uniquely named temporary collection. Do not swap it
until its inspection succeeds.

```bash
cd "$(git rev-parse --show-toplevel)"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
new_auxkc="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.new-${stamp}"
bundle_backup="/Library/Extensions/AirportItlwm.kext.preinstall-bak.${stamp}"
auxkc_backup="/Library/KernelCollections/AuxiliaryKernelExtensions.kc.preinstall-bak.${stamp}"

sudo ditto /Library/Extensions/AirportItlwm.kext "$bundle_backup"
sudo rm -rf /Library/Extensions/AirportItlwm.kext
sudo ditto Build/Debug/Tahoe/AirportItlwm.kext /Library/Extensions/AirportItlwm.kext
sudo chown -R root:wheel /Library/Extensions/AirportItlwm.kext
sudo chmod -R go-w /Library/Extensions/AirportItlwm.kext

sudo kmutil create -n aux --arch x86_64 \
  --elide-identifier com.apple.driver.AppleSunrise \
  -k /System/Library/Kernels/kernel \
  -B /System/Library/KernelCollections/BootKernelExtensions.kc \
  -S /System/Library/KernelCollections/SystemKernelExtensions.kc \
  -A "$new_auxkc" \
  -r /Library/Extensions \
  -r /Library/Apple/System/Library/Extensions \
  --force

sudo kmutil inspect --show-kext-uuids -A "$new_auxkc"
```

The inspection must show the candidate AirportItlwm UUID and exactly five
members. The four non-AirportItlwm member lines must match the pre-build
inspection; failure means restore the backed-up bundle and stop. Do not try an
alternate collection shape, load the candidate, or reboot.

Only after that passing inspection, copy the canonical AuxKC to the unique
`$auxkc_backup`, atomically move `$new_auxkc` into canonical location, restore
`root:wheel` ownership and `0644` mode, then reboot:

```bash
sudo ditto /Library/KernelCollections/AuxiliaryKernelExtensions.kc "$auxkc_backup"
sudo mv -f "$new_auxkc" /Library/KernelCollections/AuxiliaryKernelExtensions.kc
sudo chown root:wheel /Library/KernelCollections/AuxiliaryKernelExtensions.kc
sudo chmod 0644 /Library/KernelCollections/AuxiliaryKernelExtensions.kc
sudo /sbin/reboot
```

After reboot, wait for SSH with a bounded caller-side loop. If SSH does not
return within 120 seconds, inspect the VM screen through VNC and follow the
project boot-panic recovery rules before any further Wi-Fi-attached boot
attempt. Then verify `kmutil showloaded | grep -i AirportItlwm`, the loaded
UUID, and binary SHA-256 against the just-built candidate. The backups are the
only permitted rollback source; never live-unload the driver.

## Non-Claims

This file and the smoke check do not claim fresh build success, install/load
success, final runtime evidence, AP/GO functional success, Wi-Fi association,
DHCP, traffic, CR479 behavior, legal provenance closure, or project completion.
