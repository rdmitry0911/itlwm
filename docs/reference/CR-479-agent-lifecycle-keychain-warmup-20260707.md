# CR-479 AirportItlwmAgent lifecycle and keychain warmup

## Trigger

The 2026-07-07 Tahoe APSTA simple-getter validation exposed a
non-fatal but real first-association PMK timing gap:

- `AirportItlwmAgent` was started by launchd before the `AirportItlwm`
  IOService existed, exited with `AgentOpenPLTI ... kIOReturnNotFound`,
  and was respawned by launchd about ten seconds later.
- On the first post-boot association target after respawn, the helper
  received `WaitAssociationTarget OK generation=1` immediately, but the
  first cold `SecKeychainUnlock` path took about 4.1 seconds before
  `DeliverPMK OK generation=1`.
- The kext-side pre-M1 PMK wait window is 3000 ms, so the kernel emitted
  `external_pmk_wait TIMEOUT generation=1 ... ic_psk_nonzero_bytes=0/32`
  even though association later succeeded after the helper delivered the
  PMK.

No credential bytes, PMK bytes, PTK bytes, or passphrases are logged or
required for this diagnosis. The evidence is process lifecycle timing,
Security framework operation timing, and structural generation markers.

## Closure

`AirportItlwmAgent` is a long-running producer, matching the reference
shape of a resident system Wi-Fi helper instead of a short process whose
readiness depends on launchd throttle timing:

- The daemon calls `AgentPrimeProjectKeychain()` at startup. This opens
  and unlocks `/Library/Keychains/AirportItlwm.keychain` using the
  existing root-only `/etc/airportitlwm/keychain-password`, then releases
  the keychain reference without reading any SSID credential.
- The daemon retries `IOServiceOpen('PLTI')` in-process until the kext
  publishes `AirportItlwm`. Expected pre-kext failures use a quiet open
  path plus rate-limited lifecycle logging.
- `WaitAssociationTarget` aborts no longer terminate the process. The
  daemon closes the stale connection and reopens PLTI in the same
  process, keeping the keychain/securityd path warm.
- `AgentLookupProjectPSK()` still opens and unlocks defensively on every
  association edge, so keychain auto-lock remains safe and no credential
  cache is introduced.

Live reinstall validation after the change showed:

- `AgentPrimeProjectKeychain OK` at startup.
- Immediate `PLTI user client opened`.
- Pending target handling from `WaitAssociationTarget OK generation=3`
  to `DeliverPMK OK generation=3` in about 4 ms after the warmup.

