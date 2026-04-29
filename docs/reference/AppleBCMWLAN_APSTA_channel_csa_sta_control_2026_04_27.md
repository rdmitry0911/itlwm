# AppleBCMWLAN APSTA Channel, CSA, And STA-Control Reference

Source: `/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c` plus local
`/tmp/AppleBCMWLANCoreMac` disassembly.

## `getCHANNEL(apple80211_channel_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::getCHANNEL(...)`.
Address: `0xffffff8001687cbe`.

Recovered sequence:

- build a 0x0c-byte RX payload initialized with `0xaa`
- use command RX payload range qword `0x0000000c000c000c`
- read commander from APSTA state `+0x228`
- call `runVirtualIOCtlGet` selector `0x1d` with no TX payload
- on success, copy received channel number from RX carrier `+0x04` to output
  `+0x08`
- OR output flags at `+0x0c` with `0x08` when channel is below `0x0f`
- OR output flags at `+0x0c` with `0x10` when channel is `0x0f` or above

## `setCHANNEL(apple80211_channel_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setCHANNEL(...)`.
Address: `0xffffff800168dcfa`.

Recovered sequence:

- null input returns raw `0x16`
- channel number is read from input `+0x08`; values `>= 0x100` return raw
  `0x16`
- default bandwidth is read through `state+0x218 -> +0x128 -> +0x408`
- flags at input `+0x0c` map bandwidth as:
  `0x400 -> 4`, `0x04 -> 3`, `0x02 -> 2`
- band argument is `0` for channels below `0x0f`, otherwise `3`
- call `AppleBCMWLANCore::getChanSpec(channel & 0xff, band, &bandwidth)`
- zero chanspec returns `0xe00002c2`
- nonzero chanspec is sent as 4-byte IOVAR `chanspec`

## `setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_TRIGGER_CSA(...)`.
Address: `0xffffff800168e0ae`.

Recovered sequence:

- AP state `state+0x26c` must be nonzero
- link/reset flag `state+0x329 & 1` must be nonzero
- otherwise return `6`
- null input returns raw `0x16`
- input channel is read at `+0x04`
- mode byte is read at `+0x10`
- feature-gate byte is read at `+0x14`
- if `input+0x14` is nonzero, feature `0x46` is enabled, and core private
  byte `+0x4d59 & 1` is set, call core vtable `+0x1110` with argument `0`
- call `AppleBCMWLANCore::getChanSpec(input+0x04, &chanspec)`
- parsed chanspec values below `0x10000` are accepted
- parsed chanspec values `>= 0x10000` return raw `0x16`
- build 6-byte IOVAR `csa` payload:
  - byte `+0x00 = 0`
  - byte `+0x01 = input+0x10`
  - word `+0x02 = uint16_t(chanspec)`
  - word `+0x04 = 0`

## `setSTA_AUTHORIZE(apple80211_sta_authorize_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setSTA_AUTHORIZE(...)`.
Address: `0xffffff800168f016`.

Recovered sequence:

- null input returns `0xe00002c2`
- six-byte MAC payload is read from input `+0x08`
- input flag at `+0x04` selects virtual IOCTL:
  - value below `1` selects `0x7a`
  - value `>= 1` selects `0x79`

## `setSTA_DISASSOCIATE(apple80211_sta_disassoc_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setSTA_DISASSOCIATE(...)`.
Address: `0xffffff800168f15e`.
Concrete APSTA vtable slot: `522`, byte offset `0x1050`.

Recovered sequence:

- no local null guard is present before input reads
- build 0x0c-byte TX payload:
  - dword `+0x00 = input+0x04`
  - dword `+0x04 = input+0x08`
  - word `+0x08 = input+0x0c`
  - word `+0x0a = 0xaaaa`
- build 0x0c-byte RX payload with range qword `0x0000000c000c000c`
- call `runVirtualIOCtlSet` selector `0xc9`
- return the command result after error logging

## `setSTA_DEAUTH(apple80211_sta_disassoc_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setSTA_DEAUTH(...)`.
Address: `0xffffff800168f14c`.

Recovered sequence:

- tailcall the existing APSTA vtable entry at byte offset `+0x1040`

## Local Scope

The local scaffold records constants, vtable slots, carrier layouts, and static
asserts only. It does not enable APSTA role-7 success, send channel/CSA/STA
commands, or synthesize AP/STA state at runtime.
