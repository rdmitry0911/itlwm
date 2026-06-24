# Tahoe Lineage And Build Reproducibility

This note defines the non-runtime source/build surface used to audit the Tahoe
AirportItlwm build envelope from the canonical guest repository. It is not a
Wi-Fi runtime procedure and it does not capture final runtime evidence.

## Source Boundary

The canonical source checkout is the guest repository:

```bash
/Users/devops/Projects/itlwm
```

Source `git`, patch, build, install, and validation commands for this project
must run from that guest checkout. Host paths are control, scratch, logs, and
evidence mirrors only; they are not source checkouts. Do not clone, copy,
mirror, rsync, or move the guest `.git` directory to the host, `/tmp`, or any
other path to prove lineage or satisfy a preflight.

## Lineage Anchors

The bounded lineage audit uses only guest-local committed sources and git
metadata:

```bash
cd /Users/devops/Projects/itlwm
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
- git remote/history are read from the guest checkout only.

A direct configured upstream remote is not required by this audit. Do not fetch
from the network or substitute a host mirror to fill that gap.

## Smoke Check

Run the guest-local smoke verifier when the task is only to prove the lineage
and Tahoe command surface are present:

```bash
cd /Users/devops/Projects/itlwm
./scripts/tahoe_reproducibility_smoke.sh
```

The smoke check verifies the guest source boundary, committed lineage anchors,
Tahoe target/build-script text, staged kext path, BootKC symbol-check logic, and
the presence of this install-envelope document. It does not build, install,
reboot, load, unload, join Wi-Fi, capture runtime evidence, or claim AP/client
success.

## Tahoe Build Command

The default Tahoe build command in the guest repository is:

```bash
cd /Users/devops/Projects/itlwm
timeout 420s ./scripts/build_tahoe.sh
```

For an explicit Tahoe BootKC, pass the kernel collection path:

```bash
cd /Users/devops/Projects/itlwm
timeout 420s ./scripts/build_tahoe.sh /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc
```

The script builds the `AirportItlwm-Tahoe` target and stages the default kext at:

```bash
/Users/devops/Projects/itlwm/Build/Debug/Tahoe/AirportItlwm.kext
```

The opt-out exploration variant is separate and never overwrites the default
staged kext:

```bash
cd /Users/devops/Projects/itlwm
timeout 420s ./scripts/build_tahoe.sh --opt-out /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc
```

That variant stages:

```bash
/Users/devops/Projects/itlwm/Build/Debug/Tahoe-OptOut/AirportItlwm.kext
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

## Install And Reboot Envelope

Install and reboot are allowed only after the relevant auditor approval. Do not
unload the currently loaded driver as part of this envelope.

```bash
cd /Users/devops/Projects/itlwm
timeout 120s sudo rm -rf /Library/Extensions/AirportItlwm.kext
timeout 120s sudo cp -R Build/Debug/Tahoe/AirportItlwm.kext /Library/Extensions/AirportItlwm.kext
timeout 120s sudo chown -R root:wheel /Library/Extensions/AirportItlwm.kext
timeout 120s sudo chmod -R go-w /Library/Extensions/AirportItlwm.kext
timeout 120s sudo shutdown -r now || true
```

After reboot, wait for SSH with a bounded loop. If SSH does not return within
120 seconds, inspect the VM screen through VNC and follow the project boot-panic
recovery rules before any further Wi-Fi-attached boot attempt.

## Non-Claims

This file and the smoke check do not claim fresh build success, install/load
success, final runtime evidence, AP/GO functional success, Wi-Fi association,
DHCP, traffic, CR479 behavior, legal provenance closure, or project completion.
