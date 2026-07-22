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

#ifndef ItlHalService_hpp
#define ItlHalService_hpp

#include <libkern/c++/OSObject.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <HAL/ItlSaeAuthTransportV1.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>

#include "ItlDriverInfo.hpp"
#include "ItlDriverController.hpp"

#include <net80211/ieee80211_var.h>

/*
 * AP/GO HAL parameter shapes.
 *
 * These structs are the canonical local representation of the
 * recovered Apple AppleBCMWLAN AP/GO firmware command parameters.
 * They are owned by the host APSTA owner / net80211 HostAP layer and
 * are passed by const pointer into the HAL so the lower backend
 * (Intel iwx/iwm + firmware) can translate them into device-specific
 * commands when AP/GO support is implemented. Each pointer field is
 * borrowed for the duration of the call; the HAL must not retain
 * pointers across returns.
 */

struct ItlHalApConfig {
    uint8_t  bssid[IEEE80211_ADDR_LEN];
    uint16_t channel;
    uint16_t beaconInterval;
    uint8_t  dtimPeriod;
    uint32_t maxStations;
    const void *beaconTemplate;
    size_t beaconTemplateLength;
};

struct ItlHalApKey {
    const uint8_t *station;
    uint8_t keyIndex;
    uint8_t cipher;
    const void *keyData;
    size_t keyLength;
    const void *rsc;
    size_t rscLength;
};

struct ItlHalApCSA {
    uint16_t channel;
    uint8_t count;
};

struct ItlHalApSoftAPParams {
    uint32_t param04;
    uint32_t param08;
    uint32_t param0c;
    uint32_t param10;
    uint16_t beaconInterval;
    uint8_t mode;
    uint8_t enabled;
    uint8_t param18;
};

struct ItlHalApWifiNetworkInfo {
    const void *ieBytes;
    size_t ieLength;
};

struct ItlHalApRSNConfig {
    const uint32_t *pairwiseCipherList;
    uint32_t pairwiseCipherCount;
    const uint32_t *groupCipherList;
    uint32_t groupCipherCount;
    const uint32_t *pairwiseVersionList;
    uint32_t pairwiseVersionCount;
    const uint32_t *groupVersionList;
    uint32_t groupVersionCount;
    uint32_t cipherMask;
    uint32_t authMask;
    uint16_t mfp;
};

struct ItlHalApStationCommand {
    uint32_t command;
    const uint8_t *station;
    uint32_t flags;
    uint32_t disassocReason;
    uint32_t disassocCarrierValue08;
    uint16_t disassocCarrierValue0c;
    uint32_t disassocPayloadReason00;
    uint32_t disassocPayloadValue04;
    uint16_t disassocPayloadValue08;
    uint16_t disassocPayloadSentinel0a;
};

struct ItlHalApStaIEQuery {
    const uint8_t *station;
    uint32_t requestedLength;
    uint8_t *output;
    size_t outputCapacity;
};

struct ItlHalApStaStatsQuery {
    const uint8_t *station;
    uint32_t field0c;
    uint32_t field10;
    uint32_t field14;
    uint32_t field18;
};

struct ItlHalApKeyRscQuery {
    uint16_t keyIndex;
    uint8_t *rsc;
    size_t rscLength;
};

class ItlHalService : public OSObject {
    OSDeclareAbstractStructors(ItlHalService)

public:

    virtual bool attach(IOPCIDevice *device) = 0;

    virtual void detach(IOPCIDevice *device) = 0;

    virtual IOReturn enable(IONetworkInterface *interface) = 0;

    virtual IOReturn disable(IONetworkInterface *interface) = 0;

    virtual struct ieee80211com *get80211Controller() = 0;

    virtual ItlDriverInfo *getDriverInfo() = 0;

    virtual ItlDriverController *getDriverController() = 0;

    /*
     * AP/GO HAL surface.
     *
     * The default implementations are deliberately fail-closed: a
     * concrete HAL backend can own STA-mode lifetime without
     * advertising AP/GO firmware support, and host-side AP/APSTA code
     * (the host APSTA owner skeleton, OpenBSD net80211 HostAP) can
     * query the HAL through this surface instead of the backend
     * type. supportsAPMode() returns false until a backend overrides
     * it with a positive AP/GO firmware capability check; the command
     * methods return kIOReturnUnsupported until the backend overrides
     * them with the corresponding firmware command path.
     *
     * A backend that returns true from supportsAPMode() must also
     * implement startAPMode/stopAPMode and at least one of the
     * command methods needed by the AP/GO bring-up flow it advertises
     * (beacon update, AP key install, CSA, station add/remove,
     * station IE/stat/RSC queries). It must remain re-entry-safe:
     * calling stopAPMode() while AP mode is not started must succeed
     * without side effects.
     *
     * All parameter pointers are borrowed for the duration of the
     * call. The HAL must not retain pointers, must not free them, and
     * must complete any necessary copy before returning.
     */
    virtual bool supportsAPMode() const { return false; }
    virtual IOReturn startAPMode(const struct ItlHalApConfig *config) {
        (void)config;
        return kIOReturnUnsupported;
    }
    virtual IOReturn stopAPMode() { return kIOReturnUnsupported; }
    virtual IOReturn updateAPBeacon(const void *templateBytes,
                                    size_t templateLength,
                                    uint16_t beaconInterval,
                                    uint8_t dtimPeriod) {
        (void)templateBytes;
        (void)templateLength;
        (void)beaconInterval;
        (void)dtimPeriod;
        return kIOReturnUnsupported;
    }
    virtual IOReturn setAPKey(const struct ItlHalApKey *key) {
        (void)key;
        return kIOReturnUnsupported;
    }
    virtual IOReturn triggerAPCSA(const struct ItlHalApCSA *csa) {
        (void)csa;
        return kIOReturnUnsupported;
    }
    virtual IOReturn setAPHidden(bool hidden) {
        (void)hidden;
        return kIOReturnUnsupported;
    }
    virtual IOReturn setAPSoftAPParams(const struct ItlHalApSoftAPParams *params) {
        (void)params;
        return kIOReturnUnsupported;
    }
    virtual IOReturn setAPWifiNetworkInfo(const struct ItlHalApWifiNetworkInfo *info) {
        (void)info;
        return kIOReturnUnsupported;
    }
    virtual IOReturn setAPRSNConfig(const struct ItlHalApRSNConfig *config) {
        (void)config;
        return kIOReturnUnsupported;
    }
    virtual IOReturn sendAPStationCommand(const struct ItlHalApStationCommand *cmd) {
        (void)cmd;
        return kIOReturnUnsupported;
    }
    virtual IOReturn getAPStationIE(const struct ItlHalApStaIEQuery *query) {
        (void)query;
        return kIOReturnUnsupported;
    }
    virtual IOReturn getAPStationStats(struct ItlHalApStaStatsQuery *query) {
        (void)query;
        return kIOReturnUnsupported;
    }
    virtual IOReturn getAPKeyRSC(const struct ItlHalApKeyRscQuery *query) {
        (void)query;
        return kIOReturnUnsupported;
    }

    /*
     * Internal SAE Algorithm-3 TX ownership surface.
     *
     * This is intentionally not a generic raw-management injection API. A
     * backend must copy the fixed public request before returning, retain at
     * most one ticketed request, and report terminal completion only through
     * the bounded net80211 transport event. The default preserves the
     * fail-closed behaviour of HALs without the AX211 implementation.
     */
    virtual IOReturn submitSaeAuthFrame(
        const struct ItlSaeAuthTxRequestV1 *request) {
        (void)request;
        return kIOReturnUnsupported;
    }
    virtual void cancelSaeAuthFrame(uint64_t ticket) {
        (void)ticket;
    }

    virtual void free() override;

public:
    virtual bool initWithController(IOEthernetController *controller, IOWorkLoop *workloop, IOCommandGate *commandGate);
    
protected:
    
    /* Completion paths may test their predicate before sleeping under this mutex. */
    void lockTsleep();
    void unlockTsleep();
    int tsleep_nsec_locked(void *ident, int priority, const char *wmesg,
                           uint64_t timo);
    int tsleep_nsec(void *ident, int priority, const char *wmesg, uint64_t timo);
    
    void wakeupOn(void* ident);
    
    IOEthernetController *getController();
    
    IOCommandGate *getMainCommandGate();
    
    IOWorkLoop *getMainWorkLoop();
    
private:
    IOEthernetController *controller;
    IOCommandGate *mainCommandGate;
    IOWorkLoop *mainWorkLoop;

    lck_grp_t *inner_gp;
    lck_grp_attr_t *inner_gp_attr;
    lck_attr_t *inner_attr;
    lck_mtx_t *inner_lock;
};

#endif /* ItlHalService_hpp */
