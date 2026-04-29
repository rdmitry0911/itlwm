# AppleBCMWLAN APSTA Public SAP Slot Surface Reference

Source: `/srv/project/ghidra_output/apsta_sap_vtables_resolved_20260426.txt`.

The resolved APSTA vtable records the concrete public SAP method surface at
slots `505..531`. The local SAP contract must preserve these slot and byte
offset aliases before a real APSTA owner class is introduced.

## Getter Slots

- slot `505`, byte `0x0fc8`: `getSSID`
- slot `506`, byte `0x0fd0`: `getCHANNEL`
- slot `507`, byte `0x0fd8`: `getSTATE`
- slot `508`, byte `0x0fe0`: `getOP_MODE`
- slot `509`, byte `0x0fe8`: `getSTATION_LIST`
- slot `510`, byte `0x0ff0`: `getSTA_IE_LIST`
- slot `511`, byte `0x0ff8`: `getKEY_RSC`
- slot `512`, byte `0x1000`: `getSTA_STATS`
- slot `513`, byte `0x1008`: `getPEER_CACHE_MAXIMUM_SIZE`
- slot `514`, byte `0x1010`: `getHOST_AP_MODE_HIDDEN`
- slot `515`, byte `0x1018`: `getSOFTAP_PARAMS`
- slot `516`, byte `0x1020`: `getSOFTAP_STATS`

## Setter Slots

- slot `517`, byte `0x1028`: `setSSID`
- slot `518`, byte `0x1030`: `setCIPHER_KEY`
- slot `519`, byte `0x1038`: `setCHANNEL`
- slot `520`, byte `0x1040`: `setHOST_AP_MODE`
- slot `521`, byte `0x1048`: `setSTA_AUTHORIZE`
- slot `522`, byte `0x1050`: `setSTA_DISASSOCIATE`
- slot `523`, byte `0x1058`: `setSTA_DEAUTH`
- slot `524`, byte `0x1060`: `setRSN_CONF`
- slot `525`, byte `0x1068`: `setPEER_CACHE_CONTROL`
- slot `526`, byte `0x1070`: `setHOST_AP_MODE_HIDDEN`
- slot `527`, byte `0x1078`: `setSOFTAP_PARAMS`
- slot `528`, byte `0x1080`: `setSOFTAP_TRIGGER_CSA`
- slot `529`, byte `0x1088`: `setSOFTAP_WIFI_NETWORK_INFO_IE`
- slot `530`, byte `0x1090`: `setSOFTAP_EXTENDED_CAPABILITIES_IE`
- slot `531`, byte `0x1098`: `setMIS_MAX_STA`

## Local Scope

The local header records slots, byte offsets, and typed function pointer
contracts only. It does not define the final C++ APSTA owner class and does not
route runtime calls through these slots yet.
