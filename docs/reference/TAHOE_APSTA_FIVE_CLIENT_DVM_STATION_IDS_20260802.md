# Tahoe APSTA five-client and Intel DVM station-ID contract

This runtime layer follows the recovered Tahoe 25C56 reference instead of a
locally chosen client count.

- `AppleBCMWLANIO80211APSTAInterface` owns five station-table entries at
  `state+0xb8`, with stride `0x30`.
- Association and reassociation update one matching entry or the first empty
  entry. Removal clears only the matching entry.
- `setMaxAssoc` carries a 32-bit live limit and admits it only while the
  current count plus the requested count remains within the controller cap.

The matching Intel DVM resource model was checked against the upstream Linux
v4.14 driver:

- dynamic stations are allocated from `IWL_STA_ID` 2 through station 13;
- PAN broadcast owns station 14 and the full table contains 16 entries;
- firmware cannot accept two simultaneous ADD_STA requests, so client
  materialization must be serialized even though the table has ample space;
- restore walks every active station and replays ADD_STA and link quality one
  at a time.

Consequently IWM and IWX now expose five runtime client slots, and IWM owns
five dedicated DQA data queues after the existing STA aggregation range. The
remaining IWN work is not a hardware-capability workaround: it must give each
of those five Apple-visible clients its own DVM station ID and serialize the
ADD_STA completion chain.
