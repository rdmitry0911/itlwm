# Tahoe IWN five-client AP runtime

This layer replaces IWN's former single-peer AP state with the five independent
station entries exposed by the Tahoe 25C56 APSTA reference.

## Reference evidence

The Tahoe reference was checked from the raw disassembly retained on
`10.7.6.112`:

- `~/Projects/ghidra_output/aiam_apsta_update_assoc_25C56_20260710.range.tsv`
- `~/Projects/ghidra_output/aiam_apsta_removal_25C56_20260710.range.tsv`

`updateSTAAssocInfo` searches five MAC entries at stride `0x30`, updates the
matching entry, or claims the first empty entry. The removal path searches the
same five entries and clears only the matching one.

The matching Intel firmware ownership was checked against Linux v4.14 DVM
`drivers/net/wireless/intel/iwlwifi/dvm/sta.c`:

- dynamic station IDs begin at 2;
- an unassociated RXON clears firmware-active station ownership without
  destroying the driver's logical station records;
- `iwl_restore_stations` walks every driver-active station and replays ADD_STA
  and link quality in station-table order;
- station additions are serialized.

## IWN runtime contract

IWN now owns five `IwnApClientRuntime` slots. Each slot has an independent MAC,
DVM station ID (2 through 6), AID (1 through 5), authentication and association
state, PMK/PTK/SAE/PMKSA material, replay and packet numbers, software CCMP
context, RX/TX BA state, aggregate queue ownership, power-save queue, and TIM
bit.

RX selects a slot by source MAC, unicast TX selects by destination MAC,
ADD_STA completion selects by firmware station ID, and aggregate completion
selects by queue ownership. `apClientContext` is only the selected operation
context; it is not a global peer owner.

ADD_STA and initial link-quality materialization remain serialized. A new
Open or SAE authentication retires only that peer's firmware station, BA,
power-save, PTK, replay, and CCMP state. Each bounded SAE PMKSA entry survives
peer disconnect and a radio reset/sleep replay of the same BSSID, matching the
former single-peer behavior. A cached but inactive entry does not consume AP
admission capacity: allocation moves the matching PMK/PMKID into a live slot,
or evicts an inactive cache entry when every slot inside the configured limit
is cache-bearing. A profile/BSSID change, fresh SAE Commit, explicit AP stop,
or driver teardown scrubs the affected cache material.

Asynchronous ADD_STA completion is routed by the station ID embedded in its
command descriptor. LINK_QUALITY is routed the same way from the ID in its
own completed command body; selecting a merely pending peer would confuse an
existing client's BA-driven rate-table update with a new client's association
fence.

CSA follows DVM's two-table restore rule: the channel RXON transition retains
every logically associated/authorized slot, then restores IDs 2 through 6 one
at a time. The shared GTK is replayed once and each protected station receives
its own PTK replay.

## Verification

- Tahoe OptOut build succeeded in the disposable 25C56 guest.
- The selected AP/SAE regression suite passed, including IWN materialization,
  five-client ownership, open/WPA2/WPA3, PMF, reassociation, power save,
  RX/TX A-MPDU, CSA, APSTA sleep replay, and Skywalk backpressure.

The available laboratory radio currently supplies one independent on-air
client. It can validate regression of the existing AP path, but simultaneous
multi-client WPA2/WPA3 operation still requires a second independent station;
that result is not claimed here.
