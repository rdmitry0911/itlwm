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

#include "ItlIwm.hpp"

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlIwm, ItlHalService)

void ItlIwm::
detach(IOPCIDevice *device)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    struct iwm_softc *sc = &com;
    
    for (int txq_i = 0; txq_i < nitems(sc->txq); txq_i++)
        iwm_free_tx_ring(sc, &sc->txq[txq_i]);
    iwm_rs_free(sc);
    iwm_free_rx_ring(sc, &sc->rxq);
    iwm_dma_contig_free(&sc->ict_dma);
    iwm_dma_contig_free(&sc->kw_dma);
    iwm_dma_contig_free(&sc->sched_dma);
    iwm_dma_contig_free(&sc->fw_dma);
    ieee80211_ifdetach(ifp);
    taskq_destroy(systq);
    taskq_destroy(com.sc_nswq);
    releaseAll();
}

bool ItlIwm::
attach(IOPCIDevice *device)
{
    wclScanLock = IOSimpleLockAlloc();
    if (wclScanLock == NULL)
        return false;
    wclScanPhase = ItlIwmWclScanPhase::Idle;
    wclScanUpperGeneration = 0;
    wclScanBackendGeneration = 0;
    wclScanNextBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    wclScanNeedsReopen = false;

    pci.pa_tag = device;
    pci.workloop = getMainWorkLoop();
    if (!iwm_attach(&com, &pci)) {
        detach(device);
        releaseAll();
        return false;
    }
    return true;
}

void ItlIwm::
free()
{
	if (ieee80211_bip_lifetime_drain(&com.sc_ic) != 0)
		panic("ItlIwm::free BIP lifetime");
	ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic);
    super::free();
}

void ItlIwm::
releaseAll()
{
    pci_intr_handle *intrHandler = com.ih;
    if (com.sc_calib_to) {
        timeout_del(&com.sc_calib_to);
        timeout_free(&com.sc_calib_to);
    }
    if (com.sc_led_blink_to) {
        timeout_del(&com.sc_led_blink_to);
        timeout_free(&com.sc_led_blink_to);
    }
    if (intrHandler) {
        if (intrHandler->intr && intrHandler->workloop) {
//            intrHandler->intr->disable();
            intrHandler->workloop->removeEventSource(intrHandler->intr);
            intrHandler->intr->release();
        }
        intrHandler->intr = NULL;
        intrHandler->workloop = NULL;
        intrHandler->arg = NULL;
        intrHandler->dev = NULL;
        intrHandler->func = NULL;
        intrHandler->release();
        com.ih = NULL;
    }
    pci.pa_tag = NULL;
    pci.workloop = NULL;
    if (wclScanLock != NULL) {
        IOSimpleLockFree(wclScanLock);
        wclScanLock = NULL;
    }
}

IOReturn ItlIwm::
enable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (ifp->if_flags & IFF_UP) {
        XYLog("DEBUG %s SKIP: already IFF_UP\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags |= IFF_UP;
    iwm_activate(&com, DVACT_RESUME);
    iwm_activate(&com, DVACT_WAKEUP);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
disable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (!(ifp->if_flags & IFF_UP)) {
        XYLog("DEBUG %s SKIP: already !IFF_UP\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags &= ~IFF_UP;
    iwm_activate(&com, DVACT_QUIESCE);
    return kIOReturnSuccess;
}

struct ieee80211com *ItlIwm::
get80211Controller()
{
    return &com.sc_ic;
}

ItlDriverInfo *ItlIwm::
getDriverInfo()
{
    return this;
}

ItlDriverController *ItlIwm::
getDriverController()
{
    return this;
}

void ItlIwm::
clearScanningFlags()
{
    com.sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
}

static void
iwm_wcl_scan_ticket_reset_locked(ItlIwm *that)
{
    that->wclScanPhase = ItlIwmWclScanPhase::Idle;
    that->wclScanUpperGeneration = 0;
    that->wclScanBackendGeneration = 0;
    that->wclScanPublicationInvalidated = false;
}

static uint32_t
iwm_wcl_scan_next_backend_generation_locked(ItlIwm *that)
{
    ++that->wclScanNextBackendGeneration;
    if (that->wclScanNextBackendGeneration == 0)
        ++that->wclScanNextBackendGeneration;
    return that->wclScanNextBackendGeneration;
}

static void
iwm_wcl_scan_publish_started(ItlIwm *that, uint64_t generation,
                             uint32_t backendGeneration)
{
    struct ieee80211com *ic = &that->com.sc_ic;
    if (ic->ic_event_handler == NULL || generation == 0 ||
        backendGeneration == 0)
        return;

    struct ieee80211_wcl_scan_started started;
    explicit_bzero(&started, sizeof(started));
    started.generation = generation;
    started.backend_generation = backendGeneration;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_STARTED, &started);
    explicit_bzero(&started, sizeof(started));
}

static void
iwm_wcl_scan_publish_start_rejected(ItlIwm *that, uint64_t generation,
                                    uint32_t backendGeneration)
{
    struct ieee80211com *ic = &that->com.sc_ic;
    if (ic->ic_event_handler == NULL || generation == 0)
        return;

    struct ieee80211_wcl_scan_start_rejected rejected;
    explicit_bzero(&rejected, sizeof(rejected));
    rejected.generation = generation;
    rejected.backend_generation = backendGeneration;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_START_REJECTED,
                            &rejected);
    explicit_bzero(&rejected, sizeof(rejected));
}

void ItlIwm::
publishWclScanTerminal(const ItlIwmWclScanTerminal *terminal,
                       uint32_t status)
{
    struct ieee80211com *ic = &com.sc_ic;
    if (terminal == NULL || !terminal->publish ||
        terminal->upperGeneration == 0 ||
        terminal->backendGeneration == 0 ||
        ic->ic_event_handler == NULL)
        return;

    struct ieee80211_wcl_scan_terminal event;
    explicit_bzero(&event, sizeof(event));
    event.generation = terminal->upperGeneration;
    event.backend_generation = terminal->backendGeneration;
    event.status = status;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_TERMINAL, &event);
    explicit_bzero(&event, sizeof(event));
}

IOReturn ItlIwm::
beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    bool startNow = false;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (wclScanLock == NULL || ic->ic_state != IEEE80211_S_SCAN ||
        ic->ic_opmode != IEEE80211_M_STA ||
        (ic->ic_if.if_flags & IFF_RUNNING) == 0 ||
        (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        ic->ic_mgt_timer != 0 || ic->ic_des_esslen != 0)
        return kIOReturnBusy;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnBusy;
    }
    wclScanUpperGeneration = generation;
    wclScanBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    if ((com.sc_flags & IWM_FLAG_SCANNING) != 0) {
        /*
         * Do not reuse the boot scan's terminal or cache.  Its exact terminal
         * only advances this ticket to InitialStarting; iwm_endscan() then
         * performs controlled cleanup and queues one fresh IWM command.
         */
        wclScanPhase = ItlIwmWclScanPhase::InitialQueued;
    } else {
        wclScanPhase = ItlIwmWclScanPhase::InitialStarting;
        startNow = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    if (startNow)
        ieee80211_begin_scan(&ic->ic_if);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
beginWclBackgroundScan(uint64_t generation,
                       uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    bool rejectedBeforeSubmit = false;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (wclScanLock == NULL || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == NULL || ic->ic_mgt_timer != 0 ||
        (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        (com.sc_flags & (IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) != 0 ||
        ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
         !ic->ic_bss->ni_port_valid))
        return kIOReturnBusy;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnBusy;
    }
    wclScanPhase = ItlIwmWclScanPhase::BackgroundStarting;
    wclScanUpperGeneration = generation;
    wclScanBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    ieee80211_begin_cache_bgscan(&ic->ic_if);

    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive &&
        wclScanUpperGeneration == generation) {
        *outBackendGeneration = wclScanBackendGeneration;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnSuccess;
    }
    /*
     * A very short scan may already have published its tagged terminal.  In
     * that case the upper reducer owns a Pending completion and will recover
     * the backend generation from the terminal mailbox.
     */
    if (wclScanPhase == ItlIwmWclScanPhase::Idle) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnSuccess;
    }
    if (wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting &&
        wclScanUpperGeneration == generation) {
        iwm_wcl_scan_ticket_reset_locked(this);
        rejectedBeforeSubmit = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (rejectedBeforeSubmit) {
        /* ieee80211_begin_cache_bgscan() has a void result and trusts a zero
         * ic_bgscan_start return.  If the IWM callback did not cross its
         * post-submit edge, retract those generic policy bits as part of the
         * same rejected request. */
        ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
                          IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    }
    return kIOReturnBusy;
}

void ItlIwm::
noteWclInitialScanCommandStarted()
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::InitialStarting) {
        backendGeneration =
            iwm_wcl_scan_next_backend_generation_locked(this);
        wclScanBackendGeneration = backendGeneration;
        wclScanPhase = ItlIwmWclScanPhase::InitialActive;
        generation = wclScanUpperGeneration;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    iwm_wcl_scan_publish_started(this, generation, backendGeneration);
}

void ItlIwm::
noteWclInitialScanCommandRejected()
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::InitialStarting) {
        generation = wclScanUpperGeneration;
        backendGeneration = wclScanBackendGeneration;
        iwm_wcl_scan_ticket_reset_locked(this);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    iwm_wcl_scan_publish_start_rejected(this, generation,
                                        backendGeneration);
}

void ItlIwm::
noteWclBackgroundScanCommandStarted()
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting) {
        wclScanBackendGeneration =
            iwm_wcl_scan_next_backend_generation_locked(this);
        wclScanPhase = ItlIwmWclScanPhase::BackgroundActive;
        __atomic_store_n(&com.sc_ic.ic_wcl_scan_active, 1,
                         __ATOMIC_RELEASE);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
}

void ItlIwm::
noteWclScanRadioReady()
{
    bool publish = false;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanNeedsReopen) {
        wclScanNeedsReopen = false;
        publish = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (publish && com.sc_ic.ic_event_handler != NULL)
        (*com.sc_ic.ic_event_handler)(&com.sc_ic,
                                     IEEE80211_EVT_WCL_SCAN_REOPENED, NULL);
}

ItlIwmWclScanTerminalKind ItlIwm::
claimWclScanTerminal(ItlIwmWclScanTerminal *terminal)
{
    if (terminal == NULL || wclScanLock == NULL)
        return ItlIwmWclScanTerminalKind::None;
    explicit_bzero(terminal, sizeof(*terminal));

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::InitialQueued) {
        if (wclScanPublicationInvalidated) {
            iwm_wcl_scan_ticket_reset_locked(this);
            IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            return ItlIwmWclScanTerminalKind::None;
        }
        wclScanPhase = ItlIwmWclScanPhase::InitialStarting;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return ItlIwmWclScanTerminalKind::ReplayInitial;
    }

    ItlIwmWclScanTerminalKind kind =
        ItlIwmWclScanTerminalKind::None;
    if (wclScanPhase == ItlIwmWclScanPhase::InitialActive)
        kind = ItlIwmWclScanTerminalKind::Foreground;
    else if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive)
        kind = ItlIwmWclScanTerminalKind::Background;
    if (kind != ItlIwmWclScanTerminalKind::None) {
        terminal->upperGeneration = wclScanUpperGeneration;
        terminal->backendGeneration = wclScanBackendGeneration;
        terminal->publish = !wclScanPublicationInvalidated;
        iwm_wcl_scan_ticket_reset_locked(this);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return kind;
}

IOReturn ItlIwm::
abortWclBackgroundScan(uint64_t generation)
{
    struct ieee80211com *ic = &com.sc_ic;
    ItlIwmWclScanTerminal terminal;
    ItlIwmWclScanTerminalKind kind =
        ItlIwmWclScanTerminalKind::None;

    if (generation == 0 || wclScanLock == NULL)
        return kIOReturnBadArgument;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool active = wclScanUpperGeneration == generation &&
        (wclScanPhase == ItlIwmWclScanPhase::InitialActive ||
         wclScanPhase == ItlIwmWclScanPhase::BackgroundActive);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!active)
        return kIOReturnNotReady;
    if (iwm_scan_abort(&com) != 0)
        return kIOReturnError;

    explicit_bzero(&terminal, sizeof(terminal));
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanUpperGeneration == generation) {
        if (wclScanPhase == ItlIwmWclScanPhase::InitialActive)
            kind = ItlIwmWclScanTerminalKind::Foreground;
        else if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive)
            kind = ItlIwmWclScanTerminalKind::Background;
        if (kind != ItlIwmWclScanTerminalKind::None) {
            terminal.upperGeneration = wclScanUpperGeneration;
            terminal.backendGeneration = wclScanBackendGeneration;
            terminal.publish = !wclScanPublicationInvalidated;
            iwm_wcl_scan_ticket_reset_locked(this);
        }
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    if (kind == ItlIwmWclScanTerminalKind::Foreground) {
        ieee80211_end_scan_controlled(
            &ic->ic_if, IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND);
    } else if (kind == ItlIwmWclScanTerminalKind::Background) {
        __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once, 1,
                         __ATOMIC_RELEASE);
        ieee80211_end_scan(&ic->ic_if);
        __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    }
    publishWclScanTerminal(
        &terminal, IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED);
    return kIOReturnSuccess;
}

void ItlIwm::
invalidateWclBackgroundScan()
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle)
        wclScanPublicationInvalidated = true;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
}

void ItlIwm::
invalidateWclScanForReset()
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;
    bool rejectStart = false;
    bool invalidateActive = false;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    generation = wclScanUpperGeneration;
    backendGeneration = wclScanBackendGeneration;
    rejectStart =
        wclScanPhase == ItlIwmWclScanPhase::InitialQueued ||
        wclScanPhase == ItlIwmWclScanPhase::InitialStarting ||
        wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting;
    invalidateActive =
        wclScanPhase == ItlIwmWclScanPhase::InitialActive ||
        wclScanPhase == ItlIwmWclScanPhase::BackgroundActive;
    iwm_wcl_scan_ticket_reset_locked(this);
    wclScanNeedsReopen = true;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    __atomic_store_n(&com.sc_ic.ic_wcl_scan_active, 0, __ATOMIC_RELEASE);

    if (rejectStart) {
        iwm_wcl_scan_publish_start_rejected(this, generation,
                                            backendGeneration);
    } else if (invalidateActive && generation != 0 &&
               backendGeneration != 0 &&
               com.sc_ic.ic_event_handler != NULL) {
        struct ieee80211_wcl_scan_invalidation invalidation;
        explicit_bzero(&invalidation, sizeof(invalidation));
        invalidation.generation = generation;
        invalidation.backend_generation = backendGeneration;
        (*com.sc_ic.ic_event_handler)(
            &com.sc_ic, IEEE80211_EVT_WCL_SCAN_INVALIDATED, &invalidation);
        explicit_bzero(&invalidation, sizeof(invalidation));
    }
}

IOReturn ItlIwm::
setMulticastList(IOEthernetAddress *addr, int count)
{
    struct ieee80211com *ic = &com.sc_ic;
    struct iwm_mcast_filter_cmd *cmd;
    int len;
    uint8_t addr_count;
    int err;
    
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
        return kIOReturnError;
    addr_count = count;
    if (count > IWM_MAX_MCAST_FILTERING_ADDRESSES)
        addr_count = 0;
    if (addr == NULL)
        addr_count = 0;
    len = roundup(sizeof(struct iwm_mcast_filter_cmd) + addr_count * ETHER_ADDR_LEN, 4);
    cmd = (struct iwm_mcast_filter_cmd *)malloc(len, 0, 0);
    if (!cmd)
        return kIOReturnError;
    cmd->pass_all = addr_count == 0;
    cmd->count = addr_count;
    cmd->port_id = 0;
    IEEE80211_ADDR_COPY(cmd->bssid, ic->ic_bss->ni_bssid);
    if (addr_count > 0)
        memcpy(cmd->addr_list,
               addr->bytes, ETHER_ADDR_LEN * cmd->count);
    err = iwm_send_cmd_pdu(&com, IWM_MCAST_FILTER_CMD, IWM_CMD_ASYNC, len,
                     cmd);
    ::free(cmd);
    return err ? kIOReturnError : kIOReturnSuccess;
}

const char *ItlIwm::
getFirmwareVersion()
{
    return com.sc_fwver;
}

const char *ItlIwm::
getFirmwareName()
{
    return com.sc_fwname;
}

UInt32 ItlIwm::
supportedFeatures()
{
    return kIONetworkFeatureMultiPages;
}

const char *ItlIwm::
getFirmwareCountryCode()
{
    return com.sc_fw_mcc;
}

uint32_t ItlIwm::
getTxQueueSize()
{
    return IWM_TX_RING_COUNT;
}

int16_t ItlIwm::
getBSSNoise()
{
    return com.sc_noise;
}

bool ItlIwm::
is5GBandSupport()
{
    return com.sc_nvm.sku_cap_band_52GHz_enable;
}

int ItlIwm::
getTxNSS()
{
    return iwm_mimo_enabled(&com) &&
    (com.sc_ic.ic_bss != NULL && com.sc_ic.ic_bss->ni_rx_nss > 1) ?
    2 : 1;
}

uint8_t ItlIwm::
getTxChainMask()
{
    return iwm_fw_valid_tx_ant(&com);
}

uint8_t ItlIwm::
getRxChainMask()
{
    return iwm_fw_valid_rx_ant(&com);
}

uint32_t ItlIwm::
getLqmBeaconCount()
{
    return le32toh(com.sc_stats.rx.general.channel_beacons);
}
