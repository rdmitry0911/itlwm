# IWX AP WPA3 PMKSA sleep reconnect on Tahoe 25C56

## Scope

This layer closes the on-air SAE PMKSA reconnect gap in the shared IWM/IWX
HostAP runtime.  It does not claim GUI Internet Sharing coverage or concurrent
primary-STA roaming.  The runtime proof used the disposable Tahoe guest and a
physical Intel 6235 client; no physical macOS host was rebooted.

## Candidate

- guest: Tahoe 25C56, physical AX211/IWX passed through to QEMU
- loaded Mach-O SHA-256:
  `6b3913749daab5c8652ada2a1b8f5b4a947b111af1d827eba5d86cd4cb0841b8`
- loaded UUID: `227C0F2D-FBC9-3C19-B639-9F83FE1FFB0A`
- SSID: `AIAM-IWX-WPA3-WIP106`, channel 6
- BSSID: `86:e4:ba:20:ef:f9`
- client: Intel 6235, `c8:f7:33:f4:97:4c`
- security: SAE group 19, CCMP, PMF required, BIP-CMAC-128

The Tahoe opt-out build succeeded, contained no
`_thread_call_cancel_wait` import, and resolved all 1074 undefined symbols
against the running guest's BootKC.  Private AuxKC admission preserved the
exact five-member collection; the next guest boot reported the UUID above.

## Reference behavior and defect

The existing IWN authenticator already models SAE PMKSA as a lifetime separate
from transient SAE, PTK and firmware-station state.  It admits Open-System
authentication on a pure-SAE BSS, selects a matching PMKID at association,
returns status 53 (`INVALID_PMKID`) for a stale entry, and includes the selected
PMKID KDE in M1.  The Apple reference surface also exposes explicit PMKSA cache
ownership (`cachePMKSA`, `purgePMKSA`, `freePMKSA`, and
`setCLEAR_PMKSA_CACHE`), consistent with retaining the cache until an explicit
owner clears it.

The shared IWM/IWX runtime originally destroyed PMKSA with every lower-radio
epoch and rejected the Open-System authentication used by a supplicant's SAE
cache path.  After adding bounded PMK/PMKID retention, the first WIP exposed a
second exact defect: the M1 builder still required a live accepted SAE object.
After a valid cached association it therefore returned `EINVAL` and logged
`EAPOL M1 queue=22 replay=0` instead of starting the four-way handshake.

The final change:

1. retains only PMK, PMKID, station and BSSID across an unexpected radio/sleep
   epoch while scrubbing transient SAE/PTK/key/authorization state;
2. admits Open-System authentication only as the bounded PMKSA association
   path;
3. matches the association PMKID and selects the cached PMK, or replies with
   status 53 so the station performs a fresh SAE exchange;
4. permits a validated cached PMK to start M1 without a live SAE object and
   carries the PMKID KDE in M1, matching the established IWN behavior;
5. clears PMKSA on explicit AP stop/profile replacement and detach.

## Stale-cache fallback

Before starting the newly booted AP, the client retained old PMKID
`f8281eec3489bfd4855ec993b874b2e7`, while the new AP runtime intentionally had
no matching cache.  The client sent Open-System authentication and an
association request containing that PMKID.  WIP108 replied with status 53:

```text
CTRL-EVENT-ASSOC-REJECT ... status_code=53
PMKSA caching attempt rejected - drop PMKSA cache entry and fall back to SAE authentication
```

Without interface on/off or an external reconnect, the client then completed a
fresh SAE exchange, installed PMKID
`fdbd07a48214d0d7602c14157deb2330`, observed that exact PMKID KDE in M1, and
completed the protected four-way handshake.

## Sleep/wake PMKSA result

Before sleep the client was `COMPLETED`, held the new PMKSA entry, obtained
`192.168.88.100/24` by DHCP from guest `192.168.88.1`, and passed ICMP and HTTP
200 over the AP path.  `pmset sleepnow` reached the real S3 boundary:

```text
APSTA datapath disabled link=1 RX=0 TX=0 TXC=0
PMRD: System Sleep
IOCPUSleepKernel enter
ACPI SLEEP
```

The exact QEMU monitor reported `paused (suspended)`.  After only
`system_wakeup`, the serial trace reached `ACPI S3 WAKE`, recreated PHY,
beacon, MAC, binding, multicast, broadcast and quota resources, then enabled
the APSTA datapath.  No supplicant command, PMKSA flush, interface toggle or
manual reassociation was issued.

The client automatically found PMKID `fdbd07a48214d0d7602c14157deb2330`, used
Open-System authentication, associated with that PMKID, and completed the
cached-PMK four-way handshake:

```text
PMKSA cache entry found - try to use PMKSA caching instead of new SAE authentication
Auth Type 0
RSN: PMKID from assoc IE found from PMKSA cache
IWX AP WPA3 EAPOL M1 queue=0 replay=1
IWX AP WPA3 M2 accepted M3=0 replay=3
IWX AP WPA3 4-way complete authorized=1
```

DHCP automatically restored `192.168.88.100/24`; post-wake ICMP passed 5/5
and HTTP returned 200.  A further explicit disconnect/reconnect after wake,
without PMKSA flush or interface toggle, reused the same PMKID, reached
`COMPLETED`, passed ICMP 3/3, and returned HTTP 200.

As in earlier disposable-guest S3 runs, the unrelated virtio management SSH
path did not return a banner after wake.  The wireless AP, DHCP and data paths
were operational, and the proof interval contained no firmware fatal or
kernel panic.
