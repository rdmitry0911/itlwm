# STA latency attribution: peer power save and the Wi-Fi pane

## Outcome and unchanged release

The released comeback-deadline image remains loaded: Mach-O UUID
`BE0C8CE1-4924-39F2-BE2B-5929C1039113`, guest boot
`A2F0698F-FF16-449D-AC40-E3E4117D4CF9`. These experiments change no driver
source or firmware timing policy. They narrow the latency investigation;
they do **not** close the recorded reconnect/roam packet losses or qualify
lossless WPA3 generally. No replacement kext is warranted by this diagnostic
cycle alone. The published archive remains the one attested in
[the comeback report](TAHOE_ASSOC_COMEBACK_MINIMUM_DEADLINE_20260911.md).

The fixed topology is guest IWN/6235, WPA3 `LabAP`,
`9a:fb:5d:97:a9:02` channel 13, address `172.16.66.219`; the external AX211
peer is `172.16.66.226` on `82:c3:97:84:51:c9`, channel 153. This path crosses
two wireless BSSes, not a direct one-radio echo server. Forward below means
guest-to-peer, reverse peer-to-guest. Both endpoints retained their BSSID,
channel and addresses in the before/after observations. No newstate event
occurred in the bounded observers.

## Peer power-save control

With the guest Wi-Fi pane open, temporarily change only host AX211 device
power save on/off/on. Each phase sends 20 ICMP packets with 1400-byte payload
at one-second spacing in each direction. Numbers below are ping-reported
RTTs in milliseconds, not calibrated one-way latency.

| Peer PS | Forward/reverse received | Mean F/R | Maximum F/R |
| --- | --- | --- | --- |
| on A | 20/20, 20/20 | 93.817 / 44.476 | 261.033 / 239.859 |
| off | 20/20, 19/20 | 26.304 / 28.262 | 179.388 / 112.034 |
| on C | 20/20, 20/20 | 132.071 / 49.362 | 406.246 / 225.809 |

The off phase lost reverse sequence 2. It is retained, not replaced with a
passing repeat. Peer power save contributes substantially to the measured
latency in this setup, but does not explain all delays or the missing packet.
The observer ended without errors; it saw 25 scan-end events and no newstate.
The host power-save setting was restored to on.

## Wi-Fi-pane control, peer PS held off

The first unlocked-pane open/closed/open comparison uses 20 packets per
direction per phase. All screenshots and process presence/absence are kept.

| Pane | Forward/reverse received | Mean F/R | Maximum F/R |
| --- | --- | --- | --- |
| open A | 20/20, 19/20 | 27.684 / 34.213 | 162.305 / 168.507 |
| closed | 20/20, 20/20 | 5.463 / 5.859 | 6.446 / 7.604 |
| open C | 20/20, 20/20 | 16.392 / 10.048 | 193.408 / 74.735 |

Again reverse sequence 2 is missing in open A. The observer saw 16 scan builds
with `bgscan=1`, `prepare_fg=0` and 16 corresponding ends. Its initial command
probe selected `iwn_cmd`, which is bypassed by the owned scan's direct call to
`iwn_cmd_with_doorbell_hook`. Consequently this run establishes background
classification, **not** actual command-header contents.

A separate capture attempt (`gui-capture`, q2) corrected the probe, but the
screen had locked/powered down before the test. Its three screenshots are
blank. All 20 packets per direction per phase passed and both Ethernet
captures contain 240 packets with no kernel drops. No scan was observed in
the first two traffic phases; scans resumed during the last phase even though
the screenshot remained blank. This is not an unlocked active-pane A/B/A
qualification. A running System Settings process alone is insufficient as
the GUI precondition. This attempt is kept separately, not merged into q3.

## Unlocked, awake capture with firmware TX observations

After ordinary GUI unlock, the Wi-Fi pane was visually verified. A bounded
115-second `caffeinate -d -u` assertion kept the display awake; no persistent
power setting changed. At 14:52–14:54 UTC, the open/closed/open experiment
sent 100 packets per direction per phase, 1400-byte payload, 200-ms spacing.

| Pane | Forward/reverse received | Mean F/R | Maximum F/R |
| --- | --- | --- | --- |
| open A | 100/100, 100/100 | 9.334 / 12.698 | 165.207 / 99.688 |
| closed | 100/100, 100/100 | 6.061 / 6.058 | 15.724 / 16.194 |
| open C | 100/100, 100/100 | 12.693 / 11.615 | 193.750 / 96.468 |

Both endpoint captures contain exactly 1200 ICMP packets with zero kernel
drops. Matching source/destination/type/identifier/sequence gives equal packet
sets; each of the six echo streams has 100 requests and 100 replies at both
endpoints. Endpoint-local pcap RTTs independently reproduce the ping results,
with the expected small difference in timestamp location. No cross-clock
subtraction is used.

The corrected synchronous command-entry probe observes 12 complete band
scans. Every command has background classification, 13 channels for 2.4 GHz
or 24 for 5 GHz, `max_out=112640` microseconds and `pause_scan=0xb400`
(46080 microseconds, no whole-beacon component). The transport returns zero;
this is not by itself a firmware acceptance or per-channel RF receipt.

All 12 forward echo RTTs over 20 ms overlap a command-to-scan-end interval;
none occurs in the closed-pane phase. These intervals include returns to the
service channel and are **not** measurements of uninterrupted off-channel
residence. The result supports scan-associated latency, not proof that the
firmware exceeds its programmed maximum or that every delay is in IWN.

The exact packed single-frame TX notification observer reports 606 statuses,
all `0x01` (success); five contain nonzero ACK retry counts. This is not a
census of multi-frame aggregate TX status. DTrace ends with zero errors and
no unfinished observed scan interval. There is no reproduced packet loss in
this q3 run and therefore no new localization of the earlier missing packets.

## Reference boundary and next functional investigation

Tahoe 25C56 public bridge `ffffff8001522d28` forwards one dword to adapter
`ffffff80016ac8a6`; that adapter submits the `scan_home_away_time` iovar through
the controller workqueue. The existing decompiles on `10.7.6.112` were reread;
these two excerpts are the July export, not a newly claimed 5995e24caa export.
They establish policy ownership but do not expose Broadcom firmware's exact
radio scheduling or establish a numerical RTT promise.

Intel's [versioned DVM command ABI](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
defines maximum off-channel time and the extended-beacon service pause. Its
[scan implementation](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/scan.c)
programs both while associated. The observed driver is using this mechanism;
the hypothesis that GUI scans silently disable home/away scheduling is not
supported. Arbitrarily shortening it could regress discovery, especially on
passive channels. The existing dwell-budget correction remains unchanged.

Next priority is the user-visible return-to-previous-BSS path: determine why
the AP still requests a comeback/SA Query delay, and localize packets across
the departure, new authentication and first usable data interval. The current
controls must not be used to explain away the earlier gateway-directed roam
loss: that forward path was guest-to-gateway, not guest-to-AX211.

## Evidence and restoration

Raw root:
`/home/dima/Projects/itlwm/aiam-peer-ps-roam-runtime.I7UljG`.
All manifests were checked after their corresponding controllers terminated.

| Manifest | SHA-256 |
| --- | --- |
| `evidence-ps-q1.sha256` | `334dfa39463e3da71526e3e80196ee327dc4218495aa8609c0cc96349a73c951` |
| `evidence-gui-q1.sha256` | `27590d676282042ebae4f9f9abea6db09f32126f6d892cde6bad96e5a7ca66dc` |
| `evidence-gui-capture-q2.sha256` | `eb1786465eb0c904a1a2ac769208ddd0df9cbbe0e1fbf7618e0c8e327d39920f` |
| `evidence-gui-capture-awake-q3.sha256` | `bf6bafd8f01b614e1c54d392c84f16ef9c28c17152c32663ce5b67596e3e469e` |

The q3 derived packet summary is `gui-capture-awake/packet-analysis-q3.json`.
Scripts, compile receipts, raw traces, per-phase pings, endpoint snapshots,
screenshots and pcaps remain separate for every attempt. The q2/q3 probes read
only size-guarded packed command/notification buffers at synchronous entry;
they retain no packet pointers and read no controller-layout offsets.

All observers/captures and display assertions terminated. The final host PS
setting is on; the Wi-Fi pane is reopened. Wired host default route, guest
USB management, boot and kext identities are unchanged. No fixture AP was
created, no physical `.22` operation or reboot occurred, and no disk-image
manipulation or QEMU lifecycle operation was performed.
