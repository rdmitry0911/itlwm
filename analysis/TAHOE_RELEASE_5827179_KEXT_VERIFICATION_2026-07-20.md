# Tahoe release 5827179 kext verification — 2026-07-20

Release `v2.4.0-alpha-5827179` was produced by the successful CD run
`29786593288`.  The release asset
`AirportItlwm-Tahoe-v2.4.0-DEBUG-alpha-5827179.zip` was downloaded and
inspected.  It contains both
`AirportItlwm.kext/Contents/Info.plist` and
`AirportItlwm.kext/Contents/MacOS/AirportItlwm`; it is a distributable kext,
not a loose driver binary.

This check only verifies release availability and archive structure.  The
asset was not installed, loaded, or activated; no host was rebooted and no
association or traffic scenario was run.  It makes no functional Wi-Fi claim.
