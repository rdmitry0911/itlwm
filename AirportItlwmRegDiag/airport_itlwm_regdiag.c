#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <ClientKit/AirportItlwmRegDiag.h>

static const char *
kind_name(uint32_t kind)
{
    switch (kind) {
        case kAirportItlwmRegDiagTracePublicAssoc: return "public-assoc";
        case kAirportItlwmRegDiagTraceHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmRegDiagTraceLinkState: return "link-state";
        case kAirportItlwmRegDiagTraceTx: return "tx";
        case kAirportItlwmRegDiagTraceRx: return "rx";
        case kAirportItlwmRegDiagTraceBlock: return "block";
        case kAirportItlwmRegDiagTraceControl: return "control";
        default: return "unknown";
    }
}

static const char *
path_name(uint32_t path)
{
    switch (path) {
        case kAirportItlwmRegDiagPathPublicAssoc: return "public-assoc";
        case kAirportItlwmRegDiagPathHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmRegDiagPathTx: return "tx";
        case kAirportItlwmRegDiagPathRx: return "rx";
        case kAirportItlwmRegDiagPathLink: return "link";
        default: return "unknown";
    }
}

static uint32_t
next_sequence(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec ^ tv.tv_usec);
}

static CFStringRef
cfstr(const char *s)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

static io_service_t
find_service(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("AirportItlwm");
    if (matching == NULL)
        return IO_OBJECT_NULL;

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS)
        return IO_OBJECT_NULL;

    io_service_t service = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    return service;
}

static CFTypeRef
copy_property(io_service_t service, const char *name)
{
    CFStringRef key = cfstr(name);
    if (key == NULL)
        return NULL;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    CFRelease(key);
    return value;
}

static int
set_control(io_service_t service, int argc, char **argv)
{
    char control[512];
    uint32_t seq = next_sequence();
    int written = snprintf(control, sizeof(control), "seq=%u", seq);
    if (written < 0 || (size_t)written >= sizeof(control))
        return 2;

    for (int i = 0; i < argc; i += 2) {
        if (i + 1 >= argc) {
            fprintf(stderr, "set expects key value pairs\n");
            return 2;
        }
        written += snprintf(control + written, sizeof(control) - (size_t)written,
                            ";%s=%s", argv[i], argv[i + 1]);
        if (written < 0 || (size_t)written >= sizeof(control)) {
            fprintf(stderr, "control string too long\n");
            return 2;
        }
    }

    CFStringRef key = cfstr(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY);
    CFStringRef value = cfstr(control);
    if (key == NULL || value == NULL) {
        if (key != NULL)
            CFRelease(key);
        if (value != NULL)
            CFRelease(value);
        return 2;
    }

    kern_return_t kr = IORegistryEntrySetCFProperty(service, key, value);
    CFRelease(value);
    CFRelease(key);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IORegistryEntrySetCFProperty failed: 0x%x\n", kr);
        return 1;
    }

    printf("%s\n", control);
    return 0;
}

static void
print_bytes_ascii(const uint8_t *bytes, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = bytes[i];
        putchar((c >= 32 && c <= 126) ? c : '.');
    }
}

static void
print_mac(const uint8_t *mac)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int
get_control(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_CONTROL_ACK_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no control ack property\n");
        return 1;
    }

    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char buf[256];
        if (CFStringGetCString((CFStringRef)value, buf, sizeof(buf), kCFStringEncodingUTF8))
            printf("%s\n", buf);
    } else {
        fprintf(stderr, "control ack has unexpected type\n");
    }
    CFRelease(value);
    return 0;
}

static int
get_snapshot(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_SNAPSHOT_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no snapshot property; enable diagnostics first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) < (CFIndex)sizeof(AirportItlwmRegDiagSnapshot)) {
        fprintf(stderr, "snapshot has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }

    const AirportItlwmRegDiagSnapshot *s =
        (const AirportItlwmRegDiagSnapshot *)CFDataGetBytePtr((CFDataRef)value);
    printf("version=%u size=%u seq=%u control_seq=%u\n",
           s->version, s->size, s->sequence, s->lastControlSequence);
    printf("mode=0x%x block=0x%x rt=0x%x/0x%x/0x%x\n",
           s->modeFlags, s->blockMask, s->rtMask, s->rtMask2, s->rtMask3);
    printf("state ic=%d ic_flags=0x%x if_flags=0x%x power=%u pm=%u link=0x%x speed=%" PRIu64 "\n",
           s->icState, s->icFlags, s->ifFlags, s->powerState, s->pmPowerState,
           s->currentStatus, s->currentSpeed);
    printf("objects hal=%u netif=%u bsd=%u bss=%u nodes=%u bsd_name=%s\n",
           s->hasHalService, s->hasNetIf, s->hasBSDInterface, s->hasBss,
           s->nodeCount, s->bsdName);
    printf("desired_ssid_len=%u desired_ssid=", s->desiredEssLen);
    print_bytes_ascii(s->desiredSsid, s->desiredEssLen);
    printf("\ncurrent_ssid_len=%u current_ssid=", s->currentSsidLen);
    print_bytes_ascii(s->currentSsid, s->currentSsidLen);
    printf(" bssid=");
    print_mac(s->currentBssid);
    printf("\nlast_assoc_ssid_len=%u last_assoc_ssid=", s->lastAssocSsidLen);
    print_bytes_ascii(s->lastAssocSsid, s->lastAssocSsidLen);
    printf(" last_assoc_bssid=");
    print_mac(s->lastAssocBssid);
    printf(" auth=0x%x/0x%x rsn_len=%u\n",
           s->lastAssocAuthLower, s->lastAssocAuthUpper, s->lastAssocRsnIeLen);
    printf("counts public_assoc=%u hidden_assoc=%u link=%u tx=%u rx=%u eapol_tx=%u eapol_rx=%u tx_drop=%u rx_drop=%u block=%u\n",
           s->publicAssocCount, s->hiddenAssocCount, s->linkStateCount,
           s->txCount, s->rxCount, s->eapolTxCount, s->eapolRxCount,
           s->txDropCount, s->rxDropCount, s->blockHitCount);
    printf("last_result public_assoc=0x%x hidden_assoc=0x%x link=0x%x tx=0x%x rx=0x%x link_state=%d last_block=0x%x tx_len=%u rx_len=%u\n",
           (uint32_t)s->lastPublicAssocResult,
           (uint32_t)s->lastHiddenAssocResult,
           (uint32_t)s->lastLinkStateResult,
           (uint32_t)s->lastTxResult,
           (uint32_t)s->lastRxResult,
           s->lastLinkState, s->lastBlockMask, s->lastTxLength, s->lastRxLength);
    printf("ptrs fNetIf=0x%" PRIx64 " bsdIf=0x%" PRIx64 " txQ=0x%" PRIx64 " rxQ=0x%" PRIx64 "\n",
           s->fNetIfPtr, s->bsdIfPtr, s->fTxQueuePtr, s->fRxQueuePtr);

    CFRelease(value);
    return 0;
}

static int
get_trace(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_TRACE_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no trace property; enable diagnostics first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) < (CFIndex)sizeof(AirportItlwmRegDiagTraceBuffer)) {
        fprintf(stderr, "trace has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }

    const AirportItlwmRegDiagTraceBuffer *t =
        (const AirportItlwmRegDiagTraceBuffer *)CFDataGetBytePtr((CFDataRef)value);
    printf("version=%u count=%u next=%u dropped=%u\n",
           t->version, t->entryCount, t->nextSequence, t->droppedEntries);
    uint32_t count = t->entryCount;
    if (count > AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES)
        count = AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES;
    uint32_t start = t->nextSequence >= count ? t->nextSequence - count : 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t seq = start + i;
        const AirportItlwmRegDiagTraceEntry *e =
            &t->entries[seq % AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES];
        printf("#%u kind=%s path=%s result=0x%x arg0=%d arg1=0x%" PRIx64 " arg2=0x%" PRIx64 "\n",
               e->sequence, kind_name(e->kind), path_name(e->path),
               (uint32_t)e->result, e->arg0, e->arg1, e->arg2);
    }

    CFRelease(value);
    return 0;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s set enable 1 assoc 1 control 1 data 1 log 0 clear 1\n"
            "  %s set block 0x0 intervention 0\n"
            "  %s get snapshot|trace|control\n"
            "  %s on|off\n",
            prog, prog, prog, prog);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    io_service_t service = find_service();
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "AirportItlwm service not found\n");
        return 1;
    }

    int rc = 2;
    if (strcmp(argv[1], "set") == 0) {
        rc = set_control(service, argc - 2, argv + 2);
    } else if (strcmp(argv[1], "on") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"1",
            (char *)"assoc", (char *)"1",
            (char *)"control", (char *)"1",
            (char *)"data", (char *)"1",
            (char *)"log", (char *)"0",
            (char *)"intervention", (char *)"0",
            (char *)"clear", (char *)"1",
        };
        rc = set_control(service, 14, args);
    } else if (strcmp(argv[1], "off") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"0",
            (char *)"intervention", (char *)"0",
            (char *)"block", (char *)"0",
        };
        rc = set_control(service, 6, args);
    } else if (strcmp(argv[1], "get") == 0 && argc >= 3) {
        if (strcmp(argv[2], "snapshot") == 0)
            rc = get_snapshot(service);
        else if (strcmp(argv[2], "trace") == 0)
            rc = get_trace(service);
        else if (strcmp(argv[2], "control") == 0)
            rc = get_control(service);
        else
            usage(argv[0]);
    } else {
        usage(argv[0]);
    }

    IOObjectRelease(service);
    return rc;
}
