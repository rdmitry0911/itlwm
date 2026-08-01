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

#if __IO80211_TARGET >= __MAC_26_0
extern "C" void airportItlwmRequestAPTxDequeue(
    IOEthernetController *controller);
#endif

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
    itl_ap_firmware_runtime_reset(&apRuntime);
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
    /* APSTAOwner has already closed the role-7 datapath but deliberately
     * retains its profile across sleep.  Retire the firmware GO resources
     * before the generic iwm_stop() destroys their rings; wake will rebuild
     * them from that upper snapshot after the primary STA boundary. */
    if (apRuntime.stage != kItlApFirmwareResourceIdle) {
        const int apError = iwm_stop_ap_resources(&com, &apRuntime);
        if (apError != 0)
            XYLog("%s: IWM AP radio-reset teardown error=%d\n",
                  DEVNAME(&com), apError);
    }
    if (!(ifp->if_flags & IFF_UP)) {
        XYLog("DEBUG %s SKIP: already !IFF_UP\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags &= ~IFF_UP;
    iwm_activate(&com, DVACT_QUIESCE);
    return kIOReturnSuccess;
}

bool ItlIwm::
supportsAPMode() const
{
#if !defined(IEEE80211_OPT_OUT_STA_ONLY)
    return false;
#else
    /*
     * This implementation uses DQA queues, typed GO/link stations, and the
     * MQ-RX descriptor path.  Admit only firmware that advertised all three;
     * 7k/legacy queue and legacy RX families remain fail-closed.
     */
    return (com.sc_device_family == IWM_DEVICE_FAMILY_8000 ||
            com.sc_device_family == IWM_DEVICE_FAMILY_9000) &&
        isset(com.sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT) &&
        isset(com.sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE) &&
        com.sc_mqrx_supported;
#endif
}

IOReturn ItlIwm::
startAPMode(const struct ItlHalApConfig *config)
{
    if (!supportsAPMode())
        return kIOReturnUnsupported;
    if (!itl_ap_client_config_supported(config))
        return kIOReturnUnsupported;
    if (apRuntime.stage != kItlApFirmwareResourceIdle)
        return kIOReturnBusy;
    int error = itl_ap_firmware_runtime_snapshot(&apRuntime, config);
    if (error != 0)
        return kIOReturnBadArgument;
    error = iwm_start_ap_resources(&com, &apRuntime);
    if (error != 0) {
        itl_ap_firmware_runtime_reset(&apRuntime);
        if (error == EOPNOTSUPP)
            return kIOReturnUnsupported;
        if (error == EINVAL)
            return kIOReturnBadArgument;
        return error == EBUSY ? kIOReturnBusy : kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
stopAPMode()
{
    if (apRuntime.stage == kItlApFirmwareResourceIdle)
        return kIOReturnSuccess;
    return iwm_stop_ap_resources(&com, &apRuntime) == 0 ?
        kIOReturnSuccess : kIOReturnError;
}

IOReturn ItlIwm::
transmitAPData(mbuf_t packet)
{
    struct ether_header ethernet;
    if (packet == NULL || mbuf_pkthdr_len(packet) < sizeof(ethernet) ||
        mbuf_copydata(packet, 0, sizeof(ethernet), &ethernet) != 0)
        return kIOReturnBadArgument;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_tx_client(&apRuntime, ethernet.ether_dhost);
    if (client == NULL)
        return kIOReturnNotReady;
    if (itl_ap_power_save_should_buffer(&apRuntime, client, packet)) {
        const bool queueWasEmpty = client->powerSaveQueueCount == 0;
        const int queueError =
            itl_ap_power_save_enqueue(client, packet);
        if (queueError != 0)
            return queueError == ENOBUFS ? kIOReturnNoResources :
                                           kIOReturnBadArgument;
        bool changed = false;
        int timError =
            itl_ap_power_save_set_tim(&apRuntime, client, true, &changed);
        if (timError == 0 && changed)
            timError = iwm_ap_send_beacon_template(&com, &apRuntime);
        if (timError != 0 && changed) {
            bool ignored = false;
            (void)itl_ap_power_save_set_tim(
                &apRuntime, client, false, &ignored);
            XYLog("%s: IWM AP power-save TIM arm failed\n",
                  DEVNAME(&com));
        }
        if (timError != 0 && queueWasEmpty) {
            (void)itl_ap_power_save_dequeue(client);
            return timError == ENOBUFS ? kIOReturnNoResources :
                                         kIOReturnError;
        }
        return kIOReturnSuccess;
    }
    mbuf_t wirePacket = NULL;
    int error = itl_ap_open_encap_data(
        &apRuntime, client, packet, false, &wirePacket);
    if (error != 0)
        return error == ENOBUFS ? kIOReturnNoResources : kIOReturnNotReady;
    const struct ieee80211_frame *wh =
        mtod(wirePacket, const struct ieee80211_frame *);
    const bool multicast = IEEE80211_IS_MULTICAST(wh->i_addr1);
    error = iwm_ap_send_raw_frame(&com, wirePacket,
        static_cast<uint8_t>(multicast ? apRuntime.multicastQueueId :
                                        client->queueId),
        multicast ? apRuntime.multicastStaId : client->staId);
    if (error != 0) {
        mbuf_freem(wirePacket);
        return error == ENOBUFS ? kIOReturnNoResources : kIOReturnError;
    }
    if (!multicast && ethernet.ether_type != htons(ETHERTYPE_PAE) &&
        client->clientQos && client->clientHt &&
        itl_ap_tx_ba_note_data(&client->clientTxBa[0])) {
        uint8_t token = ++client->clientTxDialogToken;
        if (token == 0)
            token = ++client->clientTxDialogToken;
        itl_ap_tx_ba_request(&client->clientTxBa[0], token, 0,
                             client->clientTxSequence[0]);
        mbuf_t request = NULL;
        int requestError = itl_ap_open_build_tx_addba_request(
            &apRuntime, client, 0, &request);
        if (requestError == 0)
            requestError = iwm_ap_send_raw_frame(
                &com, request, static_cast<uint8_t>(client->queueId),
                client->staId);
        if (requestError != 0) {
            if (request != NULL)
                mbuf_freem(request);
            itl_ap_tx_ba_reset(&client->clientTxBa[0]);
        } else {
            XYLog("%s: IWM AP TX ADDBA request tid=0 ssn=%u token=%u\n",
                  DEVNAME(&com),
                  static_cast<unsigned>(client->clientTxSequence[0]),
                  static_cast<unsigned>(token));
        }
    }
    mbuf_freem(packet);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
setAPKey(const struct ItlHalApKey *key)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (!itl_ap_client_is_secure(&apRuntime))
        return kIOReturnUnsupported;
    if (key == NULL || key->keyData == NULL || key->keyLength != 16 ||
        key->cipher != kItlHalApCipherAesCcm || key->keyIndex > 3 ||
        (key->flags != kItlHalApKeyPairwise &&
         key->flags != kItlHalApKeyGroup))
        return kIOReturnBadArgument;

    const bool pairwise = key->flags == kItlHalApKeyPairwise;
    struct ItlApFirmwareClientRuntime *client = pairwise ?
        itl_ap_firmware_find_client(&apRuntime, key->station) : NULL;
    if (pairwise && (client == NULL || !client->clientAssociated ||
        !client->clientStationInstalled))
        return kIOReturnNotReady;
    if (!pairwise && apRuntime.groupKeyInstalled &&
        apRuntime.groupKeyId == key->keyIndex &&
        timingsafe_bcmp(apRuntime.groupKey, key->keyData,
                        sizeof(apRuntime.groupKey)) == 0)
        return kIOReturnSuccess;

    const uint8_t staId = pairwise ? client->staId :
                                     apRuntime.multicastStaId;
    const uint8_t keyOffset = pairwise ? static_cast<uint8_t>(
        2 + itl_ap_firmware_client_index(&apRuntime, client)) :
        static_cast<uint8_t>(2 + kItlApFirmwareMaxClients);
    int error = 0;
    /* Legacy IWM TX commands carry the AP GTK inline.  Linux iwlwifi keeps
     * that AP group key out of ADD_STA_KEY; only the PTK is needed for RX. */
    if (pairwise)
        error = iwm_ap_set_ccmp_key(&com, staId, true, keyOffset,
            key->keyIndex, key->keyData, key->keyLength,
            key->rsc, key->rscLength);
    if (error != 0)
        return kIOReturnError;

    const uint64_t receiveSequence = itl_ap_key_rsc(key);
    if (pairwise) {
        memcpy(client->clientPairwiseKey, key->keyData,
               sizeof(client->clientPairwiseKey));
        client->clientPairwiseTxPn = 0;
        for (size_t tid = 0; tid < nitems(client->clientRxPn); tid++)
            client->clientRxPn[tid] = receiveSequence;
        client->clientPairwiseKeyInstalled = true;
    } else {
        memcpy(apRuntime.groupKey, key->keyData,
               sizeof(apRuntime.groupKey));
        apRuntime.groupKeyId = key->keyIndex;
        apRuntime.groupTxPn = 0;
        apRuntime.groupKeyInstalled = true;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
setAPMaxStations(uint32_t maxStations)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    return itl_ap_firmware_set_client_limit(
        &apRuntime, maxStations) == 0 ? kIOReturnSuccess :
                                       kIOReturnBadArgument;
}

IOReturn ItlIwm::
setAPHidden(bool hidden)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (apRuntime.hidden == hidden)
        return kIOReturnSuccess;

    const bool previousHidden = apRuntime.hidden;
    int error = itl_ap_firmware_set_hidden(&apRuntime, hidden);
    if (error != 0)
        return error == ENOENT ? kIOReturnUnsupported : kIOReturnBadArgument;
    error = iwm_ap_send_beacon_template(&com, &apRuntime);
    if (error != 0) {
        const int rollback =
            itl_ap_firmware_set_hidden(&apRuntime, previousHidden);
        if (rollback == 0)
            (void)iwm_ap_send_beacon_template(&com, &apRuntime);
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
sendAPStationCommand(const struct ItlHalApStationCommand *command)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (command == NULL)
        return kIOReturnBadArgument;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_client(&apRuntime, command->station);
    if (client == NULL)
        return kIOReturnNotReady;
    if (command->command == kItlHalApStationDisassociate) {
        if (client->timSet) {
            bool changed = false;
            if (itl_ap_power_save_set_tim(
                    &apRuntime, client, false, &changed) == 0 && changed)
                (void)iwm_ap_send_beacon_template(&com, &apRuntime);
        }
        client->clientAssociationPending = false;
        client->clientAuthenticated = false;
        client->clientAssociated = false;
        const int error = iwm_ap_remove_client_sta(
            &com, &apRuntime, client);
        itl_ap_firmware_client_reset(client);
        return error == 0 ? kIOReturnSuccess : kIOReturnError;
    }
    if (!client->clientAssociated)
        return kIOReturnNotReady;
    if (command->command == kItlHalApStationUnauthorize) {
        client->clientAuthorized = false;
        return kIOReturnSuccess;
    }
    if (command->command != kItlHalApStationAuthorize)
        return kIOReturnUnsupported;
    if (itl_ap_client_is_secure(&apRuntime) &&
        (!client->clientPairwiseKeyInstalled ||
         !apRuntime.groupKeyInstalled))
        return kIOReturnNotReady;
    client->clientAuthorized = true;
#if __IO80211_TARGET >= __MAC_26_0
    airportItlwmRequestAPTxDequeue(getController());
#endif
    return kIOReturnSuccess;
}

uint32_t ItlIwm::
getAPTxFreeSpace() const
{
    if (!itl_ap_open_is_running(&apRuntime) ||
        apRuntime.multicastQueueId >= IWM_MAX_QUEUES)
        return 0;
    const struct iwm_tx_ring *multicast = &com.txq[apRuntime.multicastQueueId];
    const uint32_t usable = IWM_TX_RING_COUNT - 1;
    const uint32_t multicastFree = multicast->queued < usable ?
        usable - multicast->queued : 0;
    uint32_t freeSpace = multicastFree;
    bool found = false;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        const struct ItlApFirmwareClientRuntime *client =
            &apRuntime.clients[index];
        if (!client->inUse || !client->clientStationInstalled)
            continue;
        if (client->queueId >= IWM_MAX_QUEUES)
            return 0;
        found = true;
        const struct iwm_tx_ring *ring = &com.txq[client->queueId];
        const uint32_t clientFree = ring->queued < usable ?
            usable - ring->queued : 0;
        freeSpace = MIN(freeSpace, clientFree);
    }
    return found ? freeSpace : 0;
}

extern "C" bool
airportItlwmQueryIwmAPTxFreeSpace(ItlHalService *service, uint32_t *freeSpace)
{
    ItlIwm *that = OSDynamicCast(ItlIwm, service);
    if (that == NULL || freeSpace == NULL)
        return false;
    *freeSpace = that->getAPTxFreeSpace();
    return true;
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
        ic->ic_mgt_timer != 0 || ic->ic_des_esslen != 0)
        return kIOReturnBusy;

    /*
     * AppleBCMWLANScanAdapter accepts a valid WCL carrier independently of
     * legacy background-scan state.  Use wclScanPhase/IWM_FLAG_SCANNING as
     * the physical overlap owner below; IEEE80211_F_BGSCAN can remain set
     * briefly after link loss even though no lower background owner exists.
     */
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
