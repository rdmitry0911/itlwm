/*
 * Copyright (C) 2026 itlwm contributors
 *
 * Firmware-neutral parsing and framing for AP-side Block Ack actions.  The
 * DVM (IWN) AP path has a separate station lifetime from the MVM paths, so
 * keep the over-the-air contract here and let each backend own its ADD_STA
 * command and reorder resources.
 */

#ifndef ItlApBlockAckRuntime_hpp
#define ItlApBlockAckRuntime_hpp

#include <net80211/ieee80211_priv.h>

enum ItlApBlockAckActionKind : uint8_t {
    kItlApBlockAckNone = 0,
    kItlApBlockAckAddRequest,
    kItlApBlockAckDelete,
};

struct ItlApBlockAckAction {
    uint8_t kind;
    uint8_t token;
    uint8_t tid;
    uint16_t ssn;
    uint16_t window;
    uint16_t timeout;
    bool peerInitiator;
};

enum { kItlApRxBaTidCount = 8, kItlApRxBaMaxWindow = 64 };

struct ItlApRxBaReady;
typedef void (*ItlApRxBaDeliver)(void *, struct ItlApRxBaReady *);

struct ItlApRxBaBufferedFrame {
    mbuf_t packet;
    mbuf_t packetTail;
    size_t frameLength;
    uint32_t rxFlags;
    uint8_t descriptorType;
    bool hardwareDecrypted;
    bool amsdu;
    bool amsduComplete;
    uint8_t lastSubframeIndex;
    uint16_t sequence;
};

struct ItlApRxBaRuntime {
    bool enabled;
    uint16_t head;
    uint16_t window;
    uint16_t stored;
    CTimeout *gapTimer;
    void *deliveryOwner;
    ItlApRxBaDeliver deliver;
    bool lastReleasedValid;
    uint16_t lastReleasedSequence;
    uint8_t lastReleasedSubframeIndex;
    struct ItlApRxBaBufferedFrame slots[kItlApRxBaMaxWindow];
};

struct ItlApRxBaReady {
    size_t count;
    struct ItlApRxBaBufferedFrame frames[kItlApRxBaMaxWindow + 1];
};

static inline void itl_ap_rx_ba_gap_timeout(void *);

static inline void
itl_ap_rx_ba_frame_purge(struct ItlApRxBaBufferedFrame *frame)
{
    if (frame == NULL)
        return;
    mbuf_t packet = frame->packet;
    while (packet != NULL) {
        mbuf_t next = mbuf_nextpkt(packet);
        mbuf_setnextpkt(packet, NULL);
        mbuf_freem(packet);
        packet = next;
    }
    bzero(frame, sizeof(*frame));
}

static inline void
itl_ap_rx_ba_stop(struct ItlApRxBaRuntime *runtime)
{
    if (runtime == NULL)
        return;
    timeout_del(&runtime->gapTimer);
    timeout_free(&runtime->gapTimer);
    for (size_t index = 0; index < kItlApRxBaMaxWindow; index++) {
        if (runtime->slots[index].packet != NULL)
            itl_ap_rx_ba_frame_purge(&runtime->slots[index]);
    }
    bzero(runtime, sizeof(*runtime));
}

static inline void
itl_ap_rx_ba_start(struct ItlApRxBaRuntime *runtime, uint16_t ssn,
                   uint16_t window, void *deliveryOwner,
                   ItlApRxBaDeliver deliver)
{
    if (runtime == NULL)
        return;
    itl_ap_rx_ba_stop(runtime);
    runtime->enabled = true;
    runtime->head = ssn & 0x0fff;
    runtime->window = MIN(window,
        static_cast<uint16_t>(kItlApRxBaMaxWindow));
    if (runtime->window == 0)
        runtime->window = kItlApRxBaMaxWindow;
    runtime->deliveryOwner = deliveryOwner;
    runtime->deliver = deliver;
    timeout_set(&runtime->gapTimer, itl_ap_rx_ba_gap_timeout, runtime);
}

static inline void
itl_ap_rx_ba_ready_append(struct ItlApRxBaReady *ready,
                          struct ItlApRxBaBufferedFrame *frame)
{
    if (ready == NULL || frame == NULL || frame->packet == NULL)
        return;
    if (ready->count < nitems(ready->frames)) {
        ready->frames[ready->count++] = *frame;
    } else {
        itl_ap_rx_ba_frame_purge(frame);
    }
    bzero(frame, sizeof(*frame));
}

static inline void
itl_ap_rx_ba_release_head(struct ItlApRxBaRuntime *runtime,
                          struct ItlApRxBaReady *ready)
{
    if (runtime == NULL)
        return;
    struct ItlApRxBaBufferedFrame *slot =
        &runtime->slots[runtime->head % kItlApRxBaMaxWindow];
    if (slot->packet != NULL && slot->sequence == runtime->head)
    {
        runtime->lastReleasedValid = true;
        runtime->lastReleasedSequence = slot->sequence;
        runtime->lastReleasedSubframeIndex = slot->lastSubframeIndex;
        itl_ap_rx_ba_ready_append(ready, slot);
        if (runtime->stored != 0)
            runtime->stored--;
    }
    runtime->head = static_cast<uint16_t>((runtime->head + 1) & 0x0fff);
}

static inline void
itl_ap_rx_ba_update_timer(struct ItlApRxBaRuntime *runtime)
{
    if (runtime == NULL || runtime->gapTimer == NULL)
        return;
    if (runtime->stored == 0) {
        timeout_del(&runtime->gapTimer);
    } else if (!timeout_pending(&runtime->gapTimer)) {
        timeout_add_msec(&runtime->gapTimer, IEEE80211_BA_GAP_TIMEOUT);
    }
}

/* Match net80211's BA gap timeout: skip only the leading hole, release the
 * now-contiguous run, and re-arm while later out-of-order frames remain. */
static inline void
itl_ap_rx_ba_gap_timeout(void *argument)
{
    struct ItlApRxBaRuntime *runtime =
        static_cast<struct ItlApRxBaRuntime *>(argument);
    if (runtime == NULL || !runtime->enabled || runtime->stored == 0)
        return;
    struct ItlApRxBaReady ready;
    bzero(&ready, sizeof(ready));
    for (uint16_t skipped = 0; skipped < runtime->window; skipped++) {
        struct ItlApRxBaBufferedFrame *slot =
            &runtime->slots[runtime->head % kItlApRxBaMaxWindow];
        if (slot->packet != NULL && slot->sequence == runtime->head)
            break;
        runtime->head = static_cast<uint16_t>(
            (runtime->head + 1) & 0x0fff);
    }
    while (runtime->stored != 0) {
        struct ItlApRxBaBufferedFrame *slot =
            &runtime->slots[runtime->head % kItlApRxBaMaxWindow];
        if (slot->packet == NULL || slot->sequence != runtime->head)
            break;
        itl_ap_rx_ba_release_head(runtime, &ready);
    }
    if (ready.count != 0 && runtime->deliver != NULL)
        runtime->deliver(runtime->deliveryOwner, &ready);
    for (size_t index = 0; index < ready.count; index++)
        itl_ap_rx_ba_frame_purge(&ready.frames[index]);
    itl_ap_rx_ba_update_timer(runtime);
}

/*
 * Own packet and return true only for a QoS MPDU covered by a live receive
 * BA agreement.  Every frame released in sequence is returned in ready;
 * callers feed those frames through their normal AP decapsulation path.
 */
static inline bool
itl_ap_rx_ba_reorder(struct ItlApRxBaRuntime runtimes[kItlApRxBaTidCount],
                     const uint8_t *bssid, const uint8_t *station,
                     mbuf_t packet, size_t frameLength,
                     bool hardwareDecrypted, uint32_t rxFlags,
                     uint8_t descriptorType, bool isAmsdu,
                     uint8_t subframeIndex, bool lastSubframe,
                     struct ItlApRxBaReady *ready)
{
    if (ready != NULL)
        bzero(ready, sizeof(*ready));
    if (runtimes == NULL || bssid == NULL || station == NULL ||
        packet == NULL || ready == NULL ||
        frameLength < sizeof(struct ieee80211_frame_min))
        return false;
    const struct ieee80211_frame *wh =
        mtod(packet, const struct ieee80211_frame *);

    const uint8_t type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
    const uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
    if (type == IEEE80211_FC0_TYPE_CTL &&
        subtype == IEEE80211_FC0_SUBTYPE_BAR) {
        if (frameLength < sizeof(struct ieee80211_frame_min) + 4)
            return false;
        const struct ieee80211_frame_min *bar =
            mtod(packet, const struct ieee80211_frame_min *);
        if (!IEEE80211_ADDR_EQ(bar->i_addr1, bssid) ||
            !IEEE80211_ADDR_EQ(bar->i_addr2, station))
            return false;
        const uint8_t *body = reinterpret_cast<const uint8_t *>(bar + 1);
        const uint16_t control = LE_READ_2(body);
        const uint8_t tid = static_cast<uint8_t>(
            (control & IEEE80211_BA_TID_INFO_MASK) >>
            IEEE80211_BA_TID_INFO_SHIFT);
        if (tid >= kItlApRxBaTidCount || !runtimes[tid].enabled)
            return false;
        struct ItlApRxBaRuntime *runtime = &runtimes[tid];
        const uint16_t ssn = static_cast<uint16_t>(LE_READ_2(body + 2) >> 4);
        const uint16_t distance = static_cast<uint16_t>(
            (ssn - runtime->head) & 0x0fff);
        if (distance < 0x0800) {
            for (uint16_t index = 0; index < distance; index++)
                itl_ap_rx_ba_release_head(runtime, ready);
            while (runtime->stored != 0) {
                struct ItlApRxBaBufferedFrame *slot =
                    &runtime->slots[
                        runtime->head % kItlApRxBaMaxWindow];
                if (slot->packet == NULL ||
                    slot->sequence != runtime->head)
                    break;
                itl_ap_rx_ba_release_head(runtime, ready);
            }
        }
        mbuf_freem(packet);
        itl_ap_rx_ba_update_timer(runtime);
        return true;
    }

    if (frameLength < sizeof(struct ieee80211_qosframe))
        return false;
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_DATA ||
        !ieee80211_has_qos(wh) ||
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_NODATA) != 0 ||
        (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) !=
            IEEE80211_FC1_DIR_TODS ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, bssid) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, station))
        return false;
    const uint8_t tid = static_cast<uint8_t>(
        ieee80211_get_qos(wh) & IEEE80211_QOS_TID);
    if (tid >= kItlApRxBaTidCount || !runtimes[tid].enabled)
        return false;

    struct ItlApRxBaRuntime *runtime = &runtimes[tid];
    mbuf_setnextpkt(packet, NULL);
    const uint16_t sequence = static_cast<uint16_t>(
        LE_READ_2(wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT);
    const uint16_t distance = static_cast<uint16_t>(
        (sequence - runtime->head) & 0x0fff);
    if (distance >= 0x0800) {
        if (runtime->lastReleasedValid &&
            runtime->lastReleasedSequence == sequence &&
            isAmsdu &&
            subframeIndex > runtime->lastReleasedSubframeIndex) {
            struct ItlApRxBaBufferedFrame incoming = {
                .packet = packet,
                .packetTail = packet,
                .frameLength = frameLength,
                .rxFlags = rxFlags,
                .descriptorType = descriptorType,
                .hardwareDecrypted = hardwareDecrypted,
                .amsdu = true,
                .amsduComplete = lastSubframe,
                .lastSubframeIndex = subframeIndex,
                .sequence = sequence,
            };
            runtime->lastReleasedSubframeIndex = subframeIndex;
            itl_ap_rx_ba_ready_append(ready, &incoming);
            return true;
        }
        mbuf_freem(packet); /* old/duplicate sequence */
        return true;
    }

    if (distance >= runtime->window) {
        const uint16_t advance = static_cast<uint16_t>(
            distance - runtime->window + 1);
        for (uint16_t index = 0; index < advance; index++)
            itl_ap_rx_ba_release_head(runtime, ready);
    }

    struct ItlApRxBaBufferedFrame incoming = {
        .packet = packet,
        .packetTail = packet,
        .frameLength = frameLength,
        .rxFlags = rxFlags,
        .descriptorType = descriptorType,
        .hardwareDecrypted = hardwareDecrypted,
        .amsdu = isAmsdu,
        .amsduComplete = !isAmsdu || lastSubframe,
        .lastSubframeIndex = subframeIndex,
        .sequence = sequence,
    };
    struct ItlApRxBaBufferedFrame *slot =
        &runtime->slots[sequence % kItlApRxBaMaxWindow];
    if (slot->packet != NULL && slot->sequence == sequence) {
        const bool orderedAmsduSubframe = isAmsdu && slot->amsdu &&
            !slot->amsduComplete &&
            subframeIndex > slot->lastSubframeIndex;
        if (!orderedAmsduSubframe) {
            mbuf_freem(packet);
            return true;
        }
        mbuf_setnextpkt(slot->packetTail, packet);
        mbuf_setnextpkt(packet, NULL);
        slot->packetTail = packet;
        slot->amsdu = true;
        slot->amsduComplete = isAmsdu ? lastSubframe : true;
        slot->lastSubframeIndex = subframeIndex;
        if (sequence == runtime->head && slot->amsduComplete) {
            itl_ap_rx_ba_release_head(runtime, ready);
            while (runtime->stored != 0) {
                struct ItlApRxBaBufferedFrame *next =
                    &runtime->slots[
                        runtime->head % kItlApRxBaMaxWindow];
                if (next->packet == NULL ||
                    next->sequence != runtime->head ||
                    !next->amsduComplete)
                    break;
                itl_ap_rx_ba_release_head(runtime, ready);
            }
        }
        itl_ap_rx_ba_update_timer(runtime);
        return true;
    }

    if (sequence == runtime->head && incoming.amsduComplete) {
        runtime->lastReleasedValid = true;
        runtime->lastReleasedSequence = sequence;
        runtime->lastReleasedSubframeIndex = subframeIndex;
        itl_ap_rx_ba_ready_append(ready, &incoming);
        itl_ap_rx_ba_release_head(runtime, ready);
        while (true) {
            struct ItlApRxBaBufferedFrame *slot =
                &runtime->slots[
                    runtime->head % kItlApRxBaMaxWindow];
            if (slot->packet == NULL || slot->sequence != runtime->head ||
                !slot->amsduComplete)
                break;
            itl_ap_rx_ba_release_head(runtime, ready);
        }
        itl_ap_rx_ba_update_timer(runtime);
        return true;
    }

    *slot = incoming;
    runtime->stored++;
    itl_ap_rx_ba_update_timer(runtime);
    return true;
}

/*
 * Return true once a BA action addressed to this AP has been claimed.  A
 * claimed frame can intentionally leave kind==None when it is malformed or
 * violates association/PMF policy; it must never leak into the primary STA
 * state machine.
 */
static inline bool
itl_ap_block_ack_parse(const struct ieee80211_frame *wh, size_t frameLength,
                       const uint8_t *bssid, const uint8_t *station,
                       bool associated, bool ht, bool authorized,
                       bool requireProtected, bool hardwareDecrypted,
                       struct ItlApBlockAckAction *result)
{
    if (result != NULL)
        bzero(result, sizeof(*result));
    if (wh == NULL || bssid == NULL || station == NULL || result == NULL ||
        frameLength < sizeof(*wh) + 2 ||
        (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_ACTION ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, bssid) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, station) ||
        !IEEE80211_ADDR_EQ(wh->i_addr3, bssid))
        return false;

    const bool protectedFrame =
        (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0;
    const size_t bodyOffset = sizeof(*wh) +
        (protectedFrame ? IEEE80211_CCMP_HDRLEN : 0);
    if (frameLength < bodyOffset + 2)
        return true;
    const uint8_t *body = reinterpret_cast<const uint8_t *>(wh) +
        bodyOffset;
    if (body[0] != IEEE80211_CATEG_BA)
        return false;

    if (!associated || !ht || !authorized ||
        (requireProtected &&
         (((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) == 0) ||
          !hardwareDecrypted)))
        return true;

    if (body[1] == IEEE80211_ACTION_ADDBA_REQ) {
        if (frameLength < bodyOffset + 9)
            return true;
        const uint16_t params = LE_READ_2(body + 3);
        const uint8_t tid = static_cast<uint8_t>(
            (params & IEEE80211_ADDBA_TID_MASK) >>
            IEEE80211_ADDBA_TID_SHIFT);
        /* Intel AP firmware supports the eight QoS data TIDs and immediate
         * BA only.  Delayed BA is intentionally claimed and refused. */
        if (tid >= 8 || (params & IEEE80211_ADDBA_BA_POLICY) == 0)
            return true;
        uint16_t window = static_cast<uint16_t>(
            (params & IEEE80211_ADDBA_BUFSZ_MASK) >>
            IEEE80211_ADDBA_BUFSZ_SHIFT);
        if (window == 0 || window > IEEE80211_BA_MAX_WINSZ)
            window = IEEE80211_BA_MAX_WINSZ;
        result->kind = kItlApBlockAckAddRequest;
        result->token = body[2];
        result->tid = tid;
        result->window = window;
        result->timeout = LE_READ_2(body + 5);
        result->ssn = static_cast<uint16_t>(LE_READ_2(body + 7) >> 4);
        return true;
    }

    if (body[1] == IEEE80211_ACTION_DELBA) {
        if (frameLength < bodyOffset + 6)
            return true;
        const uint16_t params = LE_READ_2(body + 2);
        const uint8_t tid = static_cast<uint8_t>(
            (params & IEEE80211_DELBA_TID_INFO_MASK) >>
            IEEE80211_DELBA_TID_INFO_SHIFT);
        if (tid >= 8)
            return true;
        result->kind = kItlApBlockAckDelete;
        result->tid = tid;
        result->peerInitiator =
            (params & IEEE80211_DELBA_INITIATOR) != 0;
        return true;
    }

    return true;
}

static inline size_t
itl_ap_block_ack_build_response(uint8_t *frame, size_t capacity,
                                const uint8_t *bssid,
                                const uint8_t *station, uint8_t token,
                                uint8_t tid, uint16_t status,
                                uint16_t window, uint16_t timeout,
                                bool protectedFrame)
{
    const size_t frameLength = sizeof(struct ieee80211_frame) + 9;
    if (frame == NULL || bssid == NULL || station == NULL || tid >= 8 ||
        capacity < frameLength)
        return 0;
    bzero(frame, frameLength);
    struct ieee80211_frame *wh =
        reinterpret_cast<struct ieee80211_frame *>(frame);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION;
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS |
        (protectedFrame ? IEEE80211_FC1_PROTECTED : 0);
    IEEE80211_ADDR_COPY(wh->i_addr1, station);
    IEEE80211_ADDR_COPY(wh->i_addr2, bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, bssid);
    uint8_t *body = reinterpret_cast<uint8_t *>(wh + 1);
    body[0] = IEEE80211_CATEG_BA;
    body[1] = IEEE80211_ACTION_ADDBA_RESP;
    body[2] = token;
    LE_WRITE_2(body + 3, status);
    const uint16_t params = status == IEEE80211_STATUS_SUCCESS ?
        static_cast<uint16_t>(IEEE80211_ADDBA_BA_POLICY |
            (tid << IEEE80211_ADDBA_TID_SHIFT) |
            (window << IEEE80211_ADDBA_BUFSZ_SHIFT)) :
        static_cast<uint16_t>(tid << IEEE80211_ADDBA_TID_SHIFT);
    LE_WRITE_2(body + 5, params);
    LE_WRITE_2(body + 7,
        status == IEEE80211_STATUS_SUCCESS ? timeout : 0);
    return frameLength;
}

#endif /* ItlApBlockAckRuntime_hpp */
