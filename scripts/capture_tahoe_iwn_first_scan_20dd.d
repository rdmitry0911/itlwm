#pragma D option quiet
#pragma D option bufsize=2m

/* Read-only. rxinfo channel offset 12 is verified against the loaded
 * 20dd3d8a object DWARF. No node, mbuf layout, keys or writable probes. */
dtrace:::BEGIN
{
    start_ns = timestamp;
    self->building = 0;
    self->rx = 0;
    printf("%Y FIRST_SCAN_BEGIN\n", walltimestamp);
}
fbt:com.zxystd.AirportItlwm:*iwn_scan_submit*:entry
{
    self->building = 1;
    self->doorbell = 0;
    printf("%lld SCAN_BUILD band=%u background=%d\n", walltimestamp,
        (uint16_t)arg2, (int)arg3);
}
fbt:com.zxystd.AirportItlwm:*ieee80211_chan2ieee*:return
/self->building && !self->doorbell/
{
    printf("%lld BUILD_CHANNEL=%u\n", walltimestamp, (uint16_t)arg1);
}
fbt:com.zxystd.AirportItlwm:*iwn_scan_submit*:return
/self->building/
{
    printf("%lld SCAN_BUILD_RESULT=%d\n", walltimestamp, (int)arg1);
    self->building = 0;
}
fbt:com.zxystd.AirportItlwm:*iwn_scan_lease_finish_doorbell*:entry
{
    self->doorbell = 1;
    printf("%lld POST_DOORBELL_CALLBACK\n", walltimestamp);
}
fbt:com.zxystd.AirportItlwm:*iwn_scan_lease_claim_terminal*:return
{
    printf("%lld FIRMWARE_TERMINAL_CLAIM=%u\n", walltimestamp, (uint8_t)arg1);
}
fbt:com.zxystd.AirportItlwm:*ieee80211_recv_probe_resp*:entry
/arg3 != 0/
{
    self->rx = 1;
    self->channel = *(uint8_t *)(arg3 + 12);
    self->candidate = 0;
    self->open_advert = 0;
    @received[self->channel] = count();
}
/* Exact public fixture name only; no other SSID or frame bytes are emitted. */
fbt:com.zxystd.AirportItlwm:*ieee80211_refresh_scan_ssid*:entry
/self->rx && arg2 != 0 && *(uint8_t *)(arg2 + 1) == 14 &&
 *(uint8_t *)(arg2 + 2) == 65 && *(uint8_t *)(arg2 + 3) == 73 &&
 *(uint8_t *)(arg2 + 4) == 65 && *(uint8_t *)(arg2 + 5) == 77 &&
 *(uint8_t *)(arg2 + 6) == 45 && *(uint8_t *)(arg2 + 7) == 85 &&
 *(uint8_t *)(arg2 + 8) == 73 && *(uint8_t *)(arg2 + 9) == 70 &&
 *(uint8_t *)(arg2 + 10) == 51 && *(uint8_t *)(arg2 + 11) == 45 &&
 *(uint8_t *)(arg2 + 12) == 79 && *(uint8_t *)(arg2 + 13) == 80 &&
 *(uint8_t *)(arg2 + 14) == 69 && *(uint8_t *)(arg2 + 15) == 78/
{
    self->open_advert = 1;
    printf("%lld OPEN_ADVERT_RX channel=%u\n", walltimestamp, (uint32_t)self->channel);
}
fbt:com.zxystd.AirportItlwm:*ieee80211_setup_rates*:entry
/self->rx/
{
    self->candidate = 1;
}
fbt:com.zxystd.AirportItlwm:*ieee80211_recv_probe_resp*:return
/self->rx && self->open_advert/
{
    printf("%lld OPEN_CANDIDATE=%u\n", walltimestamp, (uint32_t)self->candidate);
}
fbt:com.zxystd.AirportItlwm:*ieee80211_recv_probe_resp*:return
/self->rx/
{
    @candidates[self->channel, self->candidate] = count();
    self->rx = 0;
}
profile:::tick-1sec
/timestamp - start_ns >= 60000000000/
{
    exit(0);
}
dtrace:::ERROR
{
    errors++;
    printf("%Y TRACE_ERROR=%d\n", walltimestamp, arg3);
}
dtrace:::END
{
    printf("%Y FIRST_SCAN_END errors=%u\n", walltimestamp, (uint32_t)errors);
    printa("RX channel=%u count=%@u\n", @received);
    printa("RX_RESULT channel=%u accepted=%u count=%@u\n", @candidates);
}
