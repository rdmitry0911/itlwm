# AppleBCMWLAN APSTA Event And Station Table Reference

Source: `/tmp/AppleBCMWLANCoreMac`, Tahoe symbols.

This note records APSTA producer-side station table contracts. It is a local
reference note only; it does not authorize runtime APSTA enablement.

## `handleEvent(wl_event_msg_t *)`

Function: `AppleBCMWLANIO80211APSTAInterface::handleEvent(...)`

Address: `0xffffff800168faa0`

Recovered event header operands:

- event type: `event+0x04`
- status: `event+0x08`
- reason: `event+0x0c`
- auth type: `event+0x10`
- data length: `event+0x14`
- station address: `event+0x18`, 6 bytes
- event data: `event+0x30`

Recovered direct event branches:

- `4`: `WLC_E_AUTH_IND`, emits STA message id `0x98` with payload size `0x6c`
- `5`, `6`, `11`, `12`: STA removal path
- `8`: `WLC_E_ASSOC_IND`, successful association path
- `10`: `WLC_E_REASSOC_IND`, successful reassociation path
- `16`: link-change path
- `0x36`: interface-index event path
- `0x4b`: action-frame path
- `0x96`: PSK auth / GTK rekey event path

Association/reassociation success requires status `0` and reason `0`. The path
copies the event MAC to APSTA state metadata at `state+0x80/+0x84`, calls
`updateSTAAssocInfo(event)`, resets/schedules the AP stats timer through
`state+0x70`, parses RSNXE from event data into the outgoing message, and posts
STA message id `0x0c` with payload size `0x114`.

The association message stores:

- `+0x00`: first four MAC bytes
- `+0x04`: last two MAC bytes
- `+0x08`: current APSTA associated-station count
- `+0x0c`: association flags
- `+0x10`: RSNXE output area

Association flags are derived from the station table and Apple vendor IE:

- bit `0x01`: station table entry `+0x20` is nonzero
- bit `0x02`: station table entry `+0x24` is nonzero
- bit `0x04`: Apple IE was present in the association data; APSTA also writes
  station table entry `+0x28 = 1`

STA removal events decrement `state+0x00` when nonzero, notify every APSTA TX
subqueue via vtable `+0x358` with the entry MAC pointer, clear the station-table
entry, and post STA message id `0x0d` with payload size `0x0c`.

## Station Table

The station table occupies `state+0xb8` through `state+0x1a7`: five entries,
stride `0x30`.

Per-entry layout:

- `+0x00`: active byte
- `+0x01`: six-byte MAC
- `+0x10`: sleep state; add path initializes it to `2`
- `+0x20`: AIHS flag
- `+0x24`: sharing flag
- `+0x28`: Apple-station flag

`removeStaFromStaTable(index)` zeroes six qwords at entry offsets
`+0x00/+0x08/+0x10/+0x18/+0x20/+0x28`; invalid indexes `>= 5` return
`0xe00002bc`.

## `postMessageForSTA(unsigned int, void *, unsigned short)`

Function address: `0xffffff8001691bb8`

The helper allocates a copy of the payload, dispatches it through APSTA vtable
`+0xb18`, then notifies the core owner at `state+0x218 -> +0x128 -> +0x2c20`
with flag `1`. The temporary payload is freed with the original size.

## `checkForAppleIE(unsigned char *, unsigned int)`

Function address: `0xffffff8001691cc6`

The helper scans an IE list only when at least six bytes remain. It recognizes
vendor-specific element id `0xdd`, compares three-byte OUIs at element `+0x02`
against the Apple, Apple BS, and Apple device-info OUIs, and advances by
`ie[1] + 2` otherwise.

## `updateSTAAssocInfo(wl_event_msg_t *)`

Function address: `0xffffff8001691d6a`

The helper first searches existing station MACs at `state+0xb9..state+0x1a9`
with stride `0x30`. If no entry matches, it finds an all-zero MAC slot, writes
the event MAC to entry `+0x01`, writes active byte `1`, writes sleep state `2`,
and increments `state+0x00`.

It then scans association IEs for Apple instant-hotspot subtype `0x0b`. When
present, the byte at IE `+0x09` supplies station flags:

- bit `0`: written to entry `+0x20`
- bit `1`: written to entry `+0x24`

## `parseRSNXE(unsigned char *, unsigned int, unsigned char *)`

Function address: `0xffffff800169217e`

The helper returns immediately for null input or fewer than two bytes. It scans
IEs by `ie[1] + 2`, searches for element id `0xf4`, and copies the full RSNXE
IE, including id and length bytes, to the supplied output.

## `checkForStationListMismatch(apple80211_sta_data *)`

Function address: `0xffffff800169229a`

If firmware count at input `+0x04` is greater than `5` or equals host count
`state+0x00`, the helper returns. Otherwise, it compares each active host entry
against firmware MAC entries at input `+0x0c` with stride `0x10`.

Missing host entries are posted as removal message id `0x0d`, notified to each
APSTA TX subqueue via vtable `+0x358`, cleared by `removeStaFromStaTable(index)`,
and final host count is set to the firmware count.
