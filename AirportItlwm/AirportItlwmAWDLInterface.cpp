//
//  AirportItlwmAWDLInterface.cpp
//  AirportItlwm (v-Airport)
//
//  Probe stage of awdl0 (see the header). Records what Apple's AWDL stack asks
//  of the firmware: the call, its bytes, and the frames it hands to awdl0.
//  Getters echo the last value set where this tree knows the structure size.
//  Shape and the native rules it relies on (the oob request's pointer at 0x30,
//  the 24-byte sync state, defaults of sync enabled/state) are from
//  ForbiddenItlwm's ItlAWDLInterface (GPL-2.0).
//

#include "AirportItlwmAWDLInterface.hpp"

#if __IO80211_TARGET >= __MAC_26_0

#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <sys/proc.h>
#include <kern/clock.h>

OSDefineMetaClassAndStructors(AirportItlwmAWDLInterface, IO80211AWDLProtocol)

static constexpr unsigned kTraceCapacity = 600;
static constexpr size_t kUnknownLength = 16;      // never written back
static constexpr size_t kLogBytes = 96;
static constexpr size_t kKeepBytes = 0x800;
static constexpr unsigned kTransmitSamples = 24;

static void hexInto(char *out, size_t outSize, const uint8_t *bytes, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    size_t o = 0;
    for (size_t i = 0; i < length && o + 3 < outSize; ++i) {
        out[o++] = digits[bytes[i] >> 4];
        out[o++] = digits[bytes[i] & 15];
    }
    out[o] = 0;
}

bool AirportItlwmAWDLInterface::initWithController(IO80211Controller *controller,
                                                   ether_addr *address)
{
    fLock = IOLockAlloc();
    fCounts = OSDictionary::withCapacity(80);
    fLastSet = OSDictionary::withCapacity(40);
    fTrace = OSArray::withCapacity(kTraceCapacity);
    fSequence = 0;
    fTransmitCallbacks = 0;
    fTransmitOffered = 0;
    if (address)
        fAddress = *address;
    if (!fLock || !fCounts || !fLastSet || !fTrace)
        return false;
    if (!IO80211VirtualInterface::init(controller, address, APPLE80211_VIF_AWDL, "awdl"))
        return false;
    setProperty("VAPAWDLStage", "initialized");
    return true;
}

void AirportItlwmAWDLInterface::free()
{
    OSSafeReleaseNULL(fCounts);
    OSSafeReleaseNULL(fLastSet);
    OSSafeReleaseNULL(fTrace);
    if (fLock) {
        IOLockFree(fLock);
        fLock = nullptr;
    }
    IO80211AWDLProtocol::free();
}

void AirportItlwmAWDLInterface::note(const char *what, const void *bytes, size_t length)
{
    char hex[2 * kLogBytes + 1];
    const size_t shown = length < kLogBytes ? length : kLogBytes;
    hexInto(hex, sizeof(hex), static_cast<const uint8_t *>(bytes), bytes ? shown : 0);
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    char process[24] = {};
    proc_selfname(process, sizeof(process));
    char line[2 * kLogBytes + 120];
    snprintf(line, sizeof(line), "%llu %s %s len=%lu %s", ns / 1000000ULL, process, what,
             (unsigned long)length, hex);
    IOLog("VAPAWDL %s\n", line);
    if (!fLock)
        return;
    IOLockLock(fLock);
    ++fSequence;
    if (fTrace && fTrace->getCount() < kTraceCapacity) {
        if (OSString *s = OSString::withCString(line)) {
            fTrace->setObject(s);
            s->release();
        }
    }
    IOLockUnlock(fLock);
}

// The registry gets snapshots: the live collections change under fLock on other threads.
void AirportItlwmAWDLInterface::publish()
{
    if (!fLock)
        return;
    IOLockLock(fLock);
    OSDictionary *counts = OSDictionary::withDictionary(fCounts);
    OSDictionary *last = OSDictionary::withDictionary(fLastSet);
    OSArray *trace = OSArray::withArray(fTrace);
    IOLockUnlock(fLock);
    if (counts) { setProperty("VAPAWDLCalls", counts); counts->release(); }
    if (last) { setProperty("VAPAWDLLastSet", last); last->release(); }
    if (trace) { setProperty("VAPAWDLTrace", trace); trace->release(); }
}

IOReturn AirportItlwmAWDLInterface::noteSet(const char *name, const void *data, size_t knownLength)
{
    if (!data)
        return kIOReturnBadArgument;
    const size_t length = knownLength ? knownLength : kUnknownLength;
    char what[80];
    snprintf(what, sizeof(what), "set%s", name);
    note(what, data, length);
    if (fLock) {
        IOLockLock(fLock);
        const OSNumber *old = OSDynamicCast(OSNumber, fCounts->getObject(what));
        if (OSNumber *n = OSNumber::withNumber(old ? old->unsigned32BitValue() + 1 : 1, 32)) {
            fCounts->setObject(what, n);
            n->release();
        }
        if (knownLength) {
            if (OSData *copy = OSData::withBytes(data, (unsigned)(length < kKeepBytes ? length : kKeepBytes))) {
                fLastSet->setObject(name, copy);
                copy->release();
            }
        }
        IOLockUnlock(fLock);
        publish();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::noteGet(const char *name, void *data, size_t knownLength)
{
    if (!data)
        return kIOReturnBadArgument;
    char what[80];
    snprintf(what, sizeof(what), "get%s", name);
    IOReturn result = kIOReturnUnsupported;
    uint32_t calls = 0;
    if (fLock) {
        IOLockLock(fLock);
        const OSNumber *old = OSDynamicCast(OSNumber, fCounts->getObject(what));
        calls = old ? old->unsigned32BitValue() + 1 : 1;
        if (OSNumber *n = OSNumber::withNumber(calls, 32)) {
            fCounts->setObject(what, n);
            n->release();
        }
        const OSData *last = OSDynamicCast(OSData, fLastSet->getObject(name));
        if (knownLength && last && last->getLength() == knownLength) {
            memcpy(data, last->getBytesNoCopy(), knownLength);
            result = kIOReturnSuccess;
        }
        IOLockUnlock(fLock);
    }
    // Getters are frequent: only the first few of each go to the trace.
    if (calls <= 3) {
        char tagged[96];
        snprintf(tagged, sizeof(tagged), "%s ->0x%x", what, result);
        note(tagged, result == kIOReturnSuccess ? data : nullptr, result == kIOReturnSuccess ? knownLength : 0);
        publish();
    }
    return result;
}

void AirportItlwmAWDLInterface::noteTransmit(IOSkywalkPacket *const *packets, UInt32 count)
{
    ++fTransmitCallbacks;
    fTransmitOffered += count;
    if (fTransmitCallbacks <= 32 || !(fTransmitCallbacks % 64)) {
        setProperty("VAPAWDLTxCallbacks", fTransmitCallbacks, 32);
        setProperty("VAPAWDLTxOffered", fTransmitOffered, 32);
    }
    for (UInt32 i = 0; packets && i < count && fTransmitOffered - count + i < kTransmitSamples; ++i) {
        IOSkywalkPacket *packet = packets[i];
        if (!packet)
            continue;
        const uint8_t *base = static_cast<const uint8_t *>(packet->getDataVirtualAddress());
        const UInt32 offset = packet->getDataOffset();
        const UInt32 length = packet->getDataLength();
        if (!base || !length || offset > 2048 || length > 2048 - offset)
            continue;
        note("tx", base + offset, length);
    }
    if (fTransmitOffered - count < kTransmitSamples)
        publish();
}

UInt32 AirportItlwmAWDLInterface::transmitAction(OSObject *owner, IOSkywalkTxSubmissionQueue *,
                                                 IOSkywalkPacket *const *packets, UInt32 count, void *)
{
    // Nothing reaches the radio at this stage: frames stay with Skywalk.
    if (auto *self = OSDynamicCast(AirportItlwmAWDLInterface, owner))
        self->noteTransmit(packets, count);
    return 0;
}

IOReturn AirportItlwmAWDLInterface::processBSDCommand(ifnet_t interface, UInt cmd, void *data)
{
    const bool get = cmd == SIOCGA80211, set = cmd == SIOCSA80211;
    if ((get || set) && data) {
        const apple80211req *request = static_cast<const apple80211req *>(data);
        char what[64];
        snprintf(what, sizeof(what), "ioc%s type=%d val=%d", get ? "get" : "set",
                 request->req_type, request->req_val);
        const size_t length = request->req_len;
        note(what, set ? request->req_data : nullptr, set && request->req_data ? length : 0);
    } else {
        char what[48];
        snprintf(what, sizeof(what), "bsd cmd=0x%x", cmd);
        note(what, nullptr, 0);
    }
    const IOReturn result = IO80211AWDLProtocol::processBSDCommand(interface, cmd, data);
    if ((get || set) && data && result != kIOReturnSuccess) {
        char what[64];
        snprintf(what, sizeof(what), "ioc-result type=%d ->0x%x",
                 static_cast<const apple80211req *>(data)->req_type, result);
        note(what, nullptr, 0);
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::enable(UInt options)
{
    const IOReturn result = IO80211AWDLProtocol::enable(options);
    char what[48];
    snprintf(what, sizeof(what), "enable opt=0x%x ->0x%x", options, result);
    note(what, nullptr, 0);
    return result;
}

void AirportItlwmAWDLInterface::setMacAddress(ether_addr &address)
{
    fAddress = address;
    note("setMacAddress", &address, sizeof(address));
}

// ---- IO80211AWDLProtocol ---------------------------------------------------

#define VAP_GET_KNOWN(Name, Type) \
    IOReturn AirportItlwmAWDLInterface::get##Name(struct Type *data) \
    { return noteGet(#Name, data, sizeof(struct Type)); }
#define VAP_GET_UNKNOWN(Name, Type) \
    IOReturn AirportItlwmAWDLInterface::get##Name(struct Type *data) \
    { return noteGet(#Name, data, 0); }
#define VAP_SET_KNOWN(Name, Type) \
    IOReturn AirportItlwmAWDLInterface::set##Name(struct Type *data) \
    { return noteSet(#Name, data, sizeof(struct Type)); }
#define VAP_SET_UNKNOWN(Name, Type) \
    IOReturn AirportItlwmAWDLInterface::set##Name(struct Type *data) \
    { return noteSet(#Name, data, 0); }

// The reference answers these on awdl0 only with an AWDL radio behind it.
IOReturn AirportItlwmAWDLInterface::getSSID(struct apple80211_ssid_data *data) { return noteGet("SSID", data, 0); }
IOReturn AirportItlwmAWDLInterface::getCHANNEL(struct apple80211_channel_data *data) { return noteGet("CHANNEL", data, 0); }
IOReturn AirportItlwmAWDLInterface::getBSSID(struct apple80211_bssid_data *data) { return noteGet("BSSID", data, 0); }
IOReturn AirportItlwmAWDLInterface::getSTATE(struct apple80211_state_data *data) { return noteGet("STATE", data, 0); }

VAP_GET_KNOWN(AWDL_SYNC_PARAMS, apple80211_awdl_sync_params)
VAP_GET_KNOWN(AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter)
VAP_GET_KNOWN(AWDL_ELECTION_ID, apple80211_awdl_election_id)
VAP_GET_KNOWN(AWDL_ELECTION_METRIC, apple80211_awdl_election_metric)
VAP_GET_KNOWN(AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, apple80211_awdl_sync_channel_sequence)
VAP_GET_UNKNOWN(PEER_CACHE_MAXIMUM_SIZE, apple80211_peer_cache_maximum_size)
VAP_GET_KNOWN(AWDL_MASTER_CHANNEL, apple80211_awdl_master_channel)
VAP_GET_KNOWN(AWDL_ELECTION_RSSI_THRESHOLDS, apple80211_awdl_election_rssi_thresholds)
VAP_GET_KNOWN(AWDL_PRESENCE_MODE, apple80211_awdl_presence_mode)
VAP_GET_UNKNOWN(AWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
VAP_GET_UNKNOWN(AWDL_DEVICE_CAPABILITIES, apple80211_awdl_device_capabilities)
VAP_GET_KNOWN(AWDL_SECONDARY_MASTER_CHANNEL, apple80211_awdl_secondary_master_channel)
VAP_GET_UNKNOWN(AWDL_MIN_RATE, apple80211_awdl_min_rate)
VAP_GET_UNKNOWN(AWDL_ENCRYPTION_TYPE, apple80211_encrypt)
VAP_GET_UNKNOWN(AWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
VAP_GET_UNKNOWN(AWDL_DEV_STATS, apple80211_awdl_dev_stats)
VAP_GET_UNKNOWN(AWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
VAP_GET_UNKNOWN(AWDL_CCA, apple80211_awdl_cca)
VAP_GET_UNKNOWN(LLW_PARAMS, apple80211_llw_params)

IOReturn AirportItlwmAWDLInterface::getAWDL_SYNC_ENABLED(struct apple80211_awdl_sync_enabled *data)
{
    IOReturn result = noteGet("AWDL_SYNC_ENABLED", data, sizeof(*data));
    if (result == kIOReturnUnsupported && data) {
        bzero(data, sizeof(*data));         // off until the stack turns it on
        data->version = APPLE80211_VERSION;
        result = kIOReturnSuccess;
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::getAWDL_SYNC_STATE(struct apple80211_awdl_sync_state *data)
{
    IOReturn result = noteGet("AWDL_SYNC_STATE", data, sizeof(*data));
    if (result == kIOReturnUnsupported && data) {
        bzero(data, sizeof(*data));         // 1: inactive
        data->version = APPLE80211_VERSION;
        data->state = 1;
        result = kIOReturnSuccess;
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::getAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *data)
{
    // Native record: a 32-bit mode after the version (ForbiddenItlwm); echo only those 8 bytes.
    IOReturn result = noteGet("AWDL_AF_TX_MODE", data, 0);
    if (data && fLock) {
        IOLockLock(fLock);
        const OSData *last = OSDynamicCast(OSData, fLastSet->getObject("AWDL_AF_TX_MODE8"));
        if (last && last->getLength() == 8) {
            memcpy(data, last->getBytesNoCopy(), 8);
            result = kIOReturnSuccess;
        }
        IOLockUnlock(fLock);
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(struct apple80211_awdl_cap *data)
{
    IOReturn result = noteGet("AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES", data, 0);
    if (data) {                              // no secondary payload claimed
        bzero(data, sizeof(*data));
        data->version = APPLE80211_VERSION;
        result = kIOReturnSuccess;
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::setSSID(struct apple80211_ssid_data *data) { return noteSet("SSID", data, sizeof(*data)); }
IOReturn AirportItlwmAWDLInterface::setCHANNEL(struct apple80211_channel_data *data) { return noteSet("CHANNEL", data, sizeof(*data)); }
VAP_SET_KNOWN(AWDL_SYNC_PARAMS, apple80211_awdl_sync_params)
VAP_SET_KNOWN(AWDL_SYNC_ENABLED, apple80211_awdl_sync_enabled)
VAP_SET_KNOWN(AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter)
VAP_SET_KNOWN(AWDL_ELECTION_ID, apple80211_awdl_election_id)
VAP_SET_KNOWN(AWDL_BSSID, apple80211_awdl_bssid)
VAP_SET_KNOWN(AWDL_ELECTION_METRIC, apple80211_awdl_election_metric)
VAP_SET_KNOWN(AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, apple80211_awdl_sync_channel_sequence)
VAP_SET_KNOWN(AWDL_MASTER_CHANNEL, apple80211_awdl_master_channel)
VAP_SET_KNOWN(AWDL_SYNC_STATE, apple80211_awdl_sync_state)
VAP_SET_KNOWN(AWDL_ELECTION_RSSI_THRESHOLDS, apple80211_awdl_election_rssi_thresholds)
VAP_SET_KNOWN(AWDL_PRESENCE_MODE, apple80211_awdl_presence_mode)
VAP_SET_UNKNOWN(PEER_CACHE_CONTROL, apple80211_peer_cache_control)
VAP_SET_UNKNOWN(AWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
VAP_SET_KNOWN(AWDL_SECONDARY_MASTER_CHANNEL, apple80211_awdl_secondary_master_channel)
VAP_SET_UNKNOWN(AWDL_MIN_RATE, apple80211_awdl_min_rate)
VAP_SET_UNKNOWN(AWDL_ENCRYPTION_KEYS, apple80211_key)
VAP_SET_UNKNOWN(AWDL_ENCRYPTION_TYPE, apple80211_encrypt)
VAP_SET_UNKNOWN(AWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
VAP_SET_UNKNOWN(AWDL_AWDL_ADVERTISERS, awdl_peer_advert_add)
VAP_SET_UNKNOWN(AWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
VAP_SET_UNKNOWN(AWDL_DFSP_UCSA_CONFIG, apple80211_awdl_dfsp_ucsa_config)
VAP_SET_UNKNOWN(COMPANION_SKYWALK_IF_CREATE, apple80211_companion_skywalk_if_create)
VAP_SET_UNKNOWN(AWDL_OOB_SEC_PAYLOAD_REQUEST, apple80211_awdl_af_sec_payload_request)
VAP_SET_UNKNOWN(LLW_PARAMS, apple80211_llw_params)
VAP_SET_UNKNOWN(TX_RATE, apple80211_tx_rate)
VAP_SET_UNKNOWN(P2P_COEX_PROTECTION, apple80211_p2p_coex_protection)
VAP_SET_UNKNOWN(PSF_DWELL, apple80211_awdl_psf_dwell_params)
VAP_SET_UNKNOWN(AWDL_CONFIG, apple80211_awdl_config_request)
VAP_SET_UNKNOWN(AWDL_RTG_PEER_STATS_CONFIG, apple80211_rtg_peer_stats_cfg)

IOReturn AirportItlwmAWDLInterface::setAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *data)
{
    IOReturn result = noteSet("AWDL_AF_TX_MODE", data, 0);
    if (data && fLock) {
        IOLockLock(fLock);
        if (OSData *copy = OSData::withBytes(data, 8)) {
            fLastSet->setObject("AWDL_AF_TX_MODE8", copy);
            copy->release();
        }
        IOLockUnlock(fLock);
    }
    return result;
}

IOReturn AirportItlwmAWDLInterface::setAWDL_PEER_TRAFFIC_REGISTRATION(
    struct apple80211_awdl_peer_traffic_registration *data)
{
    // Holds a pointer: record only the version, never echo it.
    return noteSet("AWDL_PEER_TRAFFIC_REGISTRATION", data, 0);
}

IOReturn AirportItlwmAWDLInterface::setAWDL_SYNC_FRAME_TEMPLATE(struct apple80211_awdl_sync_frame_template *data)
{
    if (!data)
        return kIOReturnBadArgument;
    noteSet("AWDL_SYNC_FRAME_TEMPLATE", data, 8);        // version, payload_len
    const size_t length = data->payload_len;
    if (data->payload && length && length <= kKeepBytes) {
        note("syncFrameTemplate.payload", data->payload, length);
        if (OSData *copy = OSData::withBytes(data->payload, (unsigned)length)) {
            setProperty("VAPAWDLSyncFrameTemplate", copy);
            copy->release();
        }
    }
    return kIOReturnSuccess;
}

static void vapNoteOob(AirportItlwmAWDLInterface *self, const char *name,
                       const struct apple80211_awdl_oob_request *data)
{
    // Tahoe: a 0x28-byte header, the length at 0x28 and a kernel pointer at 0x30 (ForbiddenItlwm).
    uint8_t header[0x38];
    memcpy(header, data, sizeof(header));
    uint16_t length;
    const uint8_t *source;
    memcpy(&length, header + 0x28, sizeof(length));
    memcpy(&source, header + 0x30, sizeof(source));
    bzero(header + 0x30, sizeof(source));
    char what[80];
    snprintf(what, sizeof(what), "%s.header", name);
    self->note(what, header, sizeof(header));
    if (reinterpret_cast<uintptr_t>(source) >= 0xffffff8000000000ULL && length && length <= 0x4b0) {
        snprintf(what, sizeof(what), "%s.payload", name);
        self->note(what, source, length);
        if (OSData *copy = OSData::withBytes(source, length)) {
            self->setProperty(name, copy);
            copy->release();
        }
    }
}

IOReturn AirportItlwmAWDLInterface::setAWDL_OOB_REQUEST(struct apple80211_awdl_oob_request *data)
{
    if (!data)
        return kIOReturnBadArgument;
    noteSet("AWDL_OOB_REQUEST", data, 0);
    vapNoteOob(this, "VAPAWDLOobRequest", data);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::setAWDL_OOB_AUTO_REQUEST(struct apple80211_awdl_oob_request *data)
{
    if (!data)
        return kIOReturnBadArgument;
    noteSet("AWDL_OOB_AUTO_REQUEST", data, 0);
    vapNoteOob(this, "VAPAWDLOobAutoRequest", data);
    return kIOReturnSuccess;
}

#endif /* __IO80211_TARGET >= __MAC_26_0 */
