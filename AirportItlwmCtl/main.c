#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ClientKit/AirportItlwmDiagnostics.h>

static const char *kind_name(uint32_t kind)
{
    switch (kind) {
        case kAirportItlwmDiagTraceCommandGate: return "command-gate";
        case kAirportItlwmDiagTraceHandleCardSpecific: return "handle-card";
        case kAirportItlwmDiagTraceBSDCommand: return "bsd";
        case kAirportItlwmDiagTraceApple80211Ioctl: return "apple80211";
        case kAirportItlwmDiagTracePublicAssoc: return "public-assoc";
        case kAirportItlwmDiagTraceHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmDiagTraceLinkState: return "link";
        case kAirportItlwmDiagTraceTx: return "tx";
        case kAirportItlwmDiagTraceRx: return "rx";
        case kAirportItlwmDiagTraceBlock: return "block";
        default: return "unknown";
    }
}

static const char *path_name(uint32_t path)
{
    switch (path) {
        case kAirportItlwmDiagPathPublicAssoc: return "public-assoc";
        case kAirportItlwmDiagPathHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmDiagPathHandleCardSpecific: return "handle-card";
        case kAirportItlwmDiagPathBSD: return "bsd";
        case kAirportItlwmDiagPathApple80211Ioctl: return "apple80211";
        case kAirportItlwmDiagPathTx: return "tx";
        case kAirportItlwmDiagPathRx: return "rx";
        default: return "unknown";
    }
}

static void copy_ssid(char *dst, size_t dst_len, const uint8_t *src, uint32_t src_len)
{
    size_t n = src_len < AIRPORT_ITLWM_DIAG_MAX_SSID_LEN ? src_len : AIRPORT_ITLWM_DIAG_MAX_SSID_LEN;
    if (n >= dst_len)
        n = dst_len - 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = src[i];
        dst[i] = (c >= 32 && c < 127) ? (char)c : '.';
    }
    dst[n] = '\0';
}

static void print_bssid(const uint8_t bssid[6])
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static int open_diag(io_connect_t *conn)
{
    io_iterator_t iter = IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault,
        IOServiceMatching("AirportItlwmDiagnosticsService"), &iter);
    if (kr != KERN_SUCCESS)
        return kr;
    service = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (service == IO_OBJECT_NULL)
        return KERN_NOT_FOUND;
    kr = IOServiceOpen(service, mach_task_self(), 0, conn);
    IOObjectRelease(service);
    return kr;
}

static kern_return_t call_struct(io_connect_t conn, uint32_t selector,
                                 const void *in, size_t in_size,
                                 void *out, size_t *out_size)
{
    return IOConnectCallStructMethod(conn, selector, in, in_size, out, out_size);
}

static int get_config(io_connect_t conn, AirportItlwmDiagConfig *config)
{
    size_t out_size = sizeof(*config);
    kern_return_t kr = call_struct(conn, kAirportItlwmDiagGetConfig,
                                   NULL, 0, config, &out_size);
    if (kr != KERN_SUCCESS)
        fprintf(stderr, "get config failed: 0x%x\n", kr);
    return kr == KERN_SUCCESS ? 0 : 1;
}

static int set_config(io_connect_t conn, const AirportItlwmDiagConfig *config)
{
    kern_return_t kr = call_struct(conn, kAirportItlwmDiagSetConfig,
                                   config, sizeof(*config), NULL, 0);
    if (kr != KERN_SUCCESS)
        fprintf(stderr, "set config failed: 0x%x\n", kr);
    return kr == KERN_SUCCESS ? 0 : 1;
}

static int cmd_get_config(io_connect_t conn)
{
    AirportItlwmDiagConfig config;
    if (get_config(conn, &config) != 0)
        return 1;
    printf("version=%u size=%u mode=0x%08x trace=0x%08x block=0x%08x\n",
           config.version, config.size, config.modeFlags,
           config.traceMask, config.blockMask);
    return 0;
}

static int cmd_get_snapshot(io_connect_t conn)
{
    AirportItlwmDiagSnapshot s;
    size_t out_size = sizeof(s);
    kern_return_t kr = call_struct(conn, kAirportItlwmDiagGetSnapshot,
                                   NULL, 0, &s, &out_size);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "get snapshot failed: 0x%x\n", kr);
        return 1;
    }

    char desired[33], current[33], last[33];
    copy_ssid(desired, sizeof(desired), s.desiredSsid, s.desiredEssLen);
    copy_ssid(current, sizeof(current), s.currentSsid, s.currentSsidLen);
    copy_ssid(last, sizeof(last), s.lastAssocSsid, s.lastAssocSsidLen);

    printf("state ic=%d flags=0x%x if=0x%x power=%u pm=%u link=0x%x speed=%llu\n",
           s.icState, s.icFlags, s.ifFlags, s.powerState, s.pmPowerState,
           s.currentStatus, s.currentSpeed);
    printf("objects hal=%u netif=%u bsd=%u bss=%u nodes=%u bsdName=%s\n",
           s.hasHalService, s.hasNetIf, s.hasBSDInterface, s.hasBss,
           s.nodeCount, s.bsdName);
    printf("ssid desired=\"%s\" current=\"%s\" last-assoc=\"%s\" last-bssid=",
           desired, current, last);
    print_bssid(s.lastAssocBssid);
    printf("\n");
    printf("assoc public=%u last=0x%x hidden=%u last=0x%x auth=0x%x/0x%x rsnLen=%u\n",
           s.publicAssocCount, (uint32_t)s.lastPublicAssocResult,
           s.hiddenAssocCount, (uint32_t)s.lastHiddenAssocResult,
           s.lastAssocAuthLower, s.lastAssocAuthUpper, s.lastAssocRsnIeLen);
    printf("control gate=%u handle=%u bsd=%u apple80211=%u linkCount=%u lastLink=%d linkRet=0x%x\n",
           s.commandGateCount, s.handleCardSpecificCount, s.bsdCommandCount,
           s.apple80211IoctlCount, s.linkStateCount, s.lastLinkState,
           (uint32_t)s.lastLinkStateResult);
    printf("data tx=%u eapolTx=%u txDrop=%u lastTxLen=%u lastTx=0x%x rx=%u eapolRx=%u rxDrop=%u lastRxLen=%u lastRx=0x%x blocks=%u mask=0x%x\n",
           s.txCount, s.eapolTxCount, s.txDropCount, s.lastTxLength,
           (uint32_t)s.lastTxResult, s.rxCount, s.eapolRxCount,
           s.rxDropCount, s.lastRxLength, (uint32_t)s.lastRxResult,
           s.blockHitCount, s.lastBlockMask);
    printf("rt rt=0x%x rt2=0x%x rt3=0x%x ptrs netif=0x%llx bsd=0x%llx txq=0x%llx rxq=0x%llx\n",
           s.rtMask, s.rtMask2, s.rtMask3, s.fNetIfPtr, s.bsdIfPtr,
           s.fTxQueuePtr, s.fRxQueuePtr);
    return 0;
}

static int cmd_get_scan_cache(io_connect_t conn)
{
    AirportItlwmDiagScanCache cache;
    size_t out_size = sizeof(cache);
    kern_return_t kr = call_struct(conn, kAirportItlwmDiagGetScanCache,
                                   NULL, 0, &cache, &out_size);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "get scan-cache failed: 0x%x\n", kr);
        return 1;
    }
    printf("nodes=%u entries=%u\n", cache.totalNodeCount, cache.entryCount);
    for (uint32_t i = 0; i < cache.entryCount && i < AIRPORT_ITLWM_DIAG_MAX_SCAN_ENTRIES; i++) {
        char ssid[33];
        copy_ssid(ssid, sizeof(ssid), (const uint8_t *)cache.entries[i].ssid,
                  cache.entries[i].ssidLen);
        printf("%02u ch=%u rssi=%d bssid=", i, cache.entries[i].channel,
               cache.entries[i].rssi);
        print_bssid(cache.entries[i].bssid);
        printf(" ssid=\"%s\" rsn protos=0x%x akms=0x%x ciphers=0x%x group=0x%x mgmt=0x%x\n",
               ssid, cache.entries[i].rsnProtos, cache.entries[i].rsnAkms,
               cache.entries[i].rsnCiphers, cache.entries[i].groupCipher,
               cache.entries[i].groupMgmtCipher);
    }
    return 0;
}

static int cmd_get_trace(io_connect_t conn)
{
    AirportItlwmDiagTraceBuffer trace;
    size_t out_size = sizeof(trace);
    kern_return_t kr = call_struct(conn, kAirportItlwmDiagGetTrace,
                                   NULL, 0, &trace, &out_size);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "get trace failed: 0x%x\n", kr);
        return 1;
    }
    printf("entries=%u next=%u dropped=%u\n",
           trace.entryCount, trace.nextSequence, trace.droppedEntries);
    uint32_t first = trace.nextSequence > trace.entryCount ?
        trace.nextSequence - trace.entryCount : 1;
    for (uint32_t seq = first; seq < trace.nextSequence; seq++) {
        for (uint32_t i = 0; i < AIRPORT_ITLWM_DIAG_MAX_TRACE_ENTRIES; i++) {
            AirportItlwmDiagTraceEntry *e = &trace.entries[i];
            if (e->version != AIRPORT_ITLWM_DIAG_ABI_VERSION || e->sequence != seq)
                continue;
            printf("#%u %-14s %-13s cmd=%d req=%d ret=0x%x arg0=%d arg1=0x%llx arg2=0x%llx\n",
                   e->sequence, kind_name(e->kind), path_name(e->path),
                   e->command, e->requestType, (uint32_t)e->result,
                   e->arg0, e->arg1, e->arg2);
            break;
        }
    }
    return 0;
}

static uint32_t block_bit(const char *name)
{
    if (strcmp(name, "public-assoc") == 0) return kAirportItlwmDiagBlockPublicAssoc;
    if (strcmp(name, "hidden-assoc") == 0) return kAirportItlwmDiagBlockHiddenAssoc;
    if (strcmp(name, "tx") == 0) return kAirportItlwmDiagBlockTx;
    if (strcmp(name, "rx") == 0) return kAirportItlwmDiagBlockRx;
    if (strcmp(name, "eapol-tx") == 0) return kAirportItlwmDiagBlockEapolTx;
    if (strcmp(name, "eapol-rx") == 0) return kAirportItlwmDiagBlockEapolRx;
    return 0;
}

static int parse_bool(const char *value, int *enabled)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0 ||
        strcmp(value, "true") == 0) {
        *enabled = 1;
        return 0;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0) {
        *enabled = 0;
        return 0;
    }
    return 1;
}

static int cmd_set(io_connect_t conn, int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: AirportItlwmCtl set <mode|trace|trace-mask|block|block-mask|intervention|clear> ...\n");
        return 1;
    }
    if (strcmp(argv[2], "clear") == 0) {
        kern_return_t kr = call_struct(conn, kAirportItlwmDiagClear, NULL, 0, NULL, 0);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "clear failed: 0x%x\n", kr);
            return 1;
        }
        return 0;
    }

    AirportItlwmDiagConfig config;
    if (get_config(conn, &config) != 0)
        return 1;

    if (strcmp(argv[2], "mode") == 0 && argc >= 4) {
        if (strcmp(argv[3], "off") == 0)
            config.modeFlags = 0;
        else if (strcmp(argv[3], "passive") == 0)
            config.modeFlags = kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeTrace;
        else if (strcmp(argv[3], "assoc") == 0)
            config.modeFlags = kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeTrace |
                               kAirportItlwmDiagModeAssoc;
        else if (strcmp(argv[3], "data") == 0)
            config.modeFlags = kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeTrace |
                               kAirportItlwmDiagModeData;
        else if (strcmp(argv[3], "all") == 0)
            config.modeFlags = kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeTrace |
                               kAirportItlwmDiagModeAssoc | kAirportItlwmDiagModeData;
        else {
            fprintf(stderr, "unknown mode: %s\n", argv[3]);
            return 1;
        }
        return set_config(conn, &config);
    }

    if (strcmp(argv[2], "trace") == 0 && argc >= 4) {
        int enabled = 0;
        if (parse_bool(argv[3], &enabled) != 0)
            return 1;
        if (enabled)
            config.modeFlags |= kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeTrace;
        else
            config.modeFlags &= ~kAirportItlwmDiagModeTrace;
        return set_config(conn, &config);
    }

    if (strcmp(argv[2], "trace-mask") == 0 && argc >= 4) {
        config.traceMask = (uint32_t)strtoul(argv[3], NULL, 0);
        return set_config(conn, &config);
    }

    if (strcmp(argv[2], "intervention") == 0 && argc >= 4) {
        int enabled = 0;
        if (parse_bool(argv[3], &enabled) != 0)
            return 1;
        if (enabled)
            config.modeFlags |= kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeIntervention;
        else {
            config.modeFlags &= ~kAirportItlwmDiagModeIntervention;
            config.blockMask = 0;
        }
        return set_config(conn, &config);
    }

    if (strcmp(argv[2], "block") == 0 && argc >= 5) {
        uint32_t bit = block_bit(argv[3]);
        int enabled = 0;
        if (bit == 0 || parse_bool(argv[4], &enabled) != 0) {
            fprintf(stderr, "usage: AirportItlwmCtl set block <public-assoc|hidden-assoc|tx|rx|eapol-tx|eapol-rx> <on|off>\n");
            return 1;
        }
        if (enabled) {
            config.blockMask |= bit;
            config.modeFlags |= kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeIntervention;
        } else {
            config.blockMask &= ~bit;
            if (config.blockMask == 0)
                config.modeFlags &= ~kAirportItlwmDiagModeIntervention;
        }
        return set_config(conn, &config);
    }

    if (strcmp(argv[2], "block-mask") == 0 && argc >= 4) {
        config.blockMask = (uint32_t)strtoul(argv[3], NULL, 0);
        if (config.blockMask != 0)
            config.modeFlags |= kAirportItlwmDiagModeEnabled | kAirportItlwmDiagModeIntervention;
        else
            config.modeFlags &= ~kAirportItlwmDiagModeIntervention;
        return set_config(conn, &config);
    }

    fprintf(stderr, "unknown set command\n");
    return 1;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  AirportItlwmCtl get config|snapshot|summary|trace|scan-cache\n"
        "  AirportItlwmCtl set mode off|passive|assoc|data|all\n"
        "  AirportItlwmCtl set trace on|off\n"
        "  AirportItlwmCtl set trace-mask <mask>\n"
        "  AirportItlwmCtl set intervention on|off\n"
        "  AirportItlwmCtl set block <public-assoc|hidden-assoc|tx|rx|eapol-tx|eapol-rx> on|off\n"
        "  AirportItlwmCtl set block-mask <mask>\n"
        "  AirportItlwmCtl set clear\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage();
        return 1;
    }

    io_connect_t conn = IO_OBJECT_NULL;
    kern_return_t kr = open_diag(&conn);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "AirportItlwmDiagnosticsService not open: 0x%x\n", kr);
        return 1;
    }

    int rc = 1;
    if (strcmp(argv[1], "get") == 0) {
        if (strcmp(argv[2], "config") == 0)
            rc = cmd_get_config(conn);
        else if (strcmp(argv[2], "snapshot") == 0 || strcmp(argv[2], "summary") == 0)
            rc = cmd_get_snapshot(conn);
        else if (strcmp(argv[2], "trace") == 0)
            rc = cmd_get_trace(conn);
        else if (strcmp(argv[2], "scan-cache") == 0)
            rc = cmd_get_scan_cache(conn);
        else
            usage();
    } else if (strcmp(argv[1], "set") == 0) {
        rc = cmd_set(conn, argc, argv);
    } else {
        usage();
    }

    IOServiceClose(conn);
    return rc;
}
