# AppleBCMWLAN APSTA HostAP Max-Assoc And Vendor IE Reference

Source: `/tmp/itlwm-re/AppleBCMWLANCoreMac` disassembly and
`/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c`.

## `setHostApModeInternal(...)` pre-success-tail

Range: `0xffffff800168c0bf..0xffffff800168c138`.

Recovered sequence:

- read max-assoc from `state+0x218 -> +0x128 -> +0x1558 -> +0x10 -> +0xb4`
- store that value at `state+0x8`
- call `AppleBCMWLANIO80211APSTAInterface::setMaxAssoc(unsigned int)`
- call APSTA vtable `+0xb18` with selector `0x57`, payload pointer
  `state+0x8`, payload size `4`, and final argument `0`
- read vendor IE length from `network_data+0x2dc`
- if nonzero, call `programVendorIEList(network_data+0x2e0, length)`
- otherwise call `programAppleVendorIE()`
- continue into the AP-up success tail at `0xffffff800168c138`

## `setMaxAssoc(unsigned int)`

Range: `0xffffff800168c6ac..0xffffff800168c7b5`.

Recovered sequence:

- use APSTA state block at object `+0x130`
- if requested max-assoc equals `state+0x4`, return
- compute firmware payload as `state+0x0 + requested`
- if computed payload is greater than `state+0x8`, return
- write requested max-assoc to `state+0x4`
- send virtual IOVAR `maxassoc` through commander `state+0x228`
- TX payload is 4 bytes and points at the computed payload value

## `programVendorIEList(unsigned char*, unsigned int)`

Range: `0xffffff800168c7ba..0xffffff800168c9da`.

Recovered sequence:

- return without work when remaining length is less than `6`
- read IE element id from input `+0x0`
- read IE body length from input `+0x1`
- reject when remaining bytes minus 2 are smaller than the IE body length
- allocate `0x814` bytes for an `apple80211_ie_data` carrier
- write carrier qword `+0x00 = 0x1a00000001`
- write carrier qword `+0x08 = 0x400000001`
- write carrier byte `+0x14 = input[0]`
- copy `input+0x2` body bytes to carrier `+0x15`
- write carrier dword `+0x10 = body_length + 1`
- call `AppleBCMWLANCore::setVendorIE(interfaceId, carrier)`
- free the `0x814` byte carrier
- advance by `body_length + 2` and continue while more than 5 bytes remain

## `programAppleVendorIE()`

Range: `0xffffff800168c9e0..0xffffff800168d30d`.

Recovered sequence:

- use IOVAR name `vndr_ie`
- query commander max TX payload and max RX payload
- use the smaller payload limit, minus `strlen("vndr_ie") + 1`, as the RX
  buffer capacity for the `vndr_ie` GET response
- allocate an `0x52` byte set buffer and the computed RX buffer
- run virtual IOVAR GET `vndr_ie`
- iterate returned vendor IEs and delete entries whose OUI matches
  `kAPPLE_IE_OUI` length `3`
- delete command writes `"del"` into the set buffer, writes dword `1` at
  `+0x4`, copies the existing IE at `+0x8`, and sends `vndr_ie` with size
  `0x52`
- add command writes `"add"` into the set buffer and qword
  `0x700000001` at `+0x4`
- the fixed Apple capability IE uses payload size `0x18`, header qword
  `0x10106f217000add` at set-buffer body `+0x0c`, and fields at `+0x14`
- feature flag `0x46` enables the extended capability source carried in
  APSTA state `+0x2c/+0x2e/+0x2f/+0x30`
- optional extra Apple IE data is carried by APSTA state `+0x50/+0x51/+0x59`

## Local Scope

The local scaffold records constants and layout witnesses only. It does not
call `setMaxAssoc`, selector `0x57`, `setVendorIE`, or `vndr_ie` at runtime.
