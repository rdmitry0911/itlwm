/*
 * Read-only, identity-free IWN firmware-scan channel coverage observer for
 * Tahoe 25C56 x86_64.
 *
 * This program neither initiates nor alters a scan, association, radio state,
 * saved profile, Keychain item, AP, route, or driver property.  It observes
 * the channel numbers returned while IWN constructs an already-requested
 * firmware scan command, and reports only bounded counters and membership
 * booleans for the public test set.  It never reads or formats a probe frame,
 * network name, access-point address, client address, or other identity.
 *
 * `iwn_scan_submit()` calls `ieee80211_chan2ieee()` once for every channel it
 * writes into the firmware command.  The thread-local interval below starts
 * at that constructor and closes at the IWN_CMD_SCAN command-ring entry.
 * It is accepted only if that same interval reaches the command ring.  This
 * excludes later interface-mode housekeeping and avoids treating a failed
 * allocation/build as physical scan coverage.
 */

#pragma D option quiet
#pragma D option bufsize=4m

inline int IWN_CMD_SCAN = 128;

dtrace:::BEGIN
{
    /* Declare the per-thread nesting depth before it appears in a predicate. */
    self->iwn_scan_submit_depth = 0;
    scan_submit_attempts = 0;
    command_backed_scan_builds = 0;
    command_backed_2ghz_builds = 0;
    command_backed_5ghz_builds = 0;
    command_backed_mixed_band_builds = 0;
    command_backed_channel_observations = 0;
    channel_9_present = 0;
    channel_13_present = 0;
    channel_149_present = 0;
    channel_153_present = 0;
    channel_177_present = 0;
    trace_errors = 0;
}

fbt:com.zxystd.AirportItlwm:_ZN6ItlIwn15iwn_scan_submitEP9iwn_softctiybbyjPbS2_:entry
{
    scan_submit_attempts++;
    if (self->iwn_scan_submit_depth == 0) {
        self->iwn_scan_had_command = 0;
        self->iwn_scan_channel_observations = 0;
        self->iwn_scan_has_2ghz = 0;
        self->iwn_scan_has_5ghz = 0;
        self->iwn_scan_has_9 = 0;
        self->iwn_scan_has_13 = 0;
        self->iwn_scan_has_149 = 0;
        self->iwn_scan_has_153 = 0;
        self->iwn_scan_has_177 = 0;
    }
    self->iwn_scan_submit_depth++;
}

/* The C++ member-method ABI makes arg2 the firmware command code here. */
fbt:com.zxystd.AirportItlwm:_ZN6ItlIwn26iwn_cmd_with_doorbell_hookEP9iwn_softciPKviiPFbS1_PvEPFvS1_S4_ES4_:entry
/self->iwn_scan_submit_depth > 0 && arg2 == IWN_CMD_SCAN/
{
    self->iwn_scan_had_command = 1;
}

/* On an FBT return probe arg1 is the function return value.  This function
 * is called by IWN's channel-vector constructor, not by public scan results. */
fbt:com.zxystd.AirportItlwm:_Z19ieee80211_chan2ieeeP12ieee80211comPK17ieee80211_channel:return
/self->iwn_scan_submit_depth > 0 && self->iwn_scan_had_command == 0/
{
    this->channel = (uint16_t)arg1;
    self->iwn_scan_channel_observations++;

    if (this->channel <= 14)
        self->iwn_scan_has_2ghz = 1;
    else
        self->iwn_scan_has_5ghz = 1;

    if (this->channel == 9)
        self->iwn_scan_has_9 = 1;
    else if (this->channel == 13)
        self->iwn_scan_has_13 = 1;
    else if (this->channel == 149)
        self->iwn_scan_has_149 = 1;
    else if (this->channel == 153)
        self->iwn_scan_has_153 = 1;
    else if (this->channel == 177)
        self->iwn_scan_has_177 = 1;
}

fbt:com.zxystd.AirportItlwm:_ZN6ItlIwn15iwn_scan_submitEP9iwn_softctiybbyjPbS2_:return
/self->iwn_scan_submit_depth > 0/
{
    self->iwn_scan_submit_depth--;
    if (self->iwn_scan_submit_depth == 0) {
        if (self->iwn_scan_had_command != 0) {
            command_backed_scan_builds++;
            command_backed_channel_observations +=
                self->iwn_scan_channel_observations;
            if (self->iwn_scan_has_2ghz != 0)
                command_backed_2ghz_builds++;
            if (self->iwn_scan_has_5ghz != 0)
                command_backed_5ghz_builds++;
            if (self->iwn_scan_has_2ghz != 0 &&
                self->iwn_scan_has_5ghz != 0)
                command_backed_mixed_band_builds++;
            if (self->iwn_scan_has_9 != 0)
                channel_9_present = 1;
            if (self->iwn_scan_has_13 != 0)
                channel_13_present = 1;
            if (self->iwn_scan_has_149 != 0)
                channel_149_present = 1;
            if (self->iwn_scan_has_153 != 0)
                channel_153_present = 1;
            if (self->iwn_scan_has_177 != 0)
                channel_177_present = 1;
        }
        self->iwn_scan_had_command = 0;
        self->iwn_scan_channel_observations = 0;
        self->iwn_scan_has_2ghz = 0;
        self->iwn_scan_has_5ghz = 0;
        self->iwn_scan_has_9 = 0;
        self->iwn_scan_has_13 = 0;
        self->iwn_scan_has_149 = 0;
        self->iwn_scan_has_153 = 0;
        self->iwn_scan_has_177 = 0;
    }
}

dtrace:::ERROR
{
    trace_errors++;
}

dtrace:::END
{
    printf("IWN_SCAN_CHANNEL_COVERAGE v=1 scan_submit_attempts=%llu command_backed_scan_builds=%llu command_backed_2ghz_builds=%llu command_backed_5ghz_builds=%llu command_backed_mixed_band_builds=%llu command_backed_channel_observations=%llu channel_9_present=%u channel_13_present=%u channel_149_present=%u channel_153_present=%u channel_177_present=%u requested_9_13_153_covered=%u trace_errors=%llu\n",
           (uint64_t)scan_submit_attempts,
           (uint64_t)command_backed_scan_builds,
           (uint64_t)command_backed_2ghz_builds,
           (uint64_t)command_backed_5ghz_builds,
           (uint64_t)command_backed_mixed_band_builds,
           (uint64_t)command_backed_channel_observations,
           channel_9_present != 0 ? 1U : 0U,
           channel_13_present != 0 ? 1U : 0U,
           channel_149_present != 0 ? 1U : 0U,
           channel_153_present != 0 ? 1U : 0U,
           channel_177_present != 0 ? 1U : 0U,
           (channel_9_present != 0 && channel_13_present != 0 &&
            channel_153_present != 0) ? 1U : 0U,
           (uint64_t)trace_errors);
}
