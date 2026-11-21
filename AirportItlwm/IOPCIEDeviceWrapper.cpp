//
//  IOPCIEDeviceWrapper.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "IOPCIEDeviceWrapper.hpp"
#include "Apple80211.h"

#include "ItlIwm.hpp"
#include "ItlIwx.hpp"
#include "ItlIwn.hpp"
/* ItlVirt.cpp is built into the Tahoe target only. */
#if __IO80211_TARGET >= __MAC_26_0
#include "ItlVirt.hpp"
#endif

#define super IOService
OSDefineMetaClassAndStructors(IOPCIEDeviceWrapper, IOService);

#define  PCI_MSI_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSI        0x05    /* Message Signalled Interrupts */
#define  PCI_MSIX_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSIX    0x11    /* MSI-X */
#define  PCI_MSIX_FLAGS_ENABLE    0x8000    /* MSI-X enable */
#define  PCI_MSI_FLAGS_ENABLE    0x0001    /* MSI feature enabled */

static IOPMPowerState powerStateArray[2] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

static void pciMsiSetEnable(IOPCIDevice *device, UInt8 msiCap, int enable)
{
    UInt16 control;
    
    control = device->configRead16(msiCap + PCI_MSI_FLAGS);
    control &= ~PCI_MSI_FLAGS_ENABLE;
    if (enable)
        control |= PCI_MSI_FLAGS_ENABLE;
    device->configWrite16(msiCap + PCI_MSI_FLAGS, control);
}

static void pciMsiXClearAndSet(IOPCIDevice *device, UInt8 msixCap, UInt16 clear, UInt16 set)
{
    UInt16 ctrl;
    
    ctrl = device->configRead16(msixCap + PCI_MSIX_FLAGS);
    ctrl &= ~clear;
    ctrl |= set;
    device->configWrite16(msixCap + PCI_MSIX_FLAGS, ctrl);
}

extern IOWorkLoop *_fWorkloop;
extern volatile bool gAirportItlwmControllerRetired;

IOWorkLoop *IOPCIEDeviceWrapper::getWorkLoop() const
{
    return _fWorkloop;
}

IOService* IOPCIEDeviceWrapper::
probe(IOService *provider, SInt32 *score)
{
    bool isMatch = false;
    super::probe(provider, score);
    UInt8 msiCap;
    UInt8 msixCap;
    IOPCIDevice* device = OSDynamicCast(IOPCIDevice, provider);
    if (!device)
        return NULL;
    /*
     * IO80211Family keeps one controller per boot: after a hot-unplug, airportd
     * keeps the first controller's interfaces and the Wi-Fi control layer's
     * state machines (IO80211Family statics) never run for a second one, and
     * its teardown hangs (evidence/2026-11-21-pve-hotplug.md). The reference
     * Wi-Fi is never removed. A device plugged again in the same boot is left
     * without a driver, so a later unplug needs no teardown; Wi-Fi comes back
     * after a guest restart.
     */
    if (gAirportItlwmControllerRetired) {
        XYLog("%s: a Wi-Fi controller was already removed in this boot; "
              "restart macOS to use this device\n", __FUNCTION__);
        return NULL;
    }
#if __IO80211_TARGET >= __MAC_26_0
    /* First: the Intel matchers below look at the device ID only. */
    if (ItlVirt::virt_match(device)) {
        isMatch = true;
        fHalService = new ItlVirt;
    }
#endif
    if (!isMatch && ItlIwx::iwx_match(device)) {
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
        this->pciNub = device;
        return this;
    }
    return NULL;
}

bool IOPCIEDeviceWrapper::
start(IOService *provider)
{
    _fWorkloop = IO80211WorkQueue::workQueue();

    // Panic timer: catch hangs inside IOService::start()
    static volatile bool wrapperStartReturned = false;
    wrapperStartReturned = false;
    thread_call_t wrapperTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!wrapperStartReturned)
                panic("IOPCIEDeviceWrapper: super::start() hung — did not return within 60 seconds");
        }, NULL);
    uint64_t wrapperDeadline;
    clock_interval_to_deadline(60, kSecondScale, &wrapperDeadline);
    thread_call_enter_delayed(wrapperTimer, wrapperDeadline);

    bool ret = super::start(provider);

    wrapperStartReturned = true;
    thread_call_cancel(wrapperTimer);
    thread_call_free(wrapperTimer);
    if (!ret) {
        XYLog("DEBUG %s FAIL: super::start\n", __FUNCTION__);
        return false;
    }
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, powerStateArray, 2);
    provider->joinPMtree(this);
    registerService();
    return true;
}

void IOPCIEDeviceWrapper::
stop(IOService *provider)
{
    PMstop();
    super::stop(provider);
}

IOReturn IOPCIEDeviceWrapper::
setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice)
{
    return IOPMAckImplied;
}
