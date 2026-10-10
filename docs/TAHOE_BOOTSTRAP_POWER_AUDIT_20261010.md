# Bootstrap POWER is not a radio-ready receipt

The bounded October 10 audit extends executable coverage of the existing
IWM/IWX power path. It does not change production power behavior or claim a
successful connection with the hardware-blocked 9260.

## Reference evidence

The existing 25C56 range listing `aiam_setPOWER_range_listing_25C56_20260729.txt`
on `10.7.6.112:/home/dima/Projects/ghidra_output` shows the bootstrap producer
storing the requested value at `+0x289c` and setting the pending `0x1000` bit
at `ffffff80015e0b86..ffffff80015e0b98`. The ordinary setter separately compares
the current logical state and skips an identical request at
`ffffff80015e0ba5..ffffff80015e0bb2`.

The available earlier BootKC setupDriver assembly at
`cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/00551_ffffff8001599d7a___ZN16AppleBCMWLANCore11setupDriverEv.asm.txt`
tests the pending bit, then loads `+0x289c` and calls handlePowerStateChange
at `ffffff8001599ed0..ffffff8001599ed9`. Its associated prepared C export is
marked `DECOMPILE_NOT_COMPLETED`; it is not used as a recovered function body.
These two exports come from different BootKC address layouts and are not
silently treated as one binary.

The exact transition export `aiam_power_lifecycle_exact2_25C56_20260711.c`
preserves the old logical state on a failed real power transition. Its
powerOn decompilation is truncated; the separate full powerOn assembly
listing remains the evidence for the real initialization tail. None of this
establishes that the local cold `On` getter is a hardware-ready receipt.

## Executable coverage

`test_mvm_radio_power_admission.sh` now additionally extracts the complete
production `performTahoeBootChipImage`, `setPOWER` and `getPOWER` bodies.
It retains the full CSR reader, HAL admission, enableAdapter and ordinary
POWER transition bodies already covered by the fixture. IOKit operations,
firmware enable and readiness waits remain explicit test boundaries.

The new cases cover an early cached Off followed by On, deferred bootstrap
execution, consumption of the last cached value, cached Off after an On
trigger, a re-entrant ordinary setter after the bootstrap window closes,
lower enable failure, all four getter values and null arguments. They also
prove that BootReady is distinct from an owned radio-ready epoch: accepted
bootstrap enable neither arms nor completes a public power-on wait. A later
Off/On uses fresh RFKILL admission and preserves Off when blocked.

Both IWM and IWX fixtures pass on Linux under ASan/UBSan. The cold logical
On/radio-inactive distinction is now reproduced by complete local bodies,
not merely an enableAdapter double. It remains a qualification limitation,
not a newly closed on-air discrepancy. No arbitrary second initialization,
fake readiness or RFKILL override is introduced.

## Remaining boundary

End-to-end cold-start behavior still needs an unblocked adapter and the
GUI/saved-network/recovery matrix. A full reference bootstrap hardware trace
would also be needed to assert complete bootstrap equivalence. The current
9260 cannot supply that evidence while its hardware RFKILL remains asserted.
