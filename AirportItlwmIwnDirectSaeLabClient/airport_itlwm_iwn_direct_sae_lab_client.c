/*
 * Minimal laboratory client for the separately compiled direct-IWN-SAE
 * UserClient. It intentionally has no Wi-Fi framework, credential-store,
 * network configuration, or IORegistry diagnostic dependency. Its only two modes
 * are an identity-free readiness query and one fixed-size binary request
 * received on standard input after readiness has been confirmed.
 *
 * The request bytes are never interpreted for display, copied into argv or
 * environment, persisted, or returned to the caller. A successful Submit
 * means only that the IWN lab mailbox accepted the request; the caller must
 * use the separately sealed driver trace to determine an on-air outcome.
 */
#include <IOKit/IOKitLib.h>

#include <ClientKit/AirportItlwmIwnLabDirectSaeStimulusV1.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    kDefaultHoldMilliseconds = 45000u,
    kMaximumHoldMilliseconds = 60000u,
};

enum LabClientMode {
    kLabClientModeInvalid = 0,
    kLabClientModeQueryReady,
    kLabClientModeSubmitStdin,
};

static void
secure_bzero(void *bytes, size_t length)
{
    volatile uint8_t *volatile cursor = bytes;

    while (length-- != 0)
        *cursor++ = 0;
}

static bool
parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0' || out == NULL)
        return false;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static void
usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --query-ready | --submit-stdin "
            "[--hold-milliseconds 0..60000]\n",
            program);
}

/* Return an opened direct-SAE client without exposing service identity. */
static kern_return_t
open_lab_client(io_connect_t *out_connection)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    kern_return_t result;

    if (out_connection == NULL)
        return kIOReturnBadArgument;
    *out_connection = MACH_PORT_NULL;
    result = IOServiceGetMatchingServices(kIOMainPortDefault,
                                          IOServiceMatching("AirportItlwm"),
                                          &iterator);
    if (result != kIOReturnSuccess || iterator == IO_OBJECT_NULL)
        return result != kIOReturnSuccess ? result : kIOReturnNotFound;

    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        io_connect_t connection = MACH_PORT_NULL;
        result = IOServiceOpen(
            service, mach_task_self(),
            kAirportItlwmIwnLabDirectSaeStimulusUserClientType,
            &connection);
        IOObjectRelease(service);
        if (result == kIOReturnSuccess) {
            IOObjectRelease(iterator);
            *out_connection = connection;
            return kIOReturnSuccess;
        }
    }
    IOObjectRelease(iterator);
    return kIOReturnNotFound;
}

static const char *
query_readiness(io_connect_t connection)
{
    struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1 reply;
    size_t reply_size = sizeof(reply);
    kern_return_t result;
    const char *outcome = "query-failed";

    secure_bzero(&reply, sizeof(reply));
    result = IOConnectCallMethod(
        connection, kAirportItlwmIwnLabDirectSaeStimulusQueryReadySelector,
        NULL, 0, NULL, 0, NULL, NULL, &reply, &reply_size);
    if (result == kIOReturnSuccess && reply_size == sizeof(reply) &&
        AirportItlwmIwnLabDirectSaeStimulusReadyReplyIsWellFormed(&reply)) {
        switch (reply.readiness) {
        case kAirportItlwmIwnLabDirectSaeStimulusReady:
            outcome = "ready";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusNotReady:
            outcome = "not-ready";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusUnsupported:
        default:
            outcome = "unsupported";
            break;
        }
    }
    secure_bzero(&reply, sizeof(reply));
    return outcome;
}

/* A request is accepted only if stdin contains precisely one ABI record. */
static bool
read_exact_request(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 *out)
{
    struct stat input_status;
    uint8_t extra = 0;
    size_t offset = 0;

    if (out == NULL)
        return false;
    secure_bzero(out, sizeof(*out));
    /* A secret-bearing fixture file or interactive terminal is not an
     * admissible producer. The caller supplies a single closing pipe. */
    if (isatty(STDIN_FILENO) != 0 || fstat(STDIN_FILENO, &input_status) != 0 ||
        S_ISREG(input_status.st_mode))
        return false;
    while (offset < sizeof(*out)) {
        ssize_t count = read(STDIN_FILENO,
                             ((uint8_t *)out) + offset,
                             sizeof(*out) - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            AirportItlwmIwnLabDirectSaeStimulusRequestScrub(out);
            return false;
        }
        if (count == 0) {
            AirportItlwmIwnLabDirectSaeStimulusRequestScrub(out);
            return false;
        }
        offset += (size_t)count;
    }
    for (;;) {
        ssize_t count = read(STDIN_FILENO, &extra, sizeof(extra));
        if (count < 0 && errno == EINTR)
            continue;
        if (count != 0) {
            AirportItlwmIwnLabDirectSaeStimulusRequestScrub(out);
            secure_bzero(&extra, sizeof(extra));
            return false;
        }
        break;
    }
    secure_bzero(&extra, sizeof(extra));
    return true;
}

static void
bounded_hold(uint32_t milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000u;
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
        ;
    secure_bzero(&delay, sizeof(delay));
}

int
main(int argc, char **argv)
{
    enum LabClientMode mode = kLabClientModeInvalid;
    io_connect_t connection = MACH_PORT_NULL;
    uint32_t hold_milliseconds = kDefaultHoldMilliseconds;
    struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 request;
    const char *readiness;
    kern_return_t result;
    int exit_code = 1;

    secure_bzero(&request, sizeof(request));
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--query-ready") == 0 &&
            mode == kLabClientModeInvalid) {
            mode = kLabClientModeQueryReady;
        } else if (strcmp(argv[index], "--submit-stdin") == 0 &&
                   mode == kLabClientModeInvalid) {
            mode = kLabClientModeSubmitStdin;
        } else if (strcmp(argv[index], "--hold-milliseconds") == 0 &&
                   index + 1 < argc &&
                   parse_u32(argv[index + 1], &hold_milliseconds)) {
            ++index;
        } else {
            usage(argv[0]);
            goto out;
        }
    }
    if (mode == kLabClientModeInvalid ||
        hold_milliseconds > kMaximumHoldMilliseconds) {
        usage(argv[0]);
        goto out;
    }

    result = open_lab_client(&connection);
    if (result != kIOReturnSuccess) {
        printf("lab-client=open-unavailable\n");
        goto out;
    }
    readiness = query_readiness(connection);
    if (mode == kLabClientModeQueryReady) {
        printf("lab-client=%s\n", readiness);
        exit_code = strcmp(readiness, "ready") == 0 ? 0 : 1;
        goto out;
    }
    if (strcmp(readiness, "ready") != 0) {
        printf("lab-client=not-ready\n");
        goto out;
    }
    if (!read_exact_request(&request) ||
        !AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request)) {
        printf("lab-client=rejected\n");
        goto out;
    }
    result = IOConnectCallStructMethod(
        connection, kAirportItlwmIwnLabDirectSaeStimulusSubmitSelector,
        &request, sizeof(request), NULL, NULL);
    AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request);
    if (result != kIOReturnSuccess) {
        printf("lab-client=rejected\n");
        goto out;
    }
    bounded_hold(hold_milliseconds);
    printf("lab-client=accepted\n");
    exit_code = 0;

out:
    AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request);
    if (connection != MACH_PORT_NULL)
        IOServiceClose(connection);
    return exit_code;
}
