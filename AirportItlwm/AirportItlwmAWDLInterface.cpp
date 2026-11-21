//
//  AirportItlwmAWDLInterface.cpp
//  AirportItlwm (v-Airport)
//
//  awdl0 (see the header). The stack's settings become the firmware's
//  configuration (VAP_CMD_AWDL_CONFIG, always whole) and its frame templates
//  (VAP_CMD_AWDL_TEMPLATE); both are sent from the controller's work loop, a
//  moment after the stack set them, so a setAWDL_* call never waits for the
//  host. Getters echo the last value set where this tree knows the structure
//  size. Shape and the native rules it relies on (the oob request's pointer at
//  0x30, the 24-byte sync state, defaults of sync enabled/state, the 72-byte
//  action frame metadata with the RSSI at 0x2c) are from ForbiddenItlwm's
//  ItlAWDLInterface (GPL-2.0); the 25C56 entry points are checked in v-Airport
//  lab/abi (IO80211Buffer has no virtual methods there, IO80211BufferPool's
//  getBuffer is virtual slot 0x118).
//

#include "AirportItlwmAWDLInterface.hpp"

#if __IO80211_TARGET >= __MAC_26_0

#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <sys/proc.h>
#include <kern/clock.h>

#include "Airport/IOSkywalkNetworkPacket.h"

OSDefineMetaClassAndStructors(AirportItlwmAWDLInterface, IO80211AWDLProtocol)

// The controller's way to the firmware (AirportItlwmV2.cpp -> the v-Airport HAL).
extern "C" IOReturn airportItlwmVapAWDLRequest(IO80211Controller *controller, uint16_t type,
                                               const void *payload, uint32_t length);
extern "C" IOReturn airportItlwmVapAWDLTransmit(IO80211Controller *controller, const void *frame,
                                                uint32_t length);

// Apple's management frame buffers and the peer manager's input (25C56 exports).
class IO80211Buffer;
class IO80211BufferPool;
extern "C" IO80211BufferPool *vapAWDLMgmtFramePool(IO80211Controller *)
    __asm__("__ZN17IO80211Controller16getMgmtFramePoolEv");
extern "C" void *vapAWDLBufferBytes(IO80211Buffer *)
    __asm__("__ZN13IO80211Buffer14getBytesNoCopyEv");
extern "C" size_t vapAWDLBufferCapacity(const IO80211Buffer *)
    __asm__("__ZNK13IO80211Buffer11getCapacityEv");
extern "C" void vapAWDLBufferSetLength(IO80211Buffer *, size_t)
    __asm__("__ZN13IO80211Buffer9setLengthEm");
extern "C" void vapAWDLBufferReturn(IO80211Buffer *)
    __asm__("__ZN13IO80211Buffer12returnBufferEv");
// Consumes the buffer on every path: the peer manager keeps it or the pool gets it back.
extern "C" void vapAWDLActionInput(IO80211VirtualInterface *, IO80211Buffer *, unsigned int, void *,
                                   unsigned long)
    __asm__("__ZN23IO80211VirtualInterface11bpfTapInputEP13IO80211BufferjPvm");

static constexpr unsigned kPoolGetBufferSlot = 0x118 / sizeof(void *);
static constexpr unsigned kManagementFrameType = 12;   // action frame (the native callers pass 0xc)

// What IO80211AWDLPeerManager::actionFrameInput takes with an action frame: at most 72 bytes.
struct VapAWDLFrameMetadata {
    uint8_t peer[6];
    uint8_t reserved[6];
    uint32_t version;
    uint32_t channel;
    uint32_t channelFlags;
    uint8_t tail[20];
    int32_t rssi;           // dBm; a peer is taken only above the stack's threshold
    uint8_t tail2[24];
};
static_assert(sizeof(VapAWDLFrameMetadata) == 72, "AWDL management metadata");
static_assert(__offsetof(VapAWDLFrameMetadata, rssi) == 0x2c, "AWDL metadata RSSI");

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
    fController = controller;
    fWorkLoop = nullptr;
    fFlushTimer = nullptr;
    fFlushPending = false;
    fConfigDirty = fFirmwareRunning = false;
    fConfigResult = kIOReturnSuccess;
    bzero(&fConfig, sizeof(fConfig));
    bzero(fTemplateLength, sizeof(fTemplateLength));
    bzero(fTemplateDirty, sizeof(fTemplateDirty));
    fConfig.presence_mode = 4;
    fConfig.master_channel = 6;
    fConfig.seq_step = 3;
    fConfig.aw_period_tu = fConfig.aw_length_tu = fConfig.ext_length_tu = 16;
    fConfig.sync_frame_period_tu = 110;
    for (unsigned i = 0; i < VAP_AWDL_SEQ_LEN; ++i)
        fConfig.seq[i] = 6;
    static const uint8_t bssid[6] = { 0x00, 0x25, 0x00, 0xff, 0x94, 0x73 };
    memcpy(fConfig.bssid, bssid, sizeof(bssid));
    if (address)
        memcpy(fConfig.addr, address->octet, 6);
    fRingLock = IOSimpleLockAlloc();
    fTxCompletion = nullptr;
    fRxCompletion = nullptr;
    fRxPool = nullptr;
    fTxDoneHead = fTxDoneCount = fRxHead = fRxCount = 0;
    fTxFrames = fTxDropped = fRxFrames = fRxDropped = fActionFrames = fActionDropped = 0;
    fPeersAdded = fPeersRemoved = fStateEvents = 0;
    if (!fRingLock)
        return false;
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
    detachDataPath();
    if (fRingLock) {
        IOSimpleLockFree(fRingLock);
        fRingLock = nullptr;
    }
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
    auto *self = OSDynamicCast(AirportItlwmAWDLInterface, owner);
    if (!self)
        return 0;
    self->noteTransmit(packets, count);
    return self->transmitPackets(packets, count);
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
    if (fLock) {
        IOLockLock(fLock);
        memcpy(fConfig.addr, address.octet, 6);
        IOLockUnlock(fLock);
        configChanged();
    }
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
VAP_SET_KNOWN(AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter)
VAP_SET_KNOWN(AWDL_SYNC_STATE, apple80211_awdl_sync_state)
VAP_SET_KNOWN(AWDL_ELECTION_RSSI_THRESHOLDS, apple80211_awdl_election_rssi_thresholds)
VAP_SET_UNKNOWN(PEER_CACHE_CONTROL, apple80211_peer_cache_control)
VAP_SET_UNKNOWN(AWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
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

// ---- settings the firmware runs on -------------------------------------------
//
// Each records the call, updates the firmware's configuration under fLock and
// asks for it to be sent; flush() sends the whole configuration once for any
// number of changes in between.

#define VAP_SET_CONFIG(Name, Type, Apply) \
    IOReturn AirportItlwmAWDLInterface::set##Name(struct Type *data) \
    { \
        IOReturn result = noteSet(#Name, data, sizeof(struct Type)); \
        if (result != kIOReturnSuccess || !fLock) \
            return result; \
        IOLockLock(fLock); \
        { Apply; } \
        IOLockUnlock(fLock); \
        configChanged(); \
        return kIOReturnSuccess; \
    }

VAP_SET_CONFIG(AWDL_SYNC_PARAMS, apple80211_awdl_sync_params,
    if (data->availability_window_period) fConfig.aw_period_tu = (uint16_t)data->availability_window_period;
    if (data->availability_window_length) fConfig.aw_length_tu = (uint16_t)data->availability_window_length;
    fConfig.ext_length_tu = (uint16_t)data->extension_length;
    if (data->synchronization_frame_period)
        fConfig.sync_frame_period_tu = (uint16_t)data->synchronization_frame_period)
VAP_SET_CONFIG(AWDL_SYNC_ENABLED, apple80211_awdl_sync_enabled,
    fConfig.enabled = data->enabled ? 1 : 0)
VAP_SET_CONFIG(AWDL_ELECTION_ID, apple80211_awdl_election_id,
    fConfig.election_id = data->election_id)
VAP_SET_CONFIG(AWDL_BSSID, apple80211_awdl_bssid,
    memcpy(fConfig.bssid, data->bssid, 6))
VAP_SET_CONFIG(AWDL_ELECTION_METRIC, apple80211_awdl_election_metric,
    fConfig.election_metric = data->metric)
VAP_SET_CONFIG(AWDL_MASTER_CHANNEL, apple80211_awdl_master_channel,
    if (data->master_channel && data->master_channel <= 0xff) fConfig.master_channel = (uint8_t)data->master_channel)
VAP_SET_CONFIG(AWDL_SECONDARY_MASTER_CHANNEL, apple80211_awdl_secondary_master_channel,
    fConfig.secondary_master_channel = (uint8_t)(data->secondary_master_channel <= 0xff ? data->secondary_master_channel : 0))
VAP_SET_CONFIG(AWDL_PRESENCE_MODE, apple80211_awdl_presence_mode,
    if (data->mode && data->mode <= 16) fConfig.presence_mode = (uint8_t)data->mode)
// 25C56 sends 12-byte slots from offset 16, the channel in the first 32-bit word (ForbiddenItlwm
// read the same on 25G241): (6, 8, 1) for a slot on channel 6, (0, 0, 1) for one away.
VAP_SET_CONFIG(AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, apple80211_awdl_sync_channel_sequence,
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const unsigned slots = data->length ? data->length : VAP_AWDL_SEQ_LEN;
    fConfig.seq_step = data->step_count;
    for (unsigned i = 0; i < VAP_AWDL_SEQ_LEN; ++i) {
        uint32_t channel = 0;
        if (i < slots && 16 + 12 * i + 4 <= sizeof(*data))
            memcpy(&channel, raw + 16 + 12 * i, sizeof(channel));
        fConfig.seq[i] = channel <= 0xff ? (uint8_t)channel : 0;
    })

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
    if (data->payload && length <= kTemplateMax)
        templateChanged(VAP_AWDL_TMPL_SYNC, static_cast<const uint8_t *>(data->payload), length);
    return kIOReturnSuccess;
}

// The frame's TLVs of an out-of-band request: length at 0x28, a kernel pointer at 0x30 (ForbiddenItlwm).
static const uint8_t *vapOobPayload(const struct apple80211_awdl_oob_request *data, uint16_t *length)
{
    const uint8_t *header = reinterpret_cast<const uint8_t *>(data);
    const uint8_t *source;
    memcpy(length, header + 0x28, sizeof(*length));
    memcpy(&source, header + 0x30, sizeof(source));
    if (reinterpret_cast<uintptr_t>(source) < 0xffffff8000000000ULL || !*length || *length > 0x4b0)
        return nullptr;
    return source;
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
    uint16_t length = 0;
    if (const uint8_t *tlvs = vapOobPayload(data, &length))
        templateChanged(VAP_AWDL_TMPL_ONCE, tlvs, length);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAWDLInterface::setAWDL_OOB_AUTO_REQUEST(struct apple80211_awdl_oob_request *data)
{
    if (!data)
        return kIOReturnBadArgument;
    noteSet("AWDL_OOB_AUTO_REQUEST", data, 0);
    vapNoteOob(this, "VAPAWDLOobAutoRequest", data);
    uint16_t length = 0;
    if (const uint8_t *tlvs = vapOobPayload(data, &length))
        templateChanged(VAP_AWDL_TMPL_MI, tlvs, length);
    return kIOReturnSuccess;
}

// ---- the firmware side ---------------------------------------------------------

void AirportItlwmAWDLInterface::configChanged()
{
    if (!fLock)
        return;
    IOLockLock(fLock);
    fConfigDirty = true;
    IOLockUnlock(fLock);
    kick();
}

void AirportItlwmAWDLInterface::templateChanged(unsigned kind, const uint8_t *tlvs, size_t length)
{
    if (!fLock || kind > VAP_AWDL_TMPL_ONCE || length > kTemplateMax || (length && !tlvs))
        return;
    IOLockLock(fLock);
    // The stack sets the same template again and again: only a new one goes down.
    const bool same = kind != VAP_AWDL_TMPL_ONCE && fTemplateLength[kind] == length &&
        !memcmp(fTemplate[kind], tlvs, length);
    if (!same) {
        memcpy(fTemplate[kind], tlvs, length);
        fTemplateLength[kind] = (uint16_t)length;
        fTemplateDirty[kind] = true;
    }
    IOLockUnlock(fLock);
    if (!same)
        kick();
}

void AirportItlwmAWDLInterface::kick()
{
    IOTimerEventSource *timer = fFlushTimer;
    if (!timer || __atomic_exchange_n(&fFlushPending, true, __ATOMIC_ACQ_REL))
        return;
    timer->setTimeoutUS(500);       // the stack sets several things in a row
}

void AirportItlwmAWDLInterface::flushAction(OSObject *owner, IOTimerEventSource *)
{
    if (auto *self = OSDynamicCast(AirportItlwmAWDLInterface, owner))
        self->flush();
}

// In the work loop: the configuration if it changed, then any new template.
void AirportItlwmAWDLInterface::flush()
{
    struct vap_awdl_config config;
    bool sendConfig;

    __atomic_store_n(&fFlushPending, false, __ATOMIC_RELEASE);
    if (!fLock || !fController)
        return;
    IOLockLock(fLock);
    sendConfig = fConfigDirty;
    fConfigDirty = false;
    config = fConfig;
    IOLockUnlock(fLock);
    if (sendConfig && (config.enabled || fFirmwareRunning)) {
        const bool was = fFirmwareRunning;
        fConfigResult = airportItlwmVapAWDLRequest(fController, VAP_CMD_AWDL_CONFIG, &config, sizeof(config));
        fFirmwareRunning = config.enabled && fConfigResult == kIOReturnSuccess;
        char text[96];
        snprintf(text, sizeof(text), "%s ch %u seq %u%u%u%u -> 0x%x", config.enabled ? "on" : "off",
                 config.master_channel, config.seq[0], config.seq[4], config.seq[8], config.seq[12], fConfigResult);
        setProperty("VAPAWDLFirmwareConfig", text);
        if (fFirmwareRunning != was)
            IOLog("VAPAWDL firmware AWDL %s (0x%x)\n", fFirmwareRunning ? "running" : "stopped", fConfigResult);
        if (fFirmwareRunning && !was) {
            // A fresh start in the firmware: it has no templates yet.
            IOLockLock(fLock);
            for (unsigned k = VAP_AWDL_TMPL_SYNC; k <= VAP_AWDL_TMPL_MI; ++k)
                if (fTemplateLength[k])
                    fTemplateDirty[k] = true;
            IOLockUnlock(fLock);
        }
    }
    if (!fFirmwareRunning)
        return;
    for (unsigned kind = VAP_AWDL_TMPL_SYNC; kind <= VAP_AWDL_TMPL_ONCE; ++kind) {
        auto *t = reinterpret_cast<struct vap_awdl_template *>(fSend);
        IOLockLock(fLock);
        const bool dirty = fTemplateDirty[kind];
        fTemplateDirty[kind] = false;
        bzero(t, sizeof(*t));
        t->kind = (uint16_t)kind;
        t->len = fTemplateLength[kind];
        memcpy(fSend + sizeof(*t), fTemplate[kind], t->len);
        IOLockUnlock(fLock);
        if (!dirty)
            continue;
        if (kind == VAP_AWDL_TMPL_ONCE)
            memset(t->dst, 0xff, sizeof(t->dst));
        const IOReturn result = airportItlwmVapAWDLRequest(fController, VAP_CMD_AWDL_TEMPLATE, fSend,
                                                           (uint32_t)(sizeof(*t) + t->len));
        if (result != kIOReturnSuccess) {
            char text[48];
            snprintf(text, sizeof(text), "kind %u len %u -> 0x%x", kind, t->len, result);
            setProperty("VAPAWDLFirmwareTemplateError", text);
        }
    }
}

// ---- data path ----------------------------------------------------------------

void AirportItlwmAWDLInterface::attachDataPath(IOWorkLoop *workLoop, IOSkywalkTxCompletionQueue *txCompletion,
                                               IOSkywalkRxCompletionQueue *rxCompletion,
                                               IOSkywalkPacketBufferPool *rxPool)
{
    fTxCompletion = txCompletion;
    fRxCompletion = rxCompletion;
    fRxPool = rxPool;
    fWorkLoop = workLoop;
    if (workLoop && !fFlushTimer) {
        fFlushTimer = IOTimerEventSource::timerEventSource(this, flushAction);
        if (fFlushTimer && workLoop->addEventSource(fFlushTimer) != kIOReturnSuccess)
            OSSafeReleaseNULL(fFlushTimer);
    }
    setProperty("VAPAWDLStage", fFlushTimer ? "firmware" : "firmware-no-timer");
    if (fConfigDirty)
        kick();
}

void AirportItlwmAWDLInterface::detachDataPath()
{
    IOSkywalkPacket *rx[kRing];
    unsigned nrx = 0, ntx;

    if (fFlushTimer) {
        fFlushTimer->cancelTimeout();
        if (fWorkLoop)
            fWorkLoop->removeEventSource(fFlushTimer);
        OSSafeReleaseNULL(fFlushTimer);
    }
    if (fFirmwareRunning && fController && fLock) {
        struct vap_awdl_config config;
        IOLockLock(fLock);
        config = fConfig;
        IOLockUnlock(fLock);
        config.enabled = 0;
        airportItlwmVapAWDLRequest(fController, VAP_CMD_AWDL_CONFIG, &config, sizeof(config));
        fFirmwareRunning = false;
    }
    if (!fRingLock)
        return;
    IOSimpleLockLock(fRingLock);
    while (fRxCount) {
        rx[nrx++] = fRxReady[fRxHead];
        fRxHead = (fRxHead + 1) % kRing;
        --fRxCount;
    }
    // Accepted transmit packets not completed yet can't be any more: the queues go (as ForbiddenItlwm).
    ntx = fTxDoneCount;
    fTxDoneCount = fTxDoneHead = 0;
    IOSimpleLockUnlock(fRingLock);
    for (unsigned i = 0; i < nrx; ++i) {
        rx[i]->completeWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0);
        if (fRxPool)
            fRxPool->deallocatePacket(rx[i]);
    }
    if (ntx)
        setProperty("VAPAWDLTxAbandonedAtStop", ntx, 32);
    fTxCompletion = nullptr;
    fRxCompletion = nullptr;
    fRxPool = nullptr;
}

void AirportItlwmAWDLInterface::publishCounters()
{
    char text[128];
    snprintf(text, sizeof(text), "tx %u drop %u, rx %u drop %u, action %u drop %u, peers +%u -%u, states %u",
             fTxFrames, fTxDropped, fRxFrames, fRxDropped, fActionFrames, fActionDropped, fPeersAdded,
             fPeersRemoved, fStateEvents);
    setProperty("VAPAWDLData", text);
}

static bool vapAWDLCountShown(uint32_t n)
{
    return n <= 8 || !(n % 64);
}

UInt32 AirportItlwmAWDLInterface::transmitPackets(IOSkywalkPacket *const *packets, UInt32 count)
{
    UInt32 accepted = 0;

    if (!fTxCompletion || !packets || !fRingLock)
        return 0;
    for (; accepted < count; ++accepted) {
        IOSkywalkPacket *packet = packets[accepted];
        IOSimpleLockLock(fRingLock);
        const bool room = fTxDoneCount < kRing;
        IOSimpleLockUnlock(fRingLock);
        if (!room)
            break;                  // the rest stays with Skywalk until completions free the ring
        if (!packet)
            continue;
        const uint8_t *base = static_cast<const uint8_t *>(packet->getDataVirtualAddress());
        const UInt32 offset = packet->getDataOffset();
        const UInt32 length = packet->getDataLength();
        IOReturn result = kIOReturnBadArgument;
        if (base && length >= 14 && offset <= kBufferSize && length <= kBufferSize - offset)
            result = airportItlwmVapAWDLTransmit(fController, base + offset, length);
        if (result == kIOReturnSuccess)
            ++fTxFrames;
        else
            ++fTxDropped;           // the firmware had no room or the frame is not for AWDL: as a full NIC
        IOSimpleLockLock(fRingLock);
        fTxDone[(fTxDoneHead + fTxDoneCount) % kRing] = packet;
        ++fTxDoneCount;
        IOSimpleLockUnlock(fRingLock);
    }
    if (accepted)
        fTxCompletion->requestEnqueue(nullptr, 0);
    if (vapAWDLCountShown(fTxFrames + fTxDropped))
        publishCounters();
    return accepted;
}

UInt32 AirportItlwmAWDLInterface::popTransmitted(IOSkywalkPacket **packets, UInt32 count)
{
    UInt32 n = 0;

    if (!packets || !fRingLock)
        return 0;
    IOSimpleLockLock(fRingLock);
    while (n < count && fTxDoneCount) {
        packets[n++] = fTxDone[fTxDoneHead];
        fTxDoneHead = (fTxDoneHead + 1) % kRing;
        --fTxDoneCount;
    }
    IOSimpleLockUnlock(fRingLock);
    return n;
}

UInt32 AirportItlwmAWDLInterface::popReceived(IOSkywalkPacket **packets, UInt32 count)
{
    UInt32 n = 0;

    if (!packets || !fRingLock)
        return 0;
    IOSimpleLockLock(fRingLock);
    while (n < count && fRxCount) {
        packets[n++] = fRxReady[fRxHead];
        fRxHead = (fRxHead + 1) % kRing;
        --fRxCount;
    }
    IOSimpleLockUnlock(fRingLock);
    return n;
}

// One 802.3 frame from the firmware into awdl0, as ForbiddenItlwm delivers it: straight to the
// receive ring (headroom 0, a 14-byte link header), not through IO80211's per-peer input.
void AirportItlwmAWDLInterface::inputData(const uint8_t *frame, uint32_t length)
{
    IOSkywalkPacket *packet = nullptr;
    bool staged = false;

    if (!fRxPool || !fRxCompletion || !fRingLock || length < 14 || length > kBufferSize) {
        ++fRxDropped;
        return;
    }
    if (fRxPool->allocatePacket(1, &packet, 0) != kIOReturnSuccess || !packet) {
        ++fRxDropped;
        return;
    }
    if (packet->prepareWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0) == kIOReturnSuccess) {
        void *base = packet->getDataVirtualAddress();
        if (base) {
            memcpy(base, frame, length);
            if (packet->setDataOffsetAndLength(0, length) == kIOReturnSuccess) {
                auto *network = static_cast<IOSkywalkNetworkPacket *>(packet);
                network->setHeadroom(0);
                network->setLinkHeaderLength(14);
                IOSimpleLockLock(fRingLock);
                if (fRxCount < kRing) {
                    fRxReady[(fRxHead + fRxCount) % kRing] = packet;
                    ++fRxCount;
                    staged = true;
                }
                IOSimpleLockUnlock(fRingLock);
            }
        }
    }
    if (!staged) {
        packet->completeWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0);
        fRxPool->deallocatePacket(packet);
        ++fRxDropped;
        return;
    }
    ++fRxFrames;
    fRxCompletion->requestEnqueue(nullptr, 0);
    if (vapAWDLCountShown(fRxFrames))
        publishCounters();
}

// A peer's action frame into Apple's peer manager, with the RSSI it needs to take the peer.
void AirportItlwmAWDLInterface::inputAction(const uint8_t *frame, uint32_t length, uint16_t freq, int8_t rssi)
{
    if (!fController || length < 24 + 8 || frame[24] != 127) {
        ++fActionDropped;
        return;
    }
    IO80211BufferPool *pool = vapAWDLMgmtFramePool(fController);
    if (!pool) {
        ++fActionDropped;
        return;
    }
    auto getBuffer = reinterpret_cast<IO80211Buffer *(*)(IO80211BufferPool *)>(
        (*reinterpret_cast<void *const *const *>(pool))[kPoolGetBufferSlot]);
    IO80211Buffer *buffer = getBuffer(pool);
    if (!buffer) {
        ++fActionDropped;
        return;
    }
    uint8_t *bytes = static_cast<uint8_t *>(vapAWDLBufferBytes(buffer));
    const size_t body = length - 24;    // the peer manager reads from the category on
    if (!bytes || body > vapAWDLBufferCapacity(buffer)) {
        vapAWDLBufferReturn(buffer);
        ++fActionDropped;
        return;
    }
    memcpy(bytes, frame + 24, body);
    vapAWDLBufferSetLength(buffer, body);
    VapAWDLFrameMetadata meta;
    bzero(&meta, sizeof(meta));
    memcpy(meta.peer, frame + 10, 6);
    meta.version = 1;
    const unsigned channel = freq >= 5000 ? (freq - 5000) / 5 : freq == 2484 ? 14 : freq > 2407 ? (freq - 2407) / 5 : 0;
    meta.channel = channel;
    meta.channelFlags = channel <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ;
    meta.rssi = rssi;
    vapAWDLActionInput(this, buffer, kManagementFrameType, &meta, sizeof(meta));
    ++fActionFrames;
    if (vapAWDLCountShown(fActionFrames))
        publishCounters();
}

void AirportItlwmAWDLInterface::firmwareEvent(uint16_t type, const void *payload, uint32_t length)
{
    char text[128];

    if (type == VAP_EVT_AWDL_STATE && length >= sizeof(struct vap_evt_awdl_state)) {
        struct vap_evt_awdl_state e;
        memcpy(&e, payload, sizeof(e));
        ++fStateEvents;
        snprintf(text, sizeof(text), "%s ch %u peers %u master %02x:%02x:%02x:%02x:%02x:%02x metric %u hops %u",
                 e.state == VAP_AWDL_STATE_MASTER ? "master" : e.state == VAP_AWDL_STATE_SLAVE ? "slave" : "off",
                 e.channel, e.n_peers, e.master[0], e.master[1], e.master[2], e.master[3], e.master[4],
                 e.master[5], e.master_metric, e.height);
        setProperty("VAPAWDLFirmwareState", text);
        IOLog("VAPAWDL firmware %s\n", text);
    } else if (type == VAP_EVT_AWDL_PEER && length >= sizeof(struct vap_evt_awdl_peer)) {
        struct vap_evt_awdl_peer e;
        memcpy(&e, payload, sizeof(e));
        e.name[sizeof(e.name) - 1] = 0;
        if (e.present)
            ++fPeersAdded;
        else
            ++fPeersRemoved;
        snprintf(text, sizeof(text), "%s %02x:%02x:%02x:%02x:%02x:%02x \"%s\"", e.present ? "added" : "removed",
                 e.addr[0], e.addr[1], e.addr[2], e.addr[3], e.addr[4], e.addr[5], e.name);
        setProperty("VAPAWDLFirmwareLastPeer", text);
        IOLog("VAPAWDL firmware peer %s\n", text);
    }
    publishCounters();
}

#endif /* __IO80211_TARGET >= __MAC_26_0 */
