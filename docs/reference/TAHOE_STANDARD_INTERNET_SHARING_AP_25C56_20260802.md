# Tahoe standard Internet Sharing AP path (25C56, 2026-08-02)

## Reference artifacts

- Tahoe 26.2 / build 25C56 CoreWLAN from the dyld shared cache.
- CoreWLAN decompile:
  `/home/dima/Projects/ghidra_output/cr466_selector403_apsta_followup_20260511T174258_fullbatch/05_corewifi_corewlan_trace/raw_dyld_import/06_decomp/CoreWLAN/0048_0x7ff8115d3779__updateAPModeConfiguration.c`
- Tahoe `InternetSharingPreference.bundle` 40-worker decompile:
  `/home/dima/Projects/ghidra_output/InternetSharingPreference_25C56_20260801T1500Z/03_decomp/InternetSharingPreference_25C56/all_decompiled.c`
- Targeted airportd HostAP decompile:
  `/home/dima/Projects/ghidra_output/airportd_internet_sharing_25C56_20260802T0720Z/airportd_hostap.c`

## Recovered producer contract

CoreWLAN `_updateAPModeConfiguration` opens
`com.apple.airport.preferences.plist` and reads the top-level
`InternetSharing` dictionary. The Tahoe 25C56 live CFString operands map to:

| Unslid address | Value | Use |
|---:|---|---|
| `0x7ff841463dd0` | `com.apple.airport.preferences.plist` | preferences identifier |
| `0x7ff8414642b0` | `InternetSharing` | current AP configuration |
| `0x7ff841461bb0` | `SSID` | SSID bytes |
| `0x7ff841462af0` | `SSIDString` | display string |
| `0x7ff841462b10` | `SecurityType` | CoreWLAN schema string |
| `0x7ff841461310` | `Channel` | channel number |

The previously guessed top-level key `APModeConfiguration` is not read by
this function. Security strings must come from
`schemaStringForSecurityType`; protected configurations store the password
through `CWSystemKeychainSetHostAPModePassword(interface, password, nil)`.

`InternetSharingPreference.bundle` independently opens
`com.apple.nat.plist`, reads the `NAT` dictionary, and requires `Enabled` to
be a CFNumber rather than a CFBoolean. `PrimaryService` selects the upstream
network service and `SharingDevices` names the primary Wi-Fi interface. The
plugin resolves the role-3 `ap1` interface and calls its standard
`startHostAPMode:` selector. `/usr/libexec/InternetSharing` then owns DHCP,
DNS and NAT; a bare CoreWLAN HostAP start is not expected to provide them.

The reference plugin also creates an `IOPMAssertion` of type
`DenySystemSleep`, named `InternetSharingPreferencePlugin`, while sharing is
active and the upstream has IPv4. Full S3 while standard Internet Sharing is
running is therefore intentionally prevented by macOS. A forced
`pmset sleepnow` enters DarkWake but does not suspend QEMU; the AP/data path
must remain alive. Driver-level HostAP without the standard sharing daemon has
a separate real-S3 replay contract.

## Local mapping

`AirportItlwmLabCoreWLANAP` now provides the exact standard producer path:

- `--configure-default` writes `InternetSharing` with `SSID`, `SSIDString`,
  `SecurityType` and `Channel`, and uses the system HostAP keychain helper;
- `--enable-internet-sharing` and `--disable-internet-sharing` update the
  exact NAT preferences using a numeric `Enabled` value;
- `--start-default` remains a bounded direct consumer of
  `startHostAPMode:` for control-plane diagnosis;
- the helper links SystemConfiguration explicitly.

This does not claim private entitlements for the lab binary. The entitled
Apple processes remain the actual standard HostAP and sharing producers.

## On-air verification

Disposable Tahoe guest: IWN/Intel 6235, standard `configd -> airportd ->
driver` path. External client: host AX211 managed interface. Upstream:
guest virtio `en0` through the standard Internet Sharing daemon.

- Open: the default standard `Mac Pro` BSS was visible on channel 11; AX211
  joined, received `192.168.2.2` from guest `192.168.2.1`, passed bidirectional
  ICMP and guest HTTP, and returned HTTP 200 through guest NAT.
- WPA2: airportd logged `ssid=AIAM-Standard-WPA2, sec=WPA2-PSK, ch=11` with a
  masked password. The beacon advertised RSN/CCMP/PSK. AX211 completed the
  four-way handshake, received `192.168.2.2`, passed 4/4 ICMP in both
  directions, and returned HTTP 200 through guest NAT.
- WPA3: airportd logged `ssid=AIAM-Standard-WPA3, sec=WPA3-SAE, ch=11` with a
  masked password. The beacon advertised SAE, required/capable MFP and
  AES-128-CMAC. AX211 completed SAE group 19 with `pmf=2` and BIP, received
  `192.168.2.2`, passed 4/4 ICMP in both directions, and returned HTTP 200
  through guest NAT.
- WPA3 5 GHz: the same standard producer selected channel 149 without a
  private driver-side override. The beacon advertised the correct 5 GHz rate
  set plus SAE/required MFP/AES-128-CMAC. AX211 associated at 5745 MHz,
  completed SAE group 19 with PMF/BIP, received `192.168.2.2`, passed 4/4
  gateway ICMP, and returned HTTP 200 through guest NAT.
- Client reconnect: terminating the AX211 supplicant removed the live client;
  a fresh supplicant instance with the same station MAC completed a new SAE
  Commit/Confirm and four-way handshake. It reacquired `192.168.2.2`, passed
  3/3 gateway ICMP and returned HTTP 200. Serial evidence contains two
  separate `AP SAE Commit accepted`, `AP SAE Confirm ... authenticated=1`,
  and `AP WPA3 4-way complete ... authorized=1` sequences.
- Client power save: with AX211 power save enabled after an idle interval,
  guest-to-client traffic passed 8/8 packets through the AP TIM/power-save
  path, client-to-gateway traffic passed 8/8, and NAT HTTP returned 200.
- Forced sleep request with the WPA3 client active produced the reference
  `DenySystemSleep` assertion and DarkWake, not S3. Association, DHCP address,
  PMF/BIP state, 4/4 gateway ICMP and NAT HTTP 200 all remained valid after
  the request.
- Standard sharing stop had already been verified to stop the BSS and restore
  the guest's WPA3 STA association and traffic to `LabAP`.

### Product-default replay (2026-09-09)

The published default Tahoe artifact built from `6eb51401` was requalified
after restoring the target's AP admission defines to the command-line build
override.  The loaded Mach-O was identified locally by UUID
`F9E599B0-3FA4-3A9E-886C-BC3A31294DE3` and SHA-256
`91afd774a0fb51aeba21757423661e3147466cdce674ebc90dd7bbb2c9dcb11b`.

On the actual preference path (`configd -> airportd -> InternetSharing ->
AirportItlwm`), rather than the direct diagnostic selector, an external AX211
completed discovery, association, DHCP and a 20-packet gateway transfer for
each Open, WPA2-PSK and WPA3-SAE/required-PMF configuration.  For WPA3 the
client reported `SAE`, `pmf=2` and `BIP` after the completed handshake.
The same product-default WPA3 configuration was independently replayed on
5-GHz channel 149: the AX211 discovered the BSS at 5745 MHz, completed its
SAE/required-PMF association and DHCP lease, and passed 20/20 gateway probes.

The standard Open configuration was then disabled through its numeric
`NAT.Enabled` preference and re-enabled through the same producer.  The BSS
disappeared, the client disconnected, and on the subsequent `configd` start
the client automatically reassociated, retained its DHCP route, and passed a
fresh 20-packet gateway transfer.  This closes the former default-artifact
AP-admission and ordinary stop/start gates.

With WPA3 sharing active, a forced `pmset sleepnow` took the guest through
the expected capability transition while the reference
`InternetSharingPreferencePlugin` `DenySystemSleep` assertion remained
present.  The user wake republished `ap1`; the already-associated AX211 still
reported SAE, required PMF, BIP and its DHCP lease.  Three probes were lost
during the wake transition, followed by 22 consecutive successful gateway
probes.  Thus this is a recovery result, not a claim of zero-loss service
through a forced sleep transition.  The test AP was stopped normally and the
external adapter was restored to the separate LabAP afterwards.

## Contract verification

- `scripts/test_tahoe_standard_internet_sharing_ap_contract.sh`
- `scripts/test_tahoe_primary_apsta_op_mode_contract.sh`
- `scripts/test_tahoe_apsta_wpa2_hostap_contract.sh`
- `scripts/test_tahoe_apsta_wpa3_sae_hostap_contract.sh`
