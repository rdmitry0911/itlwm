# Tahoe 25C56 SAE lower-TX contract and IWM completion owner

## Reference facts

The exact Tahoe 25C56 BootKC material on `10.7.6.112` was consulted before
implementing this layer. The relevant recovered artifacts are:

- `itlwm_full_sta_parity_decomp_20260516T111939/07_xrefs/BootKC_full_STA/`
  `1004_0xffffff80015d9cea_...checkForWPA3SAESupportEv.asm.txt`;
- the matching `1481_0xffffff800163497e_...isSaeH2eEnabledEv.asm.txt`;
- `aiam_wifi_surface_25C56_20260711/03_decomp/`
  `25C56_wifi_kernel_surface/all_decompiled.c`, function
  `0xffffff800161ec52`.

`AppleBCMWLANCore::checkForWPA3SAESupport()` is a lower-driver feature query:
it tail-calls the feature-flag owner with selector `0x41`. The H2E predicate
also asks the lower object and admits only the recovered device values
`0x1124` and `0x112f`. This is not an airportd-only credential or crypto
decision.

The exact error taxonomy at `0xffffff800161ec52` separately identifies SAE
Commit/Confirm/PMK failures and maps result `5` to the protocol failure for a
packet which was not acknowledged. Consequently, a local frame builder or a
successful queue insertion is not a valid SAE TX success boundary. The lower
driver must retain the current identity until the device reports the native
TX result, and a reset must terminate that identity as failure.

## IWM implementation

IWM now implements the same one-ticket Algorithm-3 transport contract already
used by IWN/IWX:

1. `submitSaeAuthFrame()` copies and validates the fixed public request under
   a private IWM workloop gate. It requires the current `S_AUTH` node, exact
   BSSID/STA, and current association epoch.
2. `iwm_tx()` verifies the complete pre-trim Algorithm-3 frame, retains only
   credential-free ticket/epoch/address fields in `iwm_tx_data`, and publishes
   scheduler ownership plus the MMIO doorbell under the cancellation fence.
3. Only `iwm_rx_tx_cmd_single()` can report normal success. Missed-completion,
   BA/flush, ring reset, and free paths report failure.
4. Stop/sleep/reset snapshots a doorbelled request before descriptor reclaim,
   purges old ownership, and emits the bounded reset terminal from deferred
   task context. Detach closes admission, barriers that task, drains copied
   gate leases, and only then removes the event source and frees the rings.
5. Low controller tickets and the high-bit driver ticket domain have separate
   monotonic cancellation fences. This prepares the transport for the shared
   driver-resident credential engine without poisoning the legacy relay.

The contract is checked by
`scripts/test_tahoe_iwm_sae_auth_transport_contract.sh`. Clean Tahoe default,
AP opt-out, and IWN software-PMF variants build successfully, and all 1074
undefined symbols resolve against the exact running 25C56 BootKC.

The resulting lab binary has UUID
`76CF0827-0226-32FE-8627-87CAFCB0B98D` and SHA-256
`7177a5bb444e84d89f3a7dd19837fd472f1b082fcd9dbf5e53b68a33e419fb24`.
It passed private AuxKC admission, booted in the disposable 25C56 guest, and
remained loaded across a real S3 interval. The available passthrough device is
IWN rather than IWM, so this is intentionally a whole-kext regression result:
the guest completed pure WPA3 Personal on channel 13, 5/5 source-bound ICMP
and HTTP 200 before S3, then recovered the same WPA3 data path with another
5/5 ICMP and HTTP 200 after wake. The serial interval contains
`PMRD: System Sleep`, `ACPI SLEEP`, and `PMRD: System Wake`, with no panic or
kernel trap.

## Scope

This closes the missing physical Algorithm-3 TX-completion prerequisite for
IWM. It does not by itself give IWM a driver-resident SAE password/crypto/PMK
owner, so it is not yet an on-air IWM WPA3 association claim. That next layer
must reuse the current IWN engine lifecycle and the old IWX staging work only
as a HAL integration map.

The pre-existing aggregate SAE gate currently stops later on the committed
`test_net80211_public_initial_bssid_pin_contract.sh` expectation at HEAD
`77a0e38`; the new IWM contract and all SAE tests reached before that baseline
failure pass. The two user-owned dirty laboratory source files were not
modified or staged.
