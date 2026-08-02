# IWN APSTA dynamic reassociation authentication runtime (25C56)

Date: 2026-08-02

This note records a bounded physical-IWN runtime result for the Tahoe opt-out
build.  It closes the authentication/association portion of dynamic APSTA
reassociation, but it does **not** claim that the complete concurrent STA data
path is closed.

## Reference behavior used

The implementation follows Linux DVM rather than inventing an independent
scheduler model:

- `iwlagn_commit_rxon()` commits one unassociated BSS RXON and then calls
  `iwlagn_set_pan_params()`;
- `iwlagn_set_pan_params()` gives the BSS context the three-DTIM admission
  window while hardware scan is active **or** while BSS is active and
  unassociated;
- BSS and PAN station tables are context-local, so a BSS RXON must not re-add
  PAN clients;
- after the associated BSS RXON, the retained PAN beacon is replayed before
  returning the two associated contexts to the steady split.

The exact low-level reference inspected was Linux 6.8 DVM
`drivers/net/wireless/intel/iwlwifi/dvm/rxon.c`, especially
`iwlagn_set_pan_params()` and `iwlagn_commit_rxon()`.

## Driver changes exercised

The candidate contains four related corrections:

1. a retained primary BSS supplies the PAN timing epoch and beacon interval;
2. APSTA AUTH coalesces the generic reset RXON with the complete candidate
   RXON transaction;
3. the associated BSS RXON replays the retained AP beacon before steady PAN
   scheduling;
4. scan and active-unassociated AUTH own separate BSS-priority reasons, so a
   late `STOP_SCAN` cannot clear an AUTH admission window.

The runtime log publishes both scheduler owners explicitly as `scan=` and
`auth=` alongside the slot widths.

## Candidate identity

- source identity:
  `apsta-pan-priority-owners-wip9-20260802`
- AirportItlwm UUID:
  `51A1567C-9A84-30EA-811B-9D6ACB152536`
- AirportItlwm binary SHA-256:
  `2d52d6b4f332c4f8e806478d28aa5bc12c9455b1771088a9ba94adaa3827e89c`
- build variant: `Debug/Tahoe-OptOut`
- all 1074 undefined symbols resolved against the Tahoe BootKC

## Controlled topology

- guest hardware: passed-through Intel 6235 / IWN DVM firmware
- guest AP: `AIAMX13`, open, channel 13, BSSID
  `ce:f7:33:f4:97:4b`, address `192.168.3.1`
- AP client: host AX211 virtual STA, address `192.168.3.2`
- controlled upstream AP: `AIAMUP9`, WPA2-PSK, channel 9, BSSID
  `80:e4:ba:20:ef:f9`, address `192.168.4.1`
- guest STA: `9a:2a:16:66:dd:ad`

The public CoreWLAN helper was not used for dynamic reassociation because its
explicit `-[CWInterface disassociate]` also tears down the role-7 AP.  The
test used the ordinary BSD `APPLE80211_IOC_DISASSOCIATE` followed by one exact
`APPLE80211_IOC_ASSOCIATE`, which preserves the lower AP owner.

## Runtime observations

Before reassociation, an already-associated guest STA retained real traffic to
the physical `LabAP` gateway (20/20 ICMP) while the host client exchanged
traffic through the guest AP (20/20 ICMP).  This confirms the
existing-STA-to-new-AP direction.

For the harder new-STA-under-running-AP direction, two consecutive controlled
WPA2 reassociations to `AIAMUP9` reached all of the following boundaries:

- Open Authentication request and response;
- Association with AID 1;
- `wcl_auth SUCCESS_LEDGER`;
- `wcl_assoc VALIDATED_COMPLETION`;
- hostapd `AP-STA-CONNECTED`;
- hostapd `WPA: pairwise key handshake completed (RSN)`;
- hostapd `EAPOL-4WAY-HS-COMPLETED`;
- AP beacon replay after the associated BSS RXON;
- no firmware fatal or device timeout.

The pre-existing AP client remained associated and its traffic recovered in
both repetitions.  The bounded samples were 123/160 and 112/120 replies.  The
second run had no long outage; its loss was 6.7 percent under deliberate
two-channel contention.

This is materially beyond the preceding candidate, where AUTH sequence 1 was
firmware-ACKed but neither the host AP nor the physical AP could complete the
guest association while PAN was running.

## Remaining boundary

The raw association carrier does not leave the guest BSD interface media
active.  Even after the peer completed the WPA2 four-way handshake and the
driver published the associated RXON/AID, a manually assigned guest STA
address could not pass ARP/data to `192.168.4.1`.  Therefore this evidence
does not claim concurrent STA user traffic after dynamic reassociation.

The next runtime layer is the post-association primary data-queue/port-valid
handoff.  After that: repeat reconnect/roam, simultaneous-role sleep/wake,
multi-client on-air, and physical IWM/IWX validation remain open.

## Local contracts

The candidate passed:

- `scripts/test_tahoe_iwn_apsta_existing_bss_scheduler_contract.sh`
- `scripts/test_tahoe_iwn_apsta_scan_scheduler_contract.sh`
- `scripts/test_iwn_iwm_iwx_ap_csa_runtime_contract.sh`
- `scripts/test_tahoe_apsta_sleep_wake_replay_order_contract.sh`
- `git diff --check`
