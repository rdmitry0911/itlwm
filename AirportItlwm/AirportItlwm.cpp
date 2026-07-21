/*
* Copyright (C) 2020  钟先耀
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/
#include "AirportItlwm.hpp"
#include "TahoeAssociationAuthContracts.hpp"

#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(CTimeout, OSObject)

static_assert(TahoeAssociationAuthContracts::kAuthWpa2Psk ==
                  APPLE80211_AUTHTYPE_WPA2_PSK,
              "association auth contract must match Apple80211 WPA2 PSK bit");
static_assert(TahoeAssociationAuthContracts::kAuthWpa3Sae ==
                  APPLE80211_AUTHTYPE_WPA3_SAE,
              "association auth contract must match Apple80211 WPA3 SAE bit");

IO80211WorkLoop *_fWorkloop;
IOCommandGate *_fCommandGate;

bool AirportItlwm::init(OSDictionary *properties)
{
    bool ret = super::init(properties);
    fWatchdogStopping = false;
    awdlSyncEnable = true;
    power_state = 0;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    XYLog("DEBUG %s power_state=%u ret=%d\n", __FUNCTION__, power_state, ret);
    return ret;
}

#define  PCI_MSI_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSI        0x05    /* Message Signalled Interrupts */
#define  PCI_MSIX_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSIX    0x11    /* MSI-X */
#define  PCI_MSIX_FLAGS_ENABLE    0x8000    /* MSI-X enable */
#define  PCI_MSI_FLAGS_ENABLE    0x0001    /* MSI feature enabled */

static void pciMsiSetEnable(IOPCIDevice *device, UInt8 msiCap, int enable)
{
    u16 control;
    
    control = device->configRead16(msiCap + PCI_MSI_FLAGS);
    control &= ~PCI_MSI_FLAGS_ENABLE;
    if (enable)
        control |= PCI_MSI_FLAGS_ENABLE;
    device->configWrite16(msiCap + PCI_MSI_FLAGS, control);
}

static void pciMsiXClearAndSet(IOPCIDevice *device, UInt8 msixCap, UInt16 clear, UInt16 set)
{
    u16 ctrl;
    
    ctrl = device->configRead16(msixCap + PCI_MSIX_FLAGS);
    ctrl &= ~clear;
    ctrl |= set;
    device->configWrite16(msixCap + PCI_MSIX_FLAGS, ctrl);
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    bool isMatch = false;
    super::probe(provider, score);
    UInt8 msiCap;
    UInt8 msixCap;
    IOPCIDevice* device = OSDynamicCast(IOPCIDevice, provider);
    if (!device)
        return NULL;
    if (ItlIwx::iwx_match(device)) {
        isMatch = true;
        fHalService = new ItlIwx;
    }
    if (!isMatch && ItlIwm::iwm_match(device)) {
        isMatch = true;
        fHalService = new ItlIwm;
    }
    if (!isMatch && ItlIwn::iwn_match(device)) {
        isMatch = true;
        fHalService = new ItlIwn;
    }
    if (isMatch) {
        device->findPCICapability(PCI_CAP_ID_MSIX, &msixCap);
        if (msixCap)
            pciMsiXClearAndSet(device, msixCap, PCI_MSIX_FLAGS_ENABLE, 0);
        device->findPCICapability(PCI_CAP_ID_MSI, &msiCap);
        if (msiCap)
            pciMsiSetEnable(device, msiCap, 1);
        if (!msiCap && !msixCap) {
            XYLog("%s No MSI cap\n", __FUNCTION__);
            fHalService->release();
            fHalService = NULL;
            return NULL;
        }
        return this;
    }
    return NULL;
}

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }
    
    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    fpNetStats->collisions = 0;
#ifdef __PRIVATE_SPI__
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif
    
    return true;
}

IONetworkInterface *AirportItlwm::createInterface()
{
    AirportItlwmInterface *netif = new AirportItlwmInterface;
    if (!netif)
        return NULL;
    if (!netif->init(this, fHalService)) {
        netif->release();
        return NULL;
    }
    return netif;
}

IOReturn AirportItlwm::associateSSID(uint8_t *ssid, uint32_t ssid_len, const struct ether_addr &bssid, uint32_t authtype_lower, uint32_t authtype_upper, uint8_t *key, uint32_t key_len, int key_index)
{
    if (TahoeAssociationAuthContracts::requiresUnsupportedWpa3Auth(
            authtype_upper)) {
        XYLog("associateSSID REJECT_WPA3_NO_FALLBACK auth_upper=0x%x\n",
              authtype_upper);
        return kIOReturnUnsupported;
    }

    struct ieee80211com *ic = fHalService->get80211Controller();
    
    ieee80211_disable_rsn(ic);
    ieee80211_disable_wep(ic);
    
    struct ieee80211_wpaparams	 wpa;
    struct ieee80211_nwkey		 nwkey;
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

    const uint32_t localAuthUpper =
        TahoeAssociationAuthContracts::localAuthMaskWithoutFallbackRewrite(
            authtype_upper);
    if (TahoeAssociationAuthContracts::usesLocalWpaProtocol(localAuthUpper)) {
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    }
    
    if (TahoeAssociationAuthContracts::usesLocalPskAkm(localAuthUpper)) {
        if (TahoeAssociationAuthContracts::usesLocalLegacyPskAkm(
                localAuthUpper))
            wpa.i_akms |= IEEE80211_WPA_AKM_PSK;
        if (TahoeAssociationAuthContracts::usesLocalSha256PskAkm(
                localAuthUpper))
            wpa.i_akms |= IEEE80211_WPA_AKM_SHA256_PSK;
        wpa.i_enabled = 1;
        memcpy(ic->ic_psk, key, sizeof(ic->ic_psk));
        ic->ic_flags |= IEEE80211_F_PSK;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (TahoeAssociationAuthContracts::usesLocalEnterpriseAkm(localAuthUpper)) {
        wpa.i_akms |= IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_8021X;	
        wpa.i_enabled = 1;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    
    if (authtype_lower == APPLE80211_AUTHTYPE_SHARED) {
        XYLog("shared key authentication is not supported!\n");
        return kIOReturnUnsupported;
    }
    
    if (authtype_upper == APPLE80211_AUTHTYPE_NONE && authtype_lower == APPLE80211_AUTHTYPE_OPEN) { // Open or WEP Open System
        if (key_len > 0) {
            nwkey.i_wepon = IEEE80211_NWKEY_WEP;
            nwkey.i_defkid = key_index + 1;
            nwkey.i_key[key_index].i_keylen = (int)key_len;
            nwkey.i_key[key_index].i_keydat = key;
            ieee80211_ioctl_setnwkeys(ic, &nwkey);
        }
    }

    return kIOReturnSuccess;
}

void AirportItlwm::setPTK(const u_int8_t *key, size_t key_len) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node	* ni = ic->ic_bss;
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
        ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
        ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
        ni->ni_flags |= IEEE80211_NODE_RXPROT;
    } else if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
        XYLog("%s: unexpected pairwise key update received from %s\n",
              ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));
}

void AirportItlwm::setGTK(const u_int8_t *gtk, size_t key_len, u_int8_t kid, u_int8_t *rsc) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node	* ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;

    /* Slots 4 and 5 are IGTKs. This legacy GTK ingress may not overwrite
     * protected-management state even if a caller bypasses its outer gate. */
    if (kid >= IEEE80211_WEP_NKID) {
        XYLog("%s: refusing non-GTK key index %u\n", __FUNCTION__, kid);
        return;
    }
    
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
        }
    }
    
    if (true) {
        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_opmode != IEEE80211_M_IBSS ||
            ++ni->ni_key_count == 2)
#endif
        {
            ni->ni_port_valid = 1;
            ieee80211_set_link_state(ic, LINK_STATE_UP);
            ni->ni_assoc_fail = 0;
            if (ic->ic_opmode == IEEE80211_M_STA)
                ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
        }
    }
}


bool AirportItlwm::
createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;
    
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(0x80, 11000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result)
        XYLog("Cannot publish medium dictionary!\n");

    mediumDict->release();
    return result;
}

bool AirportItlwm::start(IOService *provider)
{
    int boot_value = 0;
    if (!super::start(provider)) {
        return false;
    }
    if (!serviceMatching("AppleSMC")) {
        super::stop(provider);
        XYLog("No matching AppleSMC dictionary, failing\n");
        return false;
    }
    pciNub = OSDynamicCast(IOPCIDevice, provider);
    if (!pciNub) {
        super::stop(provider);
        return false;
    }
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        super::stop(provider);
        return false;
    }
    if (initPCIPowerManagment(pciNub) == false) {
        super::stop(pciNub);
        return false;
    }
    if (_fWorkloop == NULL) {
        XYLog("No _fWorkloop!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("No command gate!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fWorkloop->addEventSource(_fCommandGate);
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("setup medium fail\n");
        releaseAll();
        return false;
    }
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;
    
    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;
    
    if (!fHalService->attach(pciNub)) {
        XYLog("attach fail\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    if (!attachInterface((IONetworkInterface **)&fNetIf, true)) {
        XYLog("attach to interface fail\n");
        stopWatchdogAndDrain();
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("init watchdog workloop fail\n");
        stopWatchdogAndDrain();
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("init watchdog fail\n");
        stopWatchdogAndDrain();
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogStopping = false;
    if (fWatchdogWorkLoop->addEventSource(watchdogTimer) != kIOReturnSuccess) {
        XYLog("add watchdog event source fail\n");
        stopWatchdogAndDrain();
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
    _fWorkloop->addEventSource(scanSource);
    scanSource->enable();
    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    _fCommandGate->enable();
    power_state = 1;
    XYLog("DEBUG %s enabling adapter in start, power_state=%u\n", __FUNCTION__, power_state);
    enableAdapter(fNetIf);
    registerService();
    fNetIf->registerService();
    XYLog("DEBUG %s start complete\n", __FUNCTION__);
    return true;
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    // stopWatchdogAndDrain() marks permanent teardown before the source is
    // synchronously removed from fWatchdogWorkLoop.  The flag prevents an
    // in-flight action from touching HAL state or re-arming itself.
    if (fWatchdogStopping || timer == NULL || timer != watchdogTimer)
        return;

    ItlHalService *hal = fHalService;
    if (!hal)
        return;

    struct _ifnet *ifp = &hal->get80211Controller()->ic_ac.ac_if;
    if (ifp->if_watchdog)
        (*ifp->if_watchdog)(ifp);

    if (!fWatchdogStopping && timer == watchdogTimer)
        timer->setTimeoutMS(kWatchDogTimerPeriod);
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    AirportItlwm *that = (AirportItlwm *)owner;
    that->getNetworkInterface()->postMessage(APPLE80211_M_SCAN_DONE);
}

const OSString * AirportItlwm::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * AirportItlwm::newModelString() const
{
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

bool AirportItlwm::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold)
            magicPacketSupported = true;
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

bool AirportItlwm::createWorkLoop()
{
    _fWorkloop = IO80211WorkLoop::workLoop();
    return _fWorkloop != 0;
}

IOWorkLoop *AirportItlwm::getWorkLoop() const
{
    return _fWorkloop;
}

IOReturn AirportItlwm::selectMedium(const IONetworkMedium *medium)
{
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

void AirportItlwm::stop(IOService *provider)
{
    XYLog("%s\n", __FUNCTION__);
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    disableAdapter(fNetIf);
    setLinkStatus(kIONetworkLinkValid);
    stopWatchdogAndDrain();
    fHalService->detach(pciNub);
    ether_ifdetach(ifp);
    detachInterface(fNetIf, true);
    OSSafeReleaseNULL(fNetIf);
    releaseAll();
    super::stop(provider);
}

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    if (status == currentStatus) {
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#ifdef __PRIVATE_SPI__
            fNetIf->startOutputThread();
#endif
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkUp, (void *)0);
            fNetIf->setLinkQualityMetric(100);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#ifdef __PRIVATE_SPI__
            fNetIf->stopOutputThread();
            fNetIf->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkDown, (void *)fHalService->get80211Controller()->ic_deauth_reason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    IOReturn ret = that->getNetworkInterface()->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
    if (that->fAWDLInterface) {
#if __IO80211_TARGET >= __MAC_13_0
        that->fAWDLInterface->setEnabledBySystem(true);
#endif
        that->fAWDLInterface->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
    }
    return ret;
}

void AirportItlwm::stopWatchdogAndDrain()
{
    stopScanSourceAndDrain();

    // IOWorkLoop::removeEventSource() waits until the workloop acknowledges
    // removal.  After the stopping flag prevents re-arm, this is the lifetime
    // fence required before fHalService is detached or released.
    fWatchdogStopping = true;

    IOTimerEventSource *timer = watchdogTimer;
    if (!timer)
        return;

    timer->cancelTimeout();
    timer->disable();
    if (fWatchdogWorkLoop)
        fWatchdogWorkLoop->removeEventSource(timer);
    watchdogTimer = NULL;
    timer->release();
}

void AirportItlwm::stopScanSourceAndDrain()
{
    IOTimerEventSource *source = scanSource;
    if (!source)
        return;

    // fakeScanDone() posts through fNetIf, so its _fWorkloop source must be
    // synchronously removed before the interface/HAL lifetime ends.
    source->cancelTimeout();
    source->disable();
    if (_fWorkloop)
        _fWorkloop->removeEventSource(source);
    scanSource = NULL;
    source->release();
}

void AirportItlwm::releaseAll()
{
    stopWatchdogAndDrain();
    if (fWatchdogWorkLoop) {
        fWatchdogWorkLoop->release();
        fWatchdogWorkLoop = NULL;
    }
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
//            _fCommandGate->disable();
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    unregistPM();
}

void AirportItlwm::free()
{
    XYLog("%s\n", __FUNCTION__);
    stopWatchdogAndDrain();
    if (fWatchdogWorkLoop != NULL) {
        fWatchdogWorkLoop->release();
        fWatchdogWorkLoop = NULL;
    }
    if (fHalService != NULL) {
        fHalService->release();
        fHalService = NULL;
    }
    if (syncFrameTemplate != NULL && syncFrameTemplateLength > 0) {
        IOFree(syncFrameTemplate, syncFrameTemplateLength);
        syncFrameTemplateLength = 0;
        syncFrameTemplate = NULL;
    }
    if (roamProfile != NULL) {
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
        roamProfile = NULL;
    }
    if (btcProfile != NULL) {
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
        btcProfile = NULL;
    }
    super::free();
}

IOReturn AirportItlwm::enable(IONetworkInterface *netif)
{
    XYLog("DEBUG %s power_state=%u netif=%p\n", __PRETTY_FUNCTION__, power_state, netif);
    super::enable(netif);
    _fCommandGate->enable();
    if (power_state)
        enableAdapter(netif);
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::disable(IONetworkInterface *netif)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u\n", __FUNCTION__, netif, power_state, pmPowerState);
    if (!fHalService) {
        XYLog("DEBUG %s ABORT: fHalService is NULL\n", __FUNCTION__);
        return kIOReturnNotReady;
    }
    fHalService->enable(netif);
    if (!fWatchdogStopping && watchdogTimer) {
        watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
        watchdogTimer->enable();
    }
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u\n", __FUNCTION__, netif, power_state, pmPowerState);
    if (watchdogTimer) {
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
    }
    if (fHalService)
        fHalService->disable(netif);
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const IOEthernetAddress *addrP)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, addrP->bytes);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
        fHalService->disable(fNetIf);
        fHalService->enable(fNetIf);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::getHardwareAddressForInterface(
                                               IO80211Interface *netif, IOEthernetAddress *addr)
{
    return getHardwareAddress(addr);
}

#ifdef __PRIVATE_SPI__
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}
#endif

UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
//    XYLog("%s\n", __FUNCTION__);
    IOReturn ret = kIOReturnOutputSuccess;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    return ret;
}

UInt32 AirportItlwm::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

SInt32 AirportItlwm::monitorModeSetEnabled(
                                    IO80211Interface *interface, bool enabled, UInt32 dlt)
{
    return kIOReturnSuccess;
}

bool AirportItlwm::
useAppleRSNSupplicant(IO80211Interface *interface)
{
#ifdef USE_APPLE_SUPPLICANT
    return true;
#else
    return false;
#endif
}

IOReturn AirportItlwm::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported)
        *filters = kIOEthernetWakeOnMagicPacket;
    else if (group == gIONetworkFilterGroup)
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    else
        rtn = IOEthernetController::getPacketFilters(group, filters);
    return rtn;
}

IOReturn AirportItlwm::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    AirportItlwm* dev = OSDynamicCast(AirportItlwm, owner);
    if (dev == 0)
        return kIOReturnError;
    
    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

static IOPMPowerState powerStateArray[kPowerStateCount] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void AirportItlwm::unregistPM()
{
    if (powerOffThreadCall) {
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    IOReturn result = IOPMAckImplied;
    XYLog("DEBUG %s ordinal=%lu pmPowerState=%u power_state=%u\n", __FUNCTION__, powerStateOrdinal, pmPowerState, power_state);
    if (pmPowerState == powerStateOrdinal) {
        XYLog("DEBUG %s SKIPPED (already in state %lu)\n", __FUNCTION__, powerStateOrdinal);
        return result;
    }
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall))
                    release();
                result = 5000000;
            }
            break;
        case kPowerStateOn:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall))
                    release();
                result = 5000000;
            }
            break;
            
        default:
            break;
    }
    return result;
}

unsigned long AirportItlwm::initialPowerStateForDomainState(IOPMPowerFlags domainState)
{
    if ((domainState >> 9) & 1)
        return kPowerStateOff;
    return (domainState >> 1) & 1;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOff(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOff,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *) param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOn,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;
    
    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;
    
    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOff,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOn,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArray,
                                             kPowerStateCount);
    if (ret == kIOReturnSuccess) {
        changePowerStateToPriv(kPowerStateOn);
        changePowerStateTo(kPowerStateOff);
    }
    return ret;
}

void AirportItlwm::setPowerStateOff()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOff;
    disableAdapter(fNetIf);
    pmPolicyMaker->acknowledgeSetPowerState();
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOn;
    if (power_state)
        enableAdapter(fNetIf);
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    pmPolicyMaker->acknowledgeSetPowerState();
}

int AirportItlwm::
outputRaw80211Packet(IO80211Interface *interface, mbuf_t m)
{
    XYLog("%s len=%zu\n", __FUNCTION__, mbuf_len(m));
    freePacket(m);
    return kIOReturnOutputDropped;
}

UInt32 AirportItlwm::
hardwareOutputQueueDepth(IO80211Interface *interface)
{
    return 0;
}

SInt32 AirportItlwm::
performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op)
{
    return 0;
}

SInt32 AirportItlwm::
stopDMA()
{
    if (fNetIf)
        disable(fNetIf);
    return 0;
}

SInt32 AirportItlwm::
enableFeature(IO80211FeatureCode code, void *data)
{
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

int AirportItlwm::
outputActionFrame(OSObject *object, mbuf_t m)
{
    XYLog("%s len=%zu\n", __FUNCTION__, mbuf_len(m));
    mbuf_freem(m);
    // This controller has no backend action-frame injector.  Do not report
    // success after dropping a frame the hardware never received.
    return kIOReturnOutputDropped;
}

int AirportItlwm::
bpfOutput80211Radio(OSObject *object, mbuf_t m)
{
    XYLog("%s len=%zu\n", __FUNCTION__, mbuf_len(m));
    mbuf_freem(m);
    // Raw 802.11/radiotap injection is likewise not backed by this driver.
    return kIOReturnOutputDropped;
}

SInt32 AirportItlwm::
enableVirtualInterface(IO80211VirtualInterface *interface)
{
    XYLog("%s interface=%s role=%d\n", __FUNCTION__, interface->getBSDName(), interface->getInterfaceRole());
    SInt32 ret = super::enableVirtualInterface(interface);
    if (!ret) {
#if __IO80211_TARGET >= __MAC_13_0
        interface->setEnabledBySystem(true);
#endif
        interface->setLinkState(kIO80211NetworkLinkUp, 0);
        interface->postMessage(APPLE80211_M_LINK_CHANGED);
        return kIOReturnSuccess;
    }
    return ret;
}

SInt32 AirportItlwm::
disableVirtualInterface(IO80211VirtualInterface *interface)
{
    XYLog("%s interface=%s role=%d\n", __FUNCTION__, interface->getBSDName(), interface->getInterfaceRole());
    SInt32 ret = super::disableVirtualInterface(interface);
    if (!ret) {
        interface->setLinkState(kIO80211NetworkLinkDown, 0);
        interface->postMessage(APPLE80211_M_LINK_CHANGED);
        return kIOReturnSuccess;
    }
    return ret;
}

IO80211VirtualInterface *AirportItlwm::
createVirtualInterface(ether_addr *ether, UInt role)
{
    if (role - 1 > 3)
        return super::createVirtualInterface(ether, role);
    IO80211VirtualInterface *inf = new IO80211VirtualInterface;
    if (inf) {
        if (inf->init(this, ether, role, role == APPLE80211_VIF_AWDL ? "awdl" : "p2p"))
            XYLog("%s role=%d succeed\n", __FUNCTION__, role);
        else {
            inf->release();
            return NULL;
        }
    }
    return inf;
}

int AirportItlwm::
bpfOutputPacket(OSObject *object, UInt dltType, mbuf_t m)
{
    XYLog("%s dltType=%d\n", __FUNCTION__, dltType);
    if (dltType == DLT_IEEE802_11_RADIO || dltType == DLT_IEEE802_11)
        return bpfOutput80211Radio(object, m);
    if (dltType == DLT_RAW)
        return outputActionFrame(object, m);
    mbuf_freem(m);
    return 1;
}

void AirportItlwm::
requestPacketTx(void *object, UInt )
{
    UInt32 ret;
    struct TxPacketRequest request;
    if (object == NULL)
        return;
    IO80211VirtualInterface *interface = OSDynamicCast(IO80211VirtualInterface, (OSObject *)object);
    if (interface) {
        memset(&request, 0, sizeof(request));
        if (interface->getInterfaceRole() == APPLE80211_VIF_AWDL) {
//            interface->dequeueTxPackets(&request);
//
//            ret = outputPacket(NULL, interface);
//            if (ret == kIOReturnSuccess) {
//                interface->reportTransmitStatus(NULL, ret, NULL);
//            }
        }
    }
}
