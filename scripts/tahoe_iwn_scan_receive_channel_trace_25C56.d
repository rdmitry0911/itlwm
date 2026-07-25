/*
 * Read-only, identity-free IWN scan receive-channel observer for Tahoe
 * 25C56 x86_64.
 *
 * This program neither initiates nor alters a scan, association, radio state,
 * saved profile, Keychain item, AP, route, or driver property.  It observes
 * the net80211 entry that receives beacon/probe-response frames after
 * the IWN firmware has delivered them, plus the later rate-setup call that
 * proves the same frame passed mandatory IE/channel validation and reached a
 * scan-cache candidate.  It reads the existing rxinfo channel byte and emits
 * bounded counters for a public test set; it never reads or formats frame
 * payload, network name, access-point address, client address, RSSI, or any
 * other identity.
 *
 * ieee80211_rxinfo has rxi_chan at byte offset 12 on the loaded x86_64 build:
 * u32 flags, u32 timestamp, int RSSI, u8 channel.  At this C-function FBT
 * entry arg3 is ieee80211_rxinfo* and arg4 distinguishes beacon (0) from
 * probe response (nonzero).
 */

#pragma D option quiet
#pragma D option bufsize=4m

inline int IEEE80211_RXINFO_CHANNEL_OFF = 12;

dtrace:::BEGIN
{
    self->recv_probe_depth = 0;
    received_management_frames = 0;
    received_beacons = 0;
    received_probe_responses = 0;
    received_channel_9 = 0;
    received_channel_13 = 0;
    received_channel_149 = 0;
    received_channel_153 = 0;
    received_channel_177 = 0;
    received_other_channel = 0;
    candidate_reached = 0;
    candidate_channel_9 = 0;
    candidate_channel_13 = 0;
    candidate_channel_149 = 0;
    candidate_channel_153 = 0;
    candidate_channel_177 = 0;
    candidate_other_channel = 0;
    dropped_before_candidate = 0;
    dropped_channel_9 = 0;
    dropped_channel_13 = 0;
    dropped_channel_149 = 0;
    dropped_channel_153 = 0;
    dropped_channel_177 = 0;
    dropped_other_channel = 0;
    trace_errors = 0;
}

fbt:com.zxystd.AirportItlwm:_Z25ieee80211_recv_probe_respP12ieee80211comP6__mbufP14ieee80211_nodeP16ieee80211_rxinfoi:entry
/arg3 != 0/
{
    this->channel = *(uint8_t *)((uintptr_t)arg3 +
        IEEE80211_RXINFO_CHANNEL_OFF);
    if (self->recv_probe_depth == 0) {
        self->recv_probe_channel = this->channel;
        self->recv_probe_candidate = 0;
    }
    self->recv_probe_depth++;
    received_management_frames++;
    if (arg4 == 0)
        received_beacons++;
    else
        received_probe_responses++;

    if (this->channel == 9)
        received_channel_9++;
    else if (this->channel == 13)
        received_channel_13++;
    else if (this->channel == 149)
        received_channel_149++;
    else if (this->channel == 153)
        received_channel_153++;
    else if (this->channel == 177)
        received_channel_177++;
    else
        received_other_channel++;
}

/* This call is made once after the probe-response parser accepted mandatory
 * elements and channel consistency, found/allocated a node, and assigned its
 * channel.  The thread-local interval ties it to the current receive frame. */
fbt:com.zxystd.AirportItlwm:_Z21ieee80211_setup_ratesP12ieee80211comP14ieee80211_nodePKhS4_i:entry
/self->recv_probe_depth > 0/
{
    self->recv_probe_candidate = 1;
}

fbt:com.zxystd.AirportItlwm:_Z25ieee80211_recv_probe_respP12ieee80211comP6__mbufP14ieee80211_nodeP16ieee80211_rxinfoi:return
/self->recv_probe_depth > 0/
{
    self->recv_probe_depth--;
    if (self->recv_probe_depth == 0) {
        if (self->recv_probe_candidate != 0) {
            candidate_reached++;
            if (self->recv_probe_channel == 9)
                candidate_channel_9++;
            else if (self->recv_probe_channel == 13)
                candidate_channel_13++;
            else if (self->recv_probe_channel == 149)
                candidate_channel_149++;
            else if (self->recv_probe_channel == 153)
                candidate_channel_153++;
            else if (self->recv_probe_channel == 177)
                candidate_channel_177++;
            else
                candidate_other_channel++;
        } else {
            dropped_before_candidate++;
            if (self->recv_probe_channel == 9)
                dropped_channel_9++;
            else if (self->recv_probe_channel == 13)
                dropped_channel_13++;
            else if (self->recv_probe_channel == 149)
                dropped_channel_149++;
            else if (self->recv_probe_channel == 153)
                dropped_channel_153++;
            else if (self->recv_probe_channel == 177)
                dropped_channel_177++;
            else
                dropped_other_channel++;
        }
        self->recv_probe_channel = 0;
        self->recv_probe_candidate = 0;
    }
}

dtrace:::ERROR
{
    trace_errors++;
}

dtrace:::END
{
    printf("IWN_SCAN_RECEIVE_CHANNELS v=2 received_management_frames=%llu received_beacons=%llu received_probe_responses=%llu received_channel_9=%llu received_channel_13=%llu received_channel_149=%llu received_channel_153=%llu received_channel_177=%llu received_other_channel=%llu candidate_reached=%llu candidate_channel_9=%llu candidate_channel_13=%llu candidate_channel_149=%llu candidate_channel_153=%llu candidate_channel_177=%llu candidate_other_channel=%llu dropped_before_candidate=%llu dropped_channel_9=%llu dropped_channel_13=%llu dropped_channel_149=%llu dropped_channel_153=%llu dropped_channel_177=%llu dropped_other_channel=%llu requested_9_13_153_received=%u requested_9_13_153_candidate=%u trace_errors=%llu\n",
           (uint64_t)received_management_frames,
           (uint64_t)received_beacons,
           (uint64_t)received_probe_responses,
           (uint64_t)received_channel_9,
           (uint64_t)received_channel_13,
           (uint64_t)received_channel_149,
           (uint64_t)received_channel_153,
           (uint64_t)received_channel_177,
           (uint64_t)received_other_channel,
           (uint64_t)candidate_reached,
           (uint64_t)candidate_channel_9,
           (uint64_t)candidate_channel_13,
           (uint64_t)candidate_channel_149,
           (uint64_t)candidate_channel_153,
           (uint64_t)candidate_channel_177,
           (uint64_t)candidate_other_channel,
           (uint64_t)dropped_before_candidate,
           (uint64_t)dropped_channel_9,
           (uint64_t)dropped_channel_13,
           (uint64_t)dropped_channel_149,
           (uint64_t)dropped_channel_153,
           (uint64_t)dropped_channel_177,
           (uint64_t)dropped_other_channel,
           (received_channel_9 != 0 && received_channel_13 != 0 &&
            received_channel_153 != 0) ? 1U : 0U,
           (candidate_channel_9 != 0 && candidate_channel_13 != 0 &&
            candidate_channel_153 != 0) ? 1U : 0U,
           (uint64_t)trace_errors);
}
