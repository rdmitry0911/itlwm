//
//  AirportItlwmSkywalkInterface.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//
#include "AirportItlwmV2.hpp"
#include "AirportItlwmSkywalkInterface.hpp"
#include <sys/CTimeout.hpp>
#include <libkern/c++/OSMetaClass.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_priv.h>

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(AirportItlwmSkywalkInterface, IO80211InfraProtocol);

const char* hexdump(uint8_t *buf, size_t len) {
    ssize_t str_len = len * 3 + 1;
    char *str = (char*)IOMalloc(str_len);
    if (!str)
        return nullptr;
    for (size_t i = 0; i < len; i++)
    snprintf(str + 3 * i, (len - i) * 3, "%02x ", buf[i]);
    str[MAX(str_len - 2, 0)] = 0;
    return str;
}

static int ieeeChanFlag2appleScanFlagVentura(int flags)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    ret |= (APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ);
    return ret;
}

static int ieeeChanFlag2apple(int flags, int bw)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    if (!(flags & IEEE80211_CHAN_PASSIVE))
        ret |= APPLE80211_C_FLAG_ACTIVE;
    if (flags & IEEE80211_CHAN_DFS)
        ret |= APPLE80211_C_FLAG_DFS;
    if (bw == -1) {
        if (flags & IEEE80211_CHAN_VHT) {
            if ((flags & IEEE80211_CHAN_VHT160) || (flags & IEEE80211_CHAN_VHT80_80))
                ret |= APPLE80211_C_FLAG_160MHZ;
            if (flags & IEEE80211_CHAN_VHT80)
                ret |= APPLE80211_C_FLAG_80MHZ;
        } else if ((flags & IEEE80211_CHAN_HT40) && (flags & IEEE80211_CHAN_HT)) {
            ret |= APPLE80211_C_FLAG_40MHZ;
            if (flags & IEEE80211_CHAN_HT40U)
                ret |= APPLE80211_C_FLAG_EXT_ABV;
        } else if (flags & IEEE80211_CHAN_HT20)
            ret |= APPLE80211_C_FLAG_20MHZ;
        else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
            ret |= APPLE80211_C_FLAG_10MHZ;
    } else {
        switch (bw) {
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                ret |= APPLE80211_C_FLAG_160MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                ret |= APPLE80211_C_FLAG_80MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_40:
                ret |= APPLE80211_C_FLAG_40MHZ;
                if (flags & IEEE80211_CHAN_HT40U)
                    ret |= APPLE80211_C_FLAG_EXT_ABV;
                break;
            case IEEE80211_CHAN_WIDTH_20:
                ret |= APPLE80211_C_FLAG_20MHZ;
                break;
            default:
                if (flags & IEEE80211_CHAN_HT20)
                    ret |= APPLE80211_C_FLAG_20MHZ;
                else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
                    ret |= APPLE80211_C_FLAG_10MHZ;
                break;
        }
    }
    return ret;
}

struct triggerCCSnapshot
{
    uint64_t qword0;
    uint64_t qword1;
    uint64_t qword2;
    uint64_t qword3;
} __attribute__((packed));
static_assert(sizeof(triggerCCSnapshot) == 0x20,
              "triggerCCSnapshot must match the first four qwords cached by Apple");

struct tahoeIPv4ParamsContract
{
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint16_t gatewayTail;
} __attribute__((packed));
static_assert(sizeof(tahoeIPv4ParamsContract) == 0x0E,
              "tahoeIPv4ParamsContract must match recovered Apple field coverage");

struct tahoeIPv6ParamsHeader
{
    uint32_t count;
    uint8_t addresses[10][16];
} __attribute__((packed));
static_assert(sizeof(tahoeIPv6ParamsHeader) == 0xA4,
              "tahoeIPv6ParamsHeader must match recovered Apple count+address coverage");

static constexpr IOReturn kIOReturnBadArgumentTahoe = static_cast<IOReturn>(0xe00002bc);

void AirportItlwmSkywalkInterface::associateSSID(uint8_t *ssid, uint32_t ssid_len, const struct ether_addr &bssid, uint32_t authtype_lower, uint32_t authtype_upper, uint8_t *key, uint32_t key_len, int key_index)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ssid_len=%u auth_lower=%u auth_upper=%u key_len=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x ic_state=%d\n",
          __FUNCTION__, ssid_len, authtype_lower, authtype_upper, key_len,
          bssid.octet[0], bssid.octet[1], bssid.octet[2],
          bssid.octet[3], bssid.octet[4], bssid.octet[5], ic->ic_state);

    ieee80211_disable_rsn(ic);
    ieee80211_disable_wep(ic);
    
    struct ieee80211_wpaparams     wpa;
    struct ieee80211_nwkey         nwkey;
    bzero(&wpa, sizeof(wpa));
    bzero(&nwkey, sizeof(nwkey));
    
    memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
    memcpy(ic->ic_des_essid, ssid, ssid_len);
    ic->ic_des_esslen = ssid_len;
    
    bool is_zero = true;
    for (int i = 0; i < IEEE80211_ADDR_LEN; i++)
    is_zero &= bssid.octet[i] == 0;
    
    if (!is_zero) {
        IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid.octet);
        ic->ic_flags |= IEEE80211_F_DESBSSID;
    }
    else {
        memset(ic->ic_des_bssid, 0, IEEE80211_ADDR_LEN);
        ic->ic_flags &= ~IEEE80211_F_DESBSSID;
    }

    // AUTHTYPE_WPA3_SAE AUTHTYPE_WPA3_FT_SAE
    // we don't really support WPA3, but we have announced we support WPA3 in card capability function. so we fake it as WPA2 to support some WPA2/WPA3 mix wifi connection.
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_SAE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_SAE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2_PSK;// hack
    }
    // AUTHTYPE_WPA3_ENTERPRISE AUTHTYPE_WPA3_FT_ENTERPRISE
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_ENTERPRISE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_ENTERPRISE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2;// hack
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK | APPLE80211_AUTHTYPE_SHA256_8021X)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_akms |= IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK;
        wpa.i_enabled = 1;
        memcpy(ic->ic_psk, key, sizeof(ic->ic_psk));
        ic->ic_flags |= IEEE80211_F_PSK;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_SHA256_8021X)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_akms |= IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_8021X;
        wpa.i_enabled = 1;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    
    if (authtype_lower == APPLE80211_AUTHTYPE_SHARED) {
        XYLog("shared key authentication is not supported!\n");
        return;
    }
    
    if (authtype_upper == APPLE80211_AUTHTYPE_NONE && authtype_lower == APPLE80211_AUTHTYPE_OPEN) { // Open or WEP Open System
        if (key_len > 0) {
            XYLog("%s %d\n", __FUNCTION__, __LINE__);
            nwkey.i_wepon = IEEE80211_NWKEY_WEP;
            nwkey.i_defkid = key_index + 1;
            nwkey.i_key[key_index].i_keylen = (int)key_len;
            nwkey.i_key[key_index].i_keydat = key;
            ieee80211_ioctl_setnwkeys(ic, &nwkey);
        }
    }
}

void AirportItlwmSkywalkInterface::setPTK(const u_int8_t *key, size_t key_len) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;
    
    ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;
    
    if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP) {
        u_int64_t prsc;
        
        /* check that key length matches that of pairwise cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
        if (key_len != keylen) {
            XYLog("PTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        prsc = /*(gtk == NULL) ? LE_READ_6(key->rsc) :*/ 0;
        
        /* map PTK to 802.11 key */
        k = &ni->ni_pairwise_key;
        memset(k, 0, sizeof(*k));
        k->k_cipher = ni->ni_rsncipher;
        k->k_rsc[0] = prsc;
        k->k_len = keylen;
        memcpy(k->k_key, key, k->k_len);
        /* install the PTK */
        if ((*ic->ic_set_key)(ic, ni, k) != 0) {
            XYLog("setting PTK failed\n");
            return;
        }
        else
            XYLog("setting PTK successfully\n");
        ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
        ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
        ni->ni_flags |= IEEE80211_NODE_RXPROT;
    } else if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
        XYLog("%s: unexpected pairwise key update received from %s\n",
              ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));
}

void AirportItlwmSkywalkInterface::setGTK(const u_int8_t *gtk, size_t key_len, u_int8_t kid, u_int8_t *rsc) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;
    
    if (gtk != NULL) {
        /* check that key length matches that of group cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
        if (key_len != keylen) {
            XYLog("GTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        /* map GTK to 802.11 key */
        k = &ic->ic_nw_keys[kid];
        if (k->k_cipher == IEEE80211_CIPHER_NONE || k->k_len != keylen || memcmp(k->k_key, gtk, keylen) != 0) {
            memset(k, 0, sizeof(*k));
            k->k_id = kid;    /* 0-3 */
            k->k_cipher = ni->ni_rsngroupcipher;
            k->k_flags = IEEE80211_KEY_GROUP;
            //if (gtk[6] & (1 << 2))
            //  k->k_flags |= IEEE80211_KEY_TX;
            k->k_rsc[0] = LE_READ_6(rsc);
            k->k_len = keylen;
            memcpy(k->k_key, gtk, k->k_len);
            /* install the GTK */
            if ((*ic->ic_set_key)(ic, ni, k) != 0) {
                XYLog("setting GTK failed\n");
                return;
            }
            else
                XYLog("setting GTK successfully\n");
        }
    }
    
    if (true) {
        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_opmode != IEEE80211_M_IBSS ||
            ++ni->ni_key_count == 2)
#endif
        {
            XYLog("marking port %s valid\n",
                  ether_sprintf(ni->ni_macaddr));
            ni->ni_port_valid = 1;
            ieee80211_set_link_state(ic, LINK_STATE_UP);
            ni->ni_assoc_fail = 0;
            if (ic->ic_opmode == IEEE80211_M_STA)
                ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
        }
    }
}

void AirportItlwmSkywalkInterface::
free()
{
    RT_SET(22);
    sRT.skFreeStep = 1;
    XYLog("DEBUG %s entry mExpansionData=%p mExpansionData2=%p "
          "instance=%p fHalService=%p rtMask=0x%07x\n",
          __FUNCTION__, mExpansionData, mExpansionData2,
          instance, fHalService, sRT.rtMask);

    // Panic timer — the previous crash (panic8) was in the destructor chain
    // triggered by super::free() → IO80211SkywalkInterface::free() → D0 → D1.
    // If it hangs again, dump full lifecycle state.
    thread_call_t skFreeTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!(sRT.rtMask & 0x800000))
                panic("SkywalkInterface::free hung  "
                      "skFreeStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "stopStep=%u freeStep=%u ss=%u | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x | "
                      "evt=%u pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                      "scanDone=%u ifType=0x%x "
                      "ls=%d lsCnt=%u scan=%u pmCnt=%u | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u",
                      sRT.skFreeStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                      sRT.stopStep, sRT.freeStep, sRT.startStep,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.postMsgCount, sRT.lastPmReq,
                      sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.scanDoneCount, sRT.ifType,
                      sRT.lastLinkState, sRT.linkSetCount,
                      sRT.scanCount, sRT.pmCount,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu);
        }, NULL);
    uint64_t skFreeDeadline;
    clock_interval_to_deadline(30, kSecondScale, &skFreeDeadline);
    thread_call_enter_delayed(skFreeTimer, skFreeDeadline);

    // mExpansionData, mExpansionData2, and fVars->registrationInfo are
    // managed by the framework (allocated in initRegistrationInfo, freed
    // in IOSkywalkNetworkInterface::free / IOSkywalkEthernetInterface::free).
    // Manual IOFree was removed — it was freeing at wrong offsets when our
    // class size was 0x10 too small.  super::free() handles cleanup.
    sRT.skFreeStep = 2;
    instance = NULL;
    fHalService = NULL;
    scanSource = NULL;
    fNextNodeToSend = NULL;
    sRT.skFreeStep = 5;
    XYLog("DEBUG %s calling super::free (IO80211InfraProtocol)\n", __FUNCTION__);
    super::free();
    RT_SET(23);
    thread_call_cancel(skFreeTimer);
    thread_call_free(skFreeTimer);
}

IOReturn AirportItlwmSkywalkInterface::
getCHANNELS_INFO(apple80211_channels_info *data)
{
    if (!data)
        return kIOReturnBadArgument;

    // Tahoe's UI path still queries legacy APPLE80211_IOC_CHANNELS_INFO.
    // Live 26.x IOC DEBUG traces showed slot [483] returning 0xe00002c7 from
    // this Skywalk vtable.  The older AirportVirtualIOCTL path already proved
    // that a simple channel inventory satisfies the family contract here, so
    // keep the Skywalk slot on that same ABI instead of shadowing it with
    // unsupported.
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (!ic)
        return kIOReturnNotReady;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        struct ieee80211_channel *channel = &ic->ic_channels[i];
        if (channel->ic_freq == 0)
            continue;

        int slot = data->num_chan_specs;
        int chanNum = ieee80211_chan2ieee(ic, channel);
        data->chan_num[slot] = chanNum;
        data->support_80Mhz[slot] = IEEE80211_IS_CHAN_VHT80(channel);
        data->support_40Mhz[slot] =
            IEEE80211_IS_CHAN_HT40(channel) || IEEE80211_IS_CHAN_VHT40(channel);
        data->num_chan_specs++;
        if (data->num_chan_specs >= APPLE80211_MAX_CHANNELS)
            break;
    }
    return kIOReturnSuccess;
}

UInt64 AirportItlwmSkywalkInterface::createEventPipe(IO80211APIUserClient *client)
{
    return super::createEventPipe(client);
}

IOReturn AirportItlwmSkywalkInterface::
processBSDCommand(ifnet_t interface, UInt cmd, void *data)
{
    // Tahoe removed a large block of legacy Apple80211 GET/SET methods from the
    // IO80211InfraProtocol vtable. Our driver still implements those semantics
    // as helper methods below, but without an explicit BSD-command bridge they
    // never get called on 26.x.
    //
    // Evidence kept here so the reason for this bridge is not lost:
    // - airportd repeatedly queried APPLE80211_IOC_SSID and received
    //   0xe0822403 / "driver not available", aborting auto-join.
    // - docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/89_remaining_gaps_closed_checked.yaml
    //   shows Tahoe first tries WCL and then expects driver fallback behavior
    //   matching IO80211Controller cache semantics (success + zeroed data).
    // - Our Tahoe target does not build AirportSTAIOCTL.cpp, so the old ioctl
    //   dispatcher never runs here; the BSD path must forward these requests.
    if ((cmd == SIOCGA80211 || cmd == SIOCSA80211) && data != NULL) {
        IOReturn ret = processApple80211Ioctl(cmd, (apple80211req *)data);
        if (ret != kIOReturnUnsupported)
            return ret;
    }
    return super::processBSDCommand(interface, cmd, data);
}

IOReturn AirportItlwmSkywalkInterface::
processApple80211Ioctl(UInt cmd, apple80211req *req)
{
    // Tahoe's non-virtual interface path still queries VIRTUAL_IF_ROLE/PARENT
    // during airportd/CoreWiFi _initInterface.  Reverse docs record Apple's
    // expected behavior here: non-virtual interfaces do NOT return raw POSIX
    // ENXIO/6, they return the Apple80211-specific -3903 (0xe082280e) code.
    //
    // Important Tahoe-specific detail from the live 853f68e logs:
    // our earlier source already had these switch cases, yet the framework
    // still logged raw `6` for both IOCTLs.  The reason is that these Apple
    // wrappers are payload-less safe-to-fail requests, while our bridge used
    // to reject *all* req_data == NULL requests before the switch.  That sent
    // VIRTUAL_IF_ROLE/PARENT straight back into the framework fallback path,
    // which is exactly where the raw POSIX-style ENXIO/6 surfaced.
    //
    // So the real 1:1 fix is not "add cases somewhere in the switch".  The
    // fix is to intercept these payload-less Tahoe IOCTLs *before* the generic
    // req_data validation and return the Apple-specific not-a-virtual-interface
    // contract directly.
    static const IOReturn kApple80211NotVirtualInterface =
        static_cast<IOReturn>(0xe082280e);

    if (req == NULL)
        return kIOReturnUnsupported;

    switch (req->req_type) {
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
        case APPLE80211_IOC_VIRTUAL_IF_PARENT:
            return kApple80211NotVirtualInterface;
        default:
            break;
    }

    if (req->req_data == NULL)
        return kIOReturnUnsupported;

    // Tahoe architectural gap fixed here:
    // the Skywalk-only BSD bridge originally forwarded just a small hand-picked
    // subset of Apple80211 IOCTLs.  That diverged from the legacy STA IOCTL
    // plane, where APPLE80211_IOC_POWERSAVE and several other state-carrier /
    // bring-up requests were already wired to real handlers.
    //
    // Live proof on 4973c4d:
    // - WCL logged `setPOWERSAVE ... not supported` during early init even
    //   though this class already implemented setPOWERSAVE/getPOWERSAVE.
    // - The failure was not inside the handler.  The BSD bridge simply never
    //   routed APPLE80211_IOC_POWERSAVE to it, so Tahoe fell back to
    //   unsupported before the recovered handler could run.
    //
    // Restore those routes here instead of papering over each symptom higher
    // up.  This keeps the Tahoe Skywalk path architecturally aligned with the
    // legacy IOCTL plane: if a real handler exists in AirportItlwm /
    // AirportItlwmSkywalkInterface, the BSD bridge must make it reachable.
    switch (req->req_type) {
        case APPLE80211_IOC_SSID:
            return (cmd == SIOCGA80211) ? getSSID((apple80211_ssid_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_AUTH_TYPE:
            if (cmd == SIOCGA80211)
                return getAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_CHANNEL:
            if (cmd == SIOCGA80211)
                return getCHANNEL((apple80211_channel_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_POWER:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getPOWER(this, (apple80211_power_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setPOWER(this, (apple80211_power_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_POWERSAVE:
            if (cmd == SIOCGA80211)
                return getPOWERSAVE((apple80211_powersave_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setPOWERSAVE((apple80211_powersave_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_BSSID:
            return (cmd == SIOCGA80211) ? getBSSID((apple80211_bssid_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_RESULT:
            return (cmd == SIOCGA80211) ? getSCAN_RESULT((apple80211_scan_result *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_STATE:
            return (cmd == SIOCGA80211) ? getSTATE((apple80211_state_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_PHY_MODE:
            return (cmd == SIOCGA80211) ? getPHY_MODE((apple80211_phymode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NOISE:
            return (cmd == SIOCGA80211) ? getNOISE((apple80211_noise_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RSSI:
            return (cmd == SIOCGA80211) ? getRSSI((apple80211_rssi_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATE:
            return (cmd == SIOCSA80211) ? setASSOCIATE((apple80211_assoc_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DISASSOCIATE:
            return (cmd == SIOCSA80211) ? setDISASSOCIATE(req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RATE:
            return (cmd == SIOCGA80211) ? getRATE((apple80211_rate_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_TXPOWER:
            return (cmd == SIOCGA80211) ? getTXPOWER((apple80211_txpower_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_OP_MODE:
            return (cmd == SIOCGA80211) ? getOP_MODE((apple80211_opmode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SUPPORTED_CHANNELS:
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            return (cmd == SIOCGA80211) ? getSUPPORTED_CHANNELS((apple80211_sup_channel_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LOCALE:
            return (cmd == SIOCGA80211) ? getLOCALE((apple80211_locale_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DEAUTH:
            if (cmd == SIOCGA80211)
                return getDEAUTH((apple80211_deauth_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setDEAUTH((apple80211_deauth_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_RATE_SET:
            return (cmd == SIOCGA80211) ? getRATE_SET((apple80211_rate_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RSN_IE:
            if (cmd == SIOCGA80211)
                return getRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AP_IE_LIST:
            return (cmd == SIOCGA80211) ? getAP_IE_LIST((apple80211_ap_ie_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CIPHER_KEY:
            return (cmd == SIOCSA80211) ? setCIPHER_KEY((apple80211_key *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATION_STATUS:
            return (cmd == SIOCGA80211) ? getASSOCIATION_STATUS((apple80211_assoc_status_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_COUNTRY_CODE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_DRIVER_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getDRIVER_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_HARDWARE_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getHARDWARE_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_MCS:
            return (cmd == SIOCGA80211) ? getMCS((apple80211_mcs_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getMCS_INDEX_SET((apple80211_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_VHT:
            if (cmd == SIOCGA80211)
                return getMCS_VHT((apple80211_mcs_vht_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_REQ:
            return (cmd == SIOCSA80211) ? setSCAN_REQ((apple80211_scan_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NSS:
            return (cmd == SIOCGA80211) ? getNSS((apple80211_nss_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CURRENT_NETWORK:
            return (cmd == SIOCGA80211) ? getCURRENT_NETWORK((apple80211_scan_result *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
            return (cmd == SIOCGA80211) ? getLINK_CHANGED_EVENT_DATA((apple80211_link_changed_event_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_VHT_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getVHT_MCS_INDEX_SET((apple80211_vht_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        default:
            return kIOReturnUnsupported;
    }
}

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwmSkywalkInterface::
init(IOService *provider, ether_addr *addr)
{
    XYLog("DEBUG %s entry provider=%p addr=%p\n", __PRETTY_FUNCTION__, provider, addr);
    // The previous switch to IO80211InfraInterface::init(provider, addr)
    // panicked immediately on boot:
    //   IO80211InfraInterface::linkState() + 0xb, CR2=0x18
    // inside IO80211PeerManager::initWithInterface() during
    // IO80211SkywalkInterface::start().
    //
    // New 26.3 decompile from
    // /Volumes/macos-750/Users/bob/Projects/Декомпилы/ghidra_output/IO80211Family_decompiled.c
    // confirms why: IO80211InfraInterface::linkState() dereferences
    // *(this + 0x128) + 0x18, so the 2-arg path still left the infra ivar
    // block unavailable at start() for the current port state.
    //
    // Until the missing constructor/ivar path is fully recovered 1:1, the
    // only crash-free contract backed by both live runtime and decompile is
    // the no-arg IO80211InfraInterface::init() path.
    if (!IO80211InfraInterface::init()) {
        XYLog("%s IO80211InfraInterface::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    XYLog("DEBUG %s IO80211InfraInterface::init OK\n", __FUNCTION__);
#else
bool AirportItlwmSkywalkInterface::
init(IOService *provider)
{
    XYLog("DEBUG %s entry provider=%p\n", __PRETTY_FUNCTION__, provider);
    bool ret = super::init(provider);
    if (!ret) {
        XYLog("%s super::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    XYLog("DEBUG %s super::init OK\n", __FUNCTION__);
#endif
    instance = OSDynamicCast(AirportItlwm, provider);
    if (!instance) {
        XYLog("DEBUG %s FAIL: provider is not AirportItlwm\n", __FUNCTION__);
        return false;
    }
    this->fHalService = instance->fHalService;
    this->scanSource = instance->scanSource;
    this->cachedPowersaveLevel = APPLE80211_POWERSAVE_MODE_DISABLED;
    this->cachedOSFeatureFlags = 0;
    this->cachedDhcpRenewalData = false;
    this->cachedBatteryPowerSaveMode = 0;
    this->cachedPowerProfile = 0;
    this->cachedIPv4Address = 0;
    this->cachedIPv4Netmask = 0;
    this->cachedIPv4Reserved = 0;
    this->cachedIPv4Gateway = 0;
    this->cachedIPv4GatewayTail = 0;
    this->cachedIPv6Count = 0;
    memset(this->cachedIPv6Addresses, 0, sizeof(this->cachedIPv6Addresses));
    memset(this->cachedIPv6LinkLocalAddress, 0, sizeof(this->cachedIPv6LinkLocalAddress));
    this->cachedInfraEnumerated = false;
    memset(this->cachedTriggerCC, 0, sizeof(this->cachedTriggerCC));
    this->cachedTriggerCCMode = 0;
    this->hasCachedTriggerCC = false;
    RT3_SET(12); // SkywalkInterface::init OK
    XYLog("DEBUG %s OK: instance=%p fHalService=%p scanSource=%p\n",
          __FUNCTION__, instance, fHalService, scanSource);
    return true;
}

//ifnet_t AirportItlwmSkywalkInterface::
//getBSDInterface()
//{
//    if (instance->bsdInterface)
//        return instance->bsdInterface->getIfnet();
//    return NULL;
//}

IOReturn AirportItlwmSkywalkInterface::
getSSID(struct apple80211_ssid_data *sd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211Controller::getSSIDData always pre-zeroes and returns success.
    // When not associated, the cached SSID data is zeroed (ssid_len=0).
    // Returning error here causes airportd to abort auto-join with "driver not available".
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(sd->ssid_bytes, ic->ic_des_essid, strlen((const char*)ic->ic_des_essid));
        sd->ssid_len = (uint32_t)strlen((const char*)ic->ic_des_essid);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    ad->version = APPLE80211_VERSION;
    ad->authtype_lower = current_authtype_lower;
    ad->authtype_upper = current_authtype_upper;
    XYLog("DEBUG %s lower=%u upper=%u\n", __FUNCTION__, current_authtype_lower, current_authtype_upper);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    XYLog("DEBUG %s lower=%u upper=%u\n", __FUNCTION__, ad->authtype_lower, ad->authtype_upper);
    current_authtype_lower = ad->authtype_lower;
    current_authtype_upper = ad->authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCIPHER_KEY(struct apple80211_key *key)
{
    XYLog("%s\n", __FUNCTION__);
    const char* keydump = hexdump(key->key, key->key_len);
    const char* rscdump = hexdump(key->key_rsc, key->key_rsc_len);
    const char* eadump = hexdump(key->key_ea.octet, APPLE80211_ADDR_LEN);
    static_assert(__offsetof(struct apple80211_key, key_ea) == 92, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, key_rsc_len) == 80, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_len) == 100, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kek_len) == 120, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_key) == 104, "struct corrupted");
    if (keydump && rscdump && eadump)
        XYLog("Set key request: len=%d cipher_type=%d flags=%d index=%d key=%s rsc_len=%d rsc=%s ea=%s\n",
              key->key_len, key->key_cipher_type, key->key_flags, key->key_index, keydump, key->key_rsc_len, rscdump, eadump);
    else
        XYLog("Set key request, but failed to allocate memory for hexdump\n");
    
    if (keydump)
        IOFree((void*)keydump, 3 * key->key_len + 1);
    if (rscdump)
        IOFree((void*)rscdump, 3 * key->key_rsc_len + 1);
    if (eadump)
        IOFree((void*)eadump, 3 * APPLE80211_ADDR_LEN + 1);
    
    switch (key->key_cipher_type) {
        case APPLE80211_CIPHER_NONE:
            // clear existing key
//            XYLog("Setting NONE key is not supported\n");
            break;
        case APPLE80211_CIPHER_WEP_40:
        case APPLE80211_CIPHER_WEP_104:
            XYLog("Setting WEP key %d is not supported\n", key->key_index);
            break;
        case APPLE80211_CIPHER_TKIP:
        case APPLE80211_CIPHER_AES_OCB:
        case APPLE80211_CIPHER_AES_CCM:
            switch (key->key_flags) {
                case 4: // PTK
                    setPTK(key->key, key->key_len);
                    break;
                case 0: // GTK
                    setGTK(key->key, key->key_len, key->key_index, key->key_rsc);
                    break;
            }
            break;
        case APPLE80211_CIPHER_PMK:
            XYLog("Setting WPA PMK is not supported\n");
            break;
        case APPLE80211_CIPHER_MSK:
            XYLog("Setting MSK\n");
            ieee80211_pmksa_add(fHalService->get80211Controller(), IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr, key->key, 0);
            break;
        case APPLE80211_CIPHER_PMKSA:
            XYLog("Setting WPA PMKSA\n");
            ieee80211_pmksa_add(fHalService->get80211Controller(), IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr, key->key, 0);
            break;
    }
    //fInterface->postMessage(APPLE80211_M_CIPHER_KEY_CHANGED);
    if (key->key_cipher_type == APPLE80211_CIPHER_TKIP ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_OCB ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_CCM) {
        if (key->key_flags == 0) {
            // GTK installed — 4-way handshake complete, notify Apple supplicant
            XYLog("%s posting RSN_HANDSHAKE_DONE after GTK install\n", __FUNCTION__);
            postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE, NULL, 0, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPHY_MODE(struct apple80211_phymode_data *pd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ic_curmode=%d ic_flags=0x%x ic_state=%d\n",
          __FUNCTION__, ic->ic_curmode, ic->ic_flags, ic->ic_state);
    pd->version = APPLE80211_VERSION;
    pd->phy_mode = APPLE80211_MODE_11A
    | APPLE80211_MODE_11B
    | APPLE80211_MODE_11G
    | APPLE80211_MODE_11N;
    
    if (ic->ic_flags & IEEE80211_F_VHTON)
        pd->phy_mode |= APPLE80211_MODE_11AC;
    
    if (ic->ic_flags & IEEE80211_F_HEON)
        pd->phy_mode |= APPLE80211_MODE_11AX;
    
    switch (fHalService->get80211Controller()->ic_curmode) {
        case IEEE80211_MODE_AUTO:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
        case IEEE80211_MODE_11A:
            pd->active_phy_mode = APPLE80211_MODE_11A;
            break;
        case IEEE80211_MODE_11B:
            pd->active_phy_mode = APPLE80211_MODE_11B;
            break;
        case IEEE80211_MODE_11G:
            pd->active_phy_mode = APPLE80211_MODE_11G;
            break;
        case IEEE80211_MODE_11N:
            pd->active_phy_mode = APPLE80211_MODE_11N;
            break;
        case IEEE80211_MODE_11AC:
            pd->active_phy_mode = APPLE80211_MODE_11AC;
            break;
        case IEEE80211_MODE_11AX:
            pd->active_phy_mode = APPLE80211_MODE_11AX;
            break;
            
        default:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
    }
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCHANNEL(struct apple80211_channel_data *cd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211 framework pre-zeroes channel data and returns success.
    // When not associated, channel=0 / flags=0.
    // Returning error here causes airportd getCHANNEL queries to fail during init.
    memset(cd, 0, sizeof(apple80211_channel_data));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        cd->channel.channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        cd->channel.flags = ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags, ic->ic_bss->ni_chw);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSTATE(struct apple80211_state_data *sd)
{
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    sd->state = fHalService->get80211Controller()->ic_state;
    XYLog("DEBUG %s ic_state=%d\n", __FUNCTION__, sd->state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_INDEX_SET(struct apple80211_mcs_index_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(ad, 0, sizeof(*ad));
        ad->version = APPLE80211_VERSION;
        size_t size = min(ARRAY_SIZE(ic->ic_bss->ni_rxmcs), ARRAY_SIZE(ad->mcs_set_map));
        for (int i = 0; i < size; i++)
            ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];
        XYLog("DEBUG %s OK\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    XYLog("DEBUG %s ic_state=%d → 6\n", __FUNCTION__, ic->ic_state);
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getVHT_MCS_INDEX_SET(struct apple80211_vht_mcs_index_set_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC) {
        XYLog("DEBUG %s ic_bss=%p ic_curmode=%d → error\n", __FUNCTION__, ic->ic_bss, ic->ic_curmode);
        return kIOReturnError;
    }
    memset(data, 0, sizeof(struct apple80211_vht_mcs_index_set_data));
    data->version = APPLE80211_VERSION;
    data->mcs_map = ic->ic_bss->ni_vht_mcsinfo.tx_mcs_map;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_VHT(struct apple80211_mcs_vht_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC) {
        XYLog("DEBUG %s ic_bss=%p ic_curmode=%d → error\n", __FUNCTION__, ic->ic_bss, ic->ic_curmode);
        return kIOReturnError;
    }
    memset(data, 0, sizeof(struct apple80211_mcs_vht_data));
    data->version = APPLE80211_VERSION;
    data->guard_interval = (ieee80211_node_supports_vht_sgi80(ic->ic_bss) || ieee80211_node_supports_vht_sgi160(ic->ic_bss)) ? APPLE80211_GI_SHORT : APPLE80211_GI_LONG;
    data->index = ic->ic_bss->ni_txmcs;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    switch (ic->ic_bss->ni_chw) {
        case IEEE80211_CHAN_WIDTH_40:
            data->bw = 40;
            break;
        case IEEE80211_CHAN_WIDTH_80:
            data->bw = 80;
            break;
        case IEEE80211_CHAN_WIDTH_80P80:
        case IEEE80211_CHAN_WIDTH_160:
            data->bw = 160;
            break;
            
        default:
            data->bw = 20;
            break;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRATE_SET(struct apple80211_rate_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(ad, 0, sizeof(*ad));
        ad->version = APPLE80211_VERSION;
        ad->num_rates = ic->ic_bss->ni_rates.rs_nrates;
        size_t size = min(ic->ic_bss->ni_rates.rs_nrates, ARRAY_SIZE(ad->rates));
        for (int i=0; i < size; i++) {
            ad->rates[i].version = APPLE80211_VERSION;
            ad->rates[i].rate = ic->ic_bss->ni_rates.rs_rates[i];
            ad->rates[i].flags = 0;
        }
        return kIOReturnSuccess;
    }
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getOP_MODE(struct apple80211_opmode_data *od)
{
    XYLog("DEBUG VTABLE [475] %s\n", __FUNCTION__);
    od->version = APPLE80211_VERSION;
    od->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTXPOWER(struct apple80211_txpower_data *txd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(txd, 0, sizeof(*txd));
        txd->version = APPLE80211_VERSION;
        txd->txpower = ic->ic_txpower;
        txd->txpower_unit = APPLE80211_UNIT_PERCENT;
        return kIOReturnSuccess;
    }
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getRATE(struct apple80211_rate_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL) {
        XYLog("DEBUG %s ic_bss=NULL → 6\n", __FUNCTION__);
        return 6;
    }
    int nss;
    int sgi;
    int index = 0;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(rd, 0, sizeof(*rd));
        rd->version = APPLE80211_VERSION;
        rd->num_radios = 1;
        sgi = ieee80211_node_supports_sgi(ic->ic_bss);
        if (ic->ic_curmode == IEEE80211_MODE_11AC) {
            if (sgi)
                index += 1;
            nss = fHalService->getDriverInfo()->getTxNSS();
            switch (ic->ic_bss->ni_chw) {
                case IEEE80211_CHAN_WIDTH_40:
                    index += 4;
                    break;
                case IEEE80211_CHAN_WIDTH_80:
                    index += 8;
                    break;
                case IEEE80211_CHAN_WIDTH_80P80:
                case IEEE80211_CHAN_WIDTH_160:
                    index += 12;
                    break;

                default:
                    break;
            }
            index += 2 * (nss - 1);
            const struct ieee80211_vht_rateset *rs = &ieee80211_std_ratesets_11ac[index];
            rd->rate[0] = rs->rates[ic->ic_bss->ni_txmcs % rs->nrates] / 2;
        } else if (ic->ic_curmode == IEEE80211_MODE_11N) {
            int is_40mhz = ic->ic_bss->ni_chw == IEEE80211_CHAN_WIDTH_40;
            if (sgi)
                index += 1;
            if (is_40mhz)
                index += (IEEE80211_HT_RATESET_MIMO4_SGI + 1);
            index += (ic->ic_bss->ni_txmcs / 16);
            nss = ic->ic_bss->ni_txmcs / 8 + 1;
            index += 2 * (nss - 1);
            rd->rate[0] = ieee80211_std_ratesets_11n[index].rates[ic->ic_bss->ni_txmcs % 8] / 2;
        } else
            rd->rate[0] = ic->ic_bss->ni_rates.rs_rates[ic->ic_bss->ni_txrate];
        return kIOReturnSuccess;
    }
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getBSSID(struct apple80211_bssid_data *bd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple's IO80211 framework (FUN_ffffff8002215524) pre-zeroes BSSID and returns success.
    // When not associated, BSSID is all-zero.
    // Returning error here causes airportd to fail during init and abort auto-join.
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRSSI(struct apple80211_rssi_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG VTABLE [476] %s ic_state=%d\n", __FUNCTION__, ic->ic_state);
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(rd, 0, sizeof(*rd));
        rd->num_radios = 1;
        rd->rssi_unit = APPLE80211_UNIT_DBM;
        rd->rssi[0] = rd->aggregate_rssi
        = rd->rssi_ext[0]
        = rd->aggregate_rssi_ext
        = -(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi);
        return kIOReturnSuccess;
    }
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getRSN_IE(struct apple80211_rsn_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie == NULL) {
        return kIOReturnError;
    }
    data->version = APPLE80211_VERSION;
    if (ic->ic_rsn_ie_override[1] > 0) {
        data->len = 2 + ic->ic_rsn_ie_override[1];
        memcpy(data->ie, ic->ic_rsn_ie_override, data->len);
    }
    else {
        data->len = 2 + ic->ic_bss->ni_rsnie[1];
        memcpy(data->ie, ic->ic_bss->ni_rsnie, data->len);
    }
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
setRSN_IE(struct apple80211_rsn_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;
    static_assert(sizeof(ic->ic_rsn_ie_override) == APPLE80211_MAX_RSN_IE_LEN, "Max RSN IE length mismatch");
    memcpy(ic->ic_rsn_ie_override, data->ie, APPLE80211_MAX_RSN_IE_LEN);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr)
        ieee80211_save_ie(data->ie, &ic->ic_bss->ni_rsnie);
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
getAP_IE_LIST(struct apple80211_ap_ie_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;
    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie_tlv == NULL || ic->ic_bss->ni_rsnie_tlv_len == 0 || ic->ic_bss->ni_rsnie_tlv_len > data->len || ic->ic_bss->ni_rsnie_tlv_len > 1024)
        return kIOReturnError;
    data->version = APPLE80211_VERSION;
    data->len = ic->ic_bss->ni_rsnie_tlv_len;
    memcpy(data->ie_data, ic->ic_bss->ni_rsnie_tlv, data->len);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getNOISE(struct apple80211_noise_data *nd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN) {
        memset(nd, 0, sizeof(*nd));
        nd->version = APPLE80211_VERSION;
        nd->num_radios = 1;
        nd->noise[0]
        = nd->aggregate_noise = -fHalService->getDriverInfo()->getBSSNoise();
        nd->noise_unit = APPLE80211_UNIT_DBM;
        XYLog("DEBUG %s noise=%d\n", __FUNCTION__, nd->noise[0]);
        return kIOReturnSuccess;
    }
    XYLog("DEBUG %s ic_state=%d → 6\n", __FUNCTION__, ic->ic_state);
    return 6;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWERSAVE(struct apple80211_powersave_data *pd)
{
    XYLog("DEBUG VTABLE [472] %s\n", __FUNCTION__);
    pd->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getPOWERSAVE on Tahoe reads back a cached power-save
    // mode from core state instead of reporting the IOC as unsupported.  Live
    // 26.3 logs showed WCL sending APPLE80211_IOC_POWERSAVE with level 7 very
    // early during bring-up; our old kIOReturnUnsupported path made WCL log
    // "arg->powersave_level = 7 not supported" and diverged from the reference
    // contract before scan-complete delivery even mattered.
    pd->powersave_level = cachedPowersaveLevel;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWERSAVE(struct apple80211_powersave_data *pd)
{
    XYLog("DEBUG VTABLE [547] %s level=%u\n", __FUNCTION__, pd ? pd->powersave_level : 0);
    if (pd == NULL)
        return kIOReturnBadArgument;

    // Recovered Apple contract:
    // - Tahoe WCL expects APPLE80211_IOC_POWERSAVE to succeed during startup.
    // - AppleBCMWLANCore::getPOWERSAVE returns the last accepted 32-bit level.
    // Mirroring that 1:1 externally means accepting the requested policy here
    // and returning the same cached value from getPOWERSAVE later.
    cachedPowersaveLevel = pd->powersave_level;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getNSS(struct apple80211_nss_data *data)
{
    XYLog("DEBUG VTABLE [510] %s nss=%d\n", __FUNCTION__, fHalService->getDriverInfo()->getTxNSS());
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setASSOCIATE(struct apple80211_assoc_data *ad)
{
    RT2_SET(3); sRT.assocCount++;
    XYLog("%s [%s] mode=%d ad_auth_lower=%d ad_auth_upper=%d rsn_ie_len=%d%s%s%s%s%s%s%s\n", __FUNCTION__, ad->ad_ssid, ad->ad_mode, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_rsn_ie_len,
          (ad->ad_flags & 2) ? ", Instant Hotspot" : "",
          (ad->ad_flags & 4) ? ", Auto Instant Hotspot" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 1) ? ", don't disassociate" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 2) ? ", don't blacklist" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 4) ? ", closed Network" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 8) ? ", 802.1X" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 0x20) ? ", force BSSID" : "");
    
    struct apple80211_rsn_ie_data rsn_ie_data;
    struct apple80211_authtype_data auth_type_data;
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!ad)
        return kIOReturnError;
    
    XYLog("DEBUG %s ic_state=%d ic_opmode=%d\n", __FUNCTION__, ic->ic_state, ic->ic_opmode);
    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (already in ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ad->ad_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = ad->ad_auth_upper;
        auth_type_data.authtype_lower = ad->ad_auth_lower;
        setAUTH_TYPE(&auth_type_data);
        rsn_ie_data.version = APPLE80211_VERSION;
        rsn_ie_data.len = ad->ad_rsn_ie[1] + 2;
        memcpy(rsn_ie_data.ie, ad->ad_rsn_ie, rsn_ie_data.len);
        setRSN_IE(&rsn_ie_data);

        associateSSID(ad->ad_ssid, ad->ad_ssid_len, ad->ad_bssid, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_key.key, ad->ad_key.key_len, ad->ad_key.key_index);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDISASSOCIATE(void *ad)
{
    RT2_SET(7);
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }
    
    disassocIsVoluntary = true;

    ieee80211_del_ess(ic, nullptr, 0, 1);
    ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
    ic->ic_rsn_ie_override[1] = 0;
#endif
    ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
    ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSUPPORTED_CHANNELS(struct apple80211_sup_channel_data *ad)
{
    XYLog("DEBUG VTABLE [477] %s\n", __FUNCTION__);
    if (!ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_channels = 0;
    struct ieee80211com *ic = fHalService->get80211Controller();
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq != 0) {
            ad->supported_channels[ad->num_channels].channel = ieee80211_chan2ieee(ic, &ic->ic_channels[i]);
            ad->supported_channels[ad->num_channels].flags = ieeeChanFlag2appleScanFlagVentura(ic->ic_channels[i].ic_flags);
            ad->num_channels++;
        }
    }
    XYLog("DEBUG %s num_channels=%d ic_state=%d\n",
          __FUNCTION__, ad->num_channels, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLOCALE(struct apple80211_locale_data *ld)
{
    if (!ld)
        return kIOReturnError;
    ld->version = APPLE80211_VERSION;
    ld->locale  = APPLE80211_LOCALE_FCC;
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getDEAUTH(struct apple80211_deauth_data *da)
{
    if (!da)
        return kIOReturnError;
    da->version = APPLE80211_VERSION;
    struct ieee80211com *ic = fHalService->get80211Controller();
    da->deauth_reason = ic->ic_deauth_reason;
    XYLog("DEBUG %s deauth_reason=%d ic_state=%d\n", __FUNCTION__, da->deauth_reason, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getASSOCIATION_STATUS(struct apple80211_assoc_status_data *hv)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (!hv)
        return kIOReturnError;
    memset(hv, 0, sizeof(*hv));
    hv->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN)
        hv->status = APPLE80211_STATUS_SUCCESS;
    else
        hv->status = APPLE80211_STATUS_UNAVAILABLE;
    XYLog("DEBUG %s status=%d ic_state=%d\n", __FUNCTION__, hv->status, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCLEAR_PMKSA_CACHE(void *req)
{
    XYLog("%s\n", __FUNCTION__);
    struct ieee80211com *ic = fHalService->get80211Controller();
    //if doing background or active scan, don't free nodes.
    if ((ic->ic_flags & IEEE80211_F_BGSCAN) || (ic->ic_flags & IEEE80211_F_ASCAN))
        return kIOReturnSuccess;
    ieee80211_free_allnodes(ic, 0);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDEAUTH(struct apple80211_deauth_data *da)
{
    XYLog("DEBUG %s reason=%d\n", __FUNCTION__, da ? da->deauth_reason : -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS(struct apple80211_mcs_data* md)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN ||  ic->ic_bss == NULL || !md) {
        XYLog("DEBUG %s ic_state=%d ic_bss=%p → 6\n", __FUNCTION__, ic->ic_state, ic->ic_bss);
        return 6;
    }
    md->version = APPLE80211_VERSION;
    md->index = ic->ic_bss->ni_txmcs;
    XYLog("DEBUG %s mcs=%d\n", __FUNCTION__, md->index);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLINK_CHANGED_EVENT_DATA(struct apple80211_link_changed_event_data *ed)
{
    if (ed == nullptr)
        return 16;
    
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    bzero(ed, sizeof(apple80211_link_changed_event_data));
    ed->isLinkDown = !(instance->currentStatus & kIONetworkLinkActive);
    if (ed->isLinkDown) {
        ed->voluntary = disassocIsVoluntary;
        ed->reason = APPLE80211_LINK_DOWN_REASON_DEAUTH;
    } else
        ed->rssi = -(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi);
    XYLog("Link %s, reason: %d, voluntary: %d\n", ed->isLinkDown ? "down" : "up", ed->reason, ed->voluntary);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setSCAN_REQ(struct apple80211_scan_data *sd)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s Type: %u BSS Type: %u PHY Mode: %u Dwell: %u Rest: %u Channels: %u SSID: %s ic_state=%d\n",
          __FUNCTION__,
          sd->scan_type,
          sd->bss_type,
          sd->phy_mode,
          sd->dwell_time,
          sd->rest_time,
          sd->num_channels,
          sd->ssid,
          ic->ic_state);
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    /*
     * Reset SCAN_RESULT iterator — airportd/IO80211 framework reads
     * SCAN_RESULT immediately after SCAN_REQ returns success.  If
     * ic_tree already has nodes from the internal scan, the framework
     * can iterate them right away.  Without this reset, a stale
     * fScanResultWrapping=true from a previous cycle causes SCAN_RESULT
     * to return 5 ("end") before returning any nodes.
     */
    fNextNodeToSend = NULL;
    fScanResultWrapping = false;

    if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST || sd->scan_type == APPLE80211_SCAN_TYPE_PASSIVE) {
        if (scanSource) {
            scanSource->setTimeoutMS(100);
            scanSource->enable();
        }
        return kIOReturnSuccess;
    }
    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_TRIGGER_CC(triggerCC *data)
{
    if (!data)
        return kIOReturnBadArgument;

    const auto *snapshot = reinterpret_cast<const triggerCCSnapshot *>(data);
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const uint32_t mode = *reinterpret_cast<const uint32_t *>(raw + 0x8);
    const uint32_t trigger = *reinterpret_cast<const uint32_t *>(raw + 0x4);
    const uint32_t channelMetric = *reinterpret_cast<const uint32_t *>(raw + 0x10);

    // Tahoe 26.x calls APPLE80211_IOC_WCL_TRIGGER_CC during the scan manager
    // bring-up path.  The Apple producer does not treat it as optional:
    // AppleBCMWLANCore::setWCL_TRIGGER_CC first copies the first four qwords of
    // the request into adapter-owned state, then accepts mode 0/1 and returns
    // 0xe00002bc only for any other mode.  Returning unsupported here is what
    // produced the live INTERNAL WCL_TRIGGER_CC -> 0xe00002c7 failure.
    memcpy(cachedTriggerCC, snapshot, sizeof(*snapshot));
    cachedTriggerCCMode = mode;
    hasCachedTriggerCC = true;

    XYLog("DEBUG %s mode=%u trigger=%u channel_metric=%u\n",
          __FUNCTION__, mode, trigger, channelMetric);

    if (mode == 0 || mode == 1)
        return kIOReturnSuccess;
    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
setOS_FEATURE_FLAGS(apple80211_feature_flags *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const uint64_t flags = *reinterpret_cast<const uint64_t *>(data);

    // AppleBCMWLANCore::setOS_FEATURE_FLAGS is not an ack-only slot. It first
    // persists the incoming 64-bit word, then derives multiple cached booleans
    // and fans out follow-up configuration (DynSAR, 6G, KVR, AOP scan-forward,
    // adaptive 11r). We do not collapse that producer path back into an inline
    // success stub; the raw feature word must remain driver-owned state.
    cachedOSFeatureFlags = flags;

    XYLog("WCL [611] %s flags=0x%llx dynsar=%u six_ghz=%u adaptive11r=%u\n",
          __FUNCTION__,
          static_cast<unsigned long long>(flags),
          static_cast<unsigned int>((flags >> 6) & 1ULL),
          static_cast<unsigned int>((flags >> 7) & 1ULL),
          static_cast<unsigned int>((flags >> 28) & 1ULL));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple caches the first byte as a persistent bool instead of treating the
    // IOC as disposable. Carry the same state locally so later keepalive / PM
    // work does not lose the DHCP-renewal edge.
    cachedDhcpRenewalData = *reinterpret_cast<const uint8_t *>(data) != 0;
    XYLog("WCL [612] %s enabled=%u\n", __FUNCTION__,
          static_cast<unsigned int>(cachedDhcpRenewalData));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple stores the first dword and hands it into the battery-save path.
    // Even before the deeper power manager parity is finished, this IOC must
    // remain a state carrier rather than a blind success stub.
    cachedBatteryPowerSaveMode = *reinterpret_cast<const uint32_t *>(data);
    XYLog("WCL [613] %s mode=%u\n", __FUNCTION__, cachedBatteryPowerSaveMode);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWER_PROFILE(apple80211_power_profile *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore caches the first power-profile dword at +0x29f0 before
    // dispatching into its power-policy vtable. Preserve the same cached owner
    // state here instead of dropping the profile on the floor.
    cachedPowerProfile = *reinterpret_cast<const uint32_t *>(data);
    XYLog("WCL [619] %s profile=%u\n", __FUNCTION__, cachedPowerProfile);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV4_PARAMS(apple80211_ipv4_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv4 = reinterpret_cast<const tahoeIPv4ParamsContract *>(data);

    // The Apple producer persists IPv4/mask/router fields in core state,
    // triggers IPv4 notifications, and only then decides whether keepalive data
    // needs refreshing. Returning success without carrying the values breaks the
    // producer/consumer contract even if user-visible network behavior has not
    // reached that path yet.
    cachedIPv4Address = ipv4->address;
    cachedIPv4Netmask = ipv4->netmask;
    cachedIPv4Reserved = 0;
    cachedIPv4Gateway = ipv4->gateway;
    cachedIPv4GatewayTail = ipv4->gatewayTail;

    XYLog("WCL [624] %s addr=0x%08x mask=0x%08x gw=0x%08x tail=0x%04x keepalive=%u\n",
          __FUNCTION__, cachedIPv4Address, cachedIPv4Netmask, cachedIPv4Gateway,
          cachedIPv4GatewayTail,
          static_cast<unsigned int>(cachedIPv4Address != 0 && cachedIPv4Netmask != 0));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV6_PARAMS(apple80211_ipv6_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv6 = reinterpret_cast<const tahoeIPv6ParamsHeader *>(data);
    cachedIPv6Count = MIN(ipv6->count, static_cast<uint32_t>(10));
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    if (cachedIPv6Count != 0)
        memcpy(cachedIPv6Addresses, ipv6->addresses, cachedIPv6Count * sizeof(cachedIPv6Addresses[0]));

    // Apple seeds a dedicated link-local fe80:: prefix after refreshing the
    // cached IPv6 table. Keep that exact state edge so later consumers do not
    // observe "success" with no link-local cache behind it.
    cachedIPv6LinkLocalAddress[0] = 0xfe;
    cachedIPv6LinkLocalAddress[1] = 0x80;
    if (cachedIPv6Count != 0)
        memcpy(&cachedIPv6LinkLocalAddress[8], &cachedIPv6Addresses[0][8], 8);

    XYLog("WCL [636] %s count=%u first=%02x:%02x:%02x:%02x ll=%02x%02x::\n",
          __FUNCTION__, cachedIPv6Count,
          cachedIPv6Count ? cachedIPv6Addresses[0][0] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][1] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][2] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][3] : 0,
          cachedIPv6LinkLocalAddress[0], cachedIPv6LinkLocalAddress[1]);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setINFRA_ENUMERATED(apple80211_infra_enumerated *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple's contract here really is minimal: validate non-null and succeed.
    // Our old kIOReturnError path still diverged from that producer shape.
    cachedInfraEnumerated = true;
    XYLog("WCL [637] %s enumerated=1\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_REQ(apple80211ScanRequest *req)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(req);

    // Dump key fields from apple80211ScanRequest (total 0x1598 bytes)
    // Offsets from decompiled AppleBCMWLAN/IO80211Family
    uint32_t num_channels = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x54) : 0;
    uint32_t scan_type = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x44) : 0;
    uint32_t bss_type = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x14) : 0;
    uint32_t ssid_len = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x20) : 0;
    XYLog("DEBUG %s ic_state=%d req=%p scan_type=%u bss_type=%u ssid_len=%u num_channels=%u\n",
          __FUNCTION__, ic->ic_state, req, scan_type, bss_type, ssid_len, num_channels);

    // Dump first 96 bytes of struct for offset verification
    if (req) {
        XYLog("DEBUG %s hex[0x00-0x2F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw, 48));
        XYLog("DEBUG %s hex[0x30-0x5F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw + 0x30, 48));
    }

    if (!req)
        return kIOReturnBadArgument;
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    XYLog("DEBUG %s → scan triggered OK\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ASSOCIATE(apple80211AssocCandidates *candidates)
{
    RT2_SET(3); sRT.assocCount++;
    if (!candidates)
        return kIOReturnBadArgument;

    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(candidates);

    // Extract fields from apple80211AssocCandidates struct (offsets from decompiled AppleBCMWLAN)
    uint16_t ap_mode = *reinterpret_cast<const uint16_t *>(raw + 0x0C);
    uint32_t auth_lower = *reinterpret_cast<const uint32_t *>(raw + 0x10);
    uint32_t auth_upper = *reinterpret_cast<const uint32_t *>(raw + 0x14);
    uint32_t ssid_len = *reinterpret_cast<const uint32_t *>(raw + 0x1C);
    const uint8_t *ssid = raw + 0x20;
    uint16_t rsn_ie_len = *reinterpret_cast<const uint16_t *>(raw + 0xD4);
    const uint8_t *rsn_ie = raw + 0xD6;
    const struct ether_addr *bssid = reinterpret_cast<const struct ether_addr *>(raw + 0x1F4);

    if (ssid_len > APPLE80211_MAX_SSID_LEN)
        ssid_len = APPLE80211_MAX_SSID_LEN;

    char ssid_str[APPLE80211_MAX_SSID_LEN + 1];
    memcpy(ssid_str, ssid, ssid_len);
    ssid_str[ssid_len] = '\0';

    XYLog("DEBUG %s [%s] mode=%d auth_lower=%d auth_upper=%d rsn_ie_len=%d ic_state=%d\n",
          __FUNCTION__, ssid_str, ap_mode, auth_lower, auth_upper, rsn_ie_len, ic->ic_state);
    XYLog("DEBUG %s BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
          bssid->octet[0], bssid->octet[1], bssid->octet[2],
          bssid->octet[3], bssid->octet[4], bssid->octet[5]);

    // Dump first 64 bytes and BSSID region for offset verification
    XYLog("DEBUG %s hex[0x00-0x2F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw, 48));
    XYLog("DEBUG %s hex[0x1F0-0x21F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw + 0x1F0, 48));

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: already in ASSOC/AUTH ic_state=%d\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ap_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;

        struct apple80211_authtype_data auth_type_data;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = auth_upper;
        auth_type_data.authtype_lower = auth_lower;
        setAUTH_TYPE(&auth_type_data);

        if (rsn_ie_len > 0 && rsn_ie_len <= APPLE80211_MAX_RSN_IE_LEN) {
            struct apple80211_rsn_ie_data rsn_ie_data;
            rsn_ie_data.version = APPLE80211_VERSION;
            rsn_ie_data.len = rsn_ie[1] + 2;
            memcpy(rsn_ie_data.ie, rsn_ie, MIN(rsn_ie_data.len, (uint16_t)APPLE80211_MAX_RSN_IE_LEN));
            setRSN_IE(&rsn_ie_data);
        }

        associateSSID(const_cast<uint8_t *>(ssid), ssid_len, *bssid,
                      auth_lower, auth_upper, NULL, 0, 0);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LEAVE_NETWORK(apple80211_leave_network *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("%s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    if (ic->ic_state < IEEE80211_S_SCAN)
        return kIOReturnSuccess;

    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH)
        return kIOReturnSuccess;

    disassocIsVoluntary = true;

    ieee80211_del_ess(ic, nullptr, 0, 1);
    ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
    ic->ic_rsn_ie_override[1] = 0;
#endif
    ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
    ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_ABORT(void *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("%s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    // AppleBCMWLANCore::setWCL_SCAN_ABORT is not a no-op: it dispatches into
    // scan-adapter-owned abort work.  The recovered WCLScanManager FSM shows
    // why that matters: SCAN_ABORT_REQ moves IN_PROGRESS -> ABORTED, and the
    // owner only returns ABORTED -> IDLE when it later receives SCAN_COMPLETE.
    //
    // Our old Tahoe path only cleared net80211 flags and returned success, so
    // WCL stayed stuck in SCAN_MANAGER_STATE_ABORTED / IN_PROGRESS and every
    // later external SCAN_REQ hit the family ignore path (0xe00002bc / 16).
    // Cancel the local fake-scan timer and synthesize the single SCAN_DONE edge
    // that our backend otherwise never emits for abort completion.
    if (scanSource) {
        scanSource->cancelTimeout();
        scanSource->disable();
    }

    if (ic->ic_flags & IEEE80211_F_BGSCAN)
        ic->ic_flags &= ~IEEE80211_F_BGSCAN;
    if (ic->ic_flags & IEEE80211_F_ASCAN)
        ic->ic_flags &= ~IEEE80211_F_ASCAN;

    fNextNodeToSend = NULL;
    fScanResultWrapping = false;

    if (instance && instance->fNetIf) {
        static UInt32 abortScanStatus = 0;
        instance->postMessage(instance->fNetIf, APPLE80211_M_SCAN_DONE,
                              &abortScanStatus, sizeof(abortScanStatus), true);
    }

    return kIOReturnSuccess;
}

extern OSDictionary *convertScanToDictionary(apple80211_scan_result *a1);

static int convertNodeToScanResult(ItlHalService *fHalService, struct ieee80211_node *fNextNodeToSend, apple80211_scan_result *result)
{
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    if (fNextNodeToSend->ni_rsnie_tlv && fNextNodeToSend->ni_rsnie_tlv_len > 0) {
        result->asr_ie_len = fNextNodeToSend->ni_rsnie_tlv_len;
        memcpy(result->asr_ie_data, fNextNodeToSend->ni_rsnie_tlv, MIN(result->asr_ie_len, sizeof(result->asr_ie_data)));
    } else {
        result->asr_ie_len = 0;
    }
    result->asr_beacon_int = fNextNodeToSend->ni_intval;
    // Tahoe airportd candidate ingestion is sensitive to scan-result shape.
    // V16 left asr_rates empty because the loop iterated result->asr_nrates
    // immediately after bzero(), so nrates stayed 0 until after the copy.
    // That produces a malformed candidate even when the BSS is present in ic_tree.
    result->asr_nrates = MIN((uint8_t)fNextNodeToSend->ni_rates.rs_nrates,
                             (uint8_t)APPLE80211_MAX_RATES);
    for (uint8_t i = 0; i < result->asr_nrates; i++)
        result->asr_rates[i] = fNextNodeToSend->ni_rates.rs_rates[i];
    result->asr_age = (uint32_t)(airport_up_time() - fNextNodeToSend->ni_age_ts);
    result->asr_cap = fNextNodeToSend->ni_capinfo;
    result->asr_channel.version = APPLE80211_VERSION;
    result->asr_channel.channel = ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan);
    result->asr_channel.flags = ieeeChanFlag2appleScanFlagVentura(fNextNodeToSend->ni_chan->ic_flags);
    result->asr_noise = -fHalService->getDriverInfo()->getBSSNoise();
    result->asr_rssi = -(0 - IWM_MIN_DBM - fNextNodeToSend->ni_rssi);
    result->asr_snr = result->asr_rssi - result->asr_noise;
    memcpy(result->asr_bssid, fNextNodeToSend->ni_bssid, IEEE80211_ADDR_LEN);
    result->asr_ssid_len = fNextNodeToSend->ni_esslen;
    if (result->asr_ssid_len != 0)
        memcpy(&result->asr_ssid, fNextNodeToSend->ni_essid, result->asr_ssid_len);
    return 0;
}

IOReturn AirportItlwmSkywalkInterface::
getCURRENT_NETWORK(apple80211_scan_result *sr)
{
    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || fHalService->get80211Controller()->ic_bss == NULL)
        return kIOReturnError;
    convertNodeToScanResult(fHalService, fHalService->get80211Controller()->ic_bss, sr);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCOLOCATED_NETWORK_SCOPE_ID(apple80211_colocated_network_scope_id *as)
{
    if (!as)
        return kIOReturnBadArgument;

    // Apple/WCL expects a 0x30-byte buffer here, not the 12-byte stub we had
    // before.  WCLConfigManager validates bulletinBoardMessage length == 0x30
    // before calling into the driver.  The old declaration kept the trailing
    // bytes uninitialized and made our Tahoe ABI diverge from Apple.
    //
    // AppleBCMWLAN sets version=1 (not APPLE80211_VERSION), then fills two
    // IDs via a lower helper when colocated-scope state exists.  We do not
    // have that lower scope provider yet, so the correct non-fabricated state
    // is a zeroed payload with version=1 and zero IDs.
    bzero(as, sizeof(*as));
    as->version = 1;
    XYLog("%s version=%u id1=%u id2=%u\n", __FUNCTION__, as->version, as->id1, as->id2);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSCAN_RESULT(struct apple80211_scan_result *sr)
{
    RT2_SET(4); sRT.scanResCount++;
    if (fNextNodeToSend == NULL) {
        if (fScanResultWrapping) {
            fScanResultWrapping = false;
            XYLog("DEBUG %s wrapping done → 5\n", __FUNCTION__);
            return 5;
        } else {
            fNextNodeToSend = RB_MIN(ieee80211_tree, &fHalService->get80211Controller()->ic_tree);
            if (fNextNodeToSend == NULL) {
                XYLog("DEBUG %s no nodes → 5\n", __FUNCTION__);
                return 5;
            }
        }
    }
    XYLog("DEBUG %s ssid=%s rssi=%d ch=%d\n", __FUNCTION__,
          fNextNodeToSend->ni_essid, fNextNodeToSend->ni_rssi,
          fNextNodeToSend->ni_chan ? ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan) : -1);
    convertNodeToScanResult(fHalService, fNextNodeToSend, sr);
    
    fNextNodeToSend = RB_NEXT(ieee80211_tree, &HalService->get80211Controller()->ic_tree, fNextNodeToSend);
    if (fNextNodeToSend == NULL)
        fScanResultWrapping = true;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint64_t now = airport_up_time();
    uint32_t count = 0;
    struct ieee80211_node *ni;

    RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
        if (count >= APPLE80211_BGSCAN_MAX_NETWORKS)
            break;
        if (ni->ni_chan == NULL || ni->ni_chan == IEEE80211_CHAN_ANYC)
            continue;

        struct apple80211_bgscan_cached_network_entry *entry = &data->entries[count];
        memcpy(entry->bssid, ni->ni_bssid, 6);
        entry->channel = ieee80211_chan2ieee(ic, ni->ni_chan);
        entry->rssi = -(0 - IWM_MIN_DBM - ni->ni_rssi);
        entry->capability = ni->ni_capinfo;
        entry->ssid_crc = ether_crc32_le_update(0xFFFFFFFF,
                            (const u_int8_t *)ni->ni_essid, ni->ni_esslen);
        entry->age_ms = (uint32_t)(now - ni->ni_age_ts);
        count++;
    }

    data->count = count;
    data->timestamp = now;

    XYLog("%s count=%u timestamp=%llu\n", __FUNCTION__, count, now);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_CHANNELS_INFO(apple80211ChannelInfo *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint16_t count = 0;
    for (int i = 0; i < IEEE80211_CHAN_MAX && count < APPLE80211_WCL_MAX_CHANNELS; i++) {
        struct ieee80211_channel *ch = &ic->ic_channels[i];
        if (ch->ic_freq == 0)
            continue;

        struct apple80211_wcl_channel_entry *entry = &data->entries[count];
        entry->chan_spec = ieee80211_chan2ieee(ic, ch);
        entry->max_tx_power = 0;
        entry->min_tx_power = 0;
        entry->field4 = 0;
        entry->field5 = 0;
        entry->indoor = 0;

        uint8_t flags = 0;
        if (ch->ic_flags & IEEE80211_CHAN_PASSIVE)
            flags |= (1 << 2);  // bit2: passive
        if (ch->ic_flags & IEEE80211_CHAN_DFS)
            flags |= (1 << 1);  // bit1: radar/DFS
        if (IEEE80211_IS_CHAN_HT40(ch) || IEEE80211_IS_CHAN_VHT40(ch))
            flags |= (1 << 4);  // bit4: 40MHz
        if (IEEE80211_IS_CHAN_VHT80(ch))
            flags |= (1 << 5);  // bit5: 80MHz
        entry->flags = flags;
        count++;
    }

    data->num_channels = count;
    data->support_6e = 0;

    XYLog("WCL [530] %s num_channels=%u\n", __FUNCTION__, count);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BSS_INFO(apple80211_beacon_msg *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL) {
        XYLog("WCL [526] %s not associated ic_state=%d\n", __FUNCTION__, ic->ic_state);
        return kIOReturnError;
    }

    struct ieee80211_node *ni = ic->ic_bss;
    bzero(data, sizeof(*data));

    // Populate at known offsets — exact layout is reverse-engineered,
    // fields may shift. Log raw bytes for verification.
    uint8_t *buf = data->data;

    // +0x00: BSSID (6 bytes) — most beacon structs start with BSSID
    memcpy(buf + 0x00, ni->ni_bssid, 6);
    // +0x06: beacon interval (2 bytes LE)
    *(uint16_t *)(buf + 0x06) = ni->ni_intval;
    // +0x08: capability info (2 bytes LE)
    *(uint16_t *)(buf + 0x08) = ni->ni_capinfo;
    // +0x0A: channel number (2 bytes LE)
    *(uint16_t *)(buf + 0x0A) = ieee80211_chan2ieee(ic, ni->ni_chan);
    // +0x0C: RSSI (2 bytes LE, signed)
    *(int16_t *)(buf + 0x0C) = -(0 - IWM_MIN_DBM - ni->ni_rssi);
    // +0x0E: noise (2 bytes LE, signed)
    *(int16_t *)(buf + 0x0E) = -fHalService->getDriverInfo()->getBSSNoise();
    // +0x10: SSID length (1 byte)
    buf[0x10] = ni->ni_esslen;
    // +0x11: SSID (up to 32 bytes)
    if (ni->ni_esslen > 0)
        memcpy(buf + 0x11, ni->ni_essid, MIN(ni->ni_esslen, 32));
    // +0x7C: IE length (2 bytes LE)
    uint16_t ie_len = 0;
    if (ni->ni_rsnie_tlv && ni->ni_rsnie_tlv_len > 0)
        ie_len = MIN(ni->ni_rsnie_tlv_len, (uint16_t)(0x84 - 0x7E));
    *(uint16_t *)(buf + 0x7C) = ie_len;
    // +0x7E: IE data (remaining bytes)
    if (ie_len > 0)
        memcpy(buf + 0x7E, ni->ni_rsnie_tlv, ie_len);

    XYLog("WCL [526] %s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rssi=%d ssid_len=%u ie_len=%u\n",
          __FUNCTION__,
          ni->ni_bssid[0], ni->ni_bssid[1], ni->ni_bssid[2],
          ni->ni_bssid[3], ni->ni_bssid[4], ni->ni_bssid[5],
          ieee80211_chan2ieee(ic, ni->ni_chan),
          -(0 - IWM_MIN_DBM - ni->ni_rssi), ni->ni_esslen, ie_len);
    // Dump first 32 bytes for offset verification
    XYLog("WCL [526] hex[0x00-0x1F]: %s\n", hexdump(buf, 32));
    return kIOReturnSuccess;
}
