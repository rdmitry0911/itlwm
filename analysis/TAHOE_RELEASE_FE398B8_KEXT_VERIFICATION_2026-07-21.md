# Tahoe release fe398b8 kext verification — 2026-07-21

## Successful, bounded release-archive scenario

CD run `29787781829` successfully published prerelease
`v2.4.0-alpha-fe398b8` for exact commit
`fe398b8404da03013b689ac5ffb53ff194445a86`.

The release asset
`AirportItlwm-Tahoe-v2.4.0-DEBUG-alpha-fe398b8.zip` was downloaded and
checked with `unzip -t`.  Its SHA-256 is
`28d8cc4487a16a3bc7bc05002241e7fb4feb95207beb42e195b305a976d4ab08`, and it
contains both `AirportItlwm.kext/Contents/Info.plist` and
`AirportItlwm.kext/Contents/MacOS/AirportItlwm`.  The release therefore exposes
a real distributable kext bundle, not a loose executable or a source-only
artifact.

## Explicit limits

This check verifies release availability and archive structure only.  The
asset was not installed, loaded, activated, or rebooted; no association,
traffic, or other functional Wi-Fi scenario was run.  It makes no functional
Wi-Fi claim.
