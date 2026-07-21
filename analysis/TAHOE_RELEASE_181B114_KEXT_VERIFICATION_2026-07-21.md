# Tahoe release 181b114 kext verification — 2026-07-21

## Successful, bounded release-archive scenario

CD run `29790038703` successfully published prerelease
`v2.4.0-alpha-181b114` for exact commit
`181b114314cd6db4d1da9223363bfa4e4454e58a`.

The release asset
`AirportItlwm-Tahoe-v2.4.0-DEBUG-alpha-181b114.zip` was downloaded and
checked with `unzip -t`. Its SHA-256 is
`a1be31cbd228a3ddff9f3b37a093e4e9ccbedaa85cc8f374828af7b8dfde41b3`, and it
contains both `AirportItlwm.kext/Contents/Info.plist` and
`AirportItlwm.kext/Contents/MacOS/AirportItlwm`. The release therefore exposes
a real distributable kext bundle, not a loose executable or source-only
artifact.

## Explicit limits

This check verifies release availability and archive structure only. Archive
verification itself did not install, load, activate, or reboot a kext and made
no association or traffic claim. The separately recorded, overlay-scoped
activation/runtime scenario is documented in
`analysis/TAHOE_LAB_181B114_OVERLAY_RUNTIME_2026-07-21.md`.
