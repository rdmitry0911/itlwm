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
#include <limits.h>
#include <poll.h>
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
    kInputDeadlineMilliseconds = 10000u,
    kOutcomePollMilliseconds = 100u,
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

static const char *
query_outcome(io_connect_t connection)
{
    struct AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyV1 reply;
    size_t reply_size = sizeof(reply);
    kern_return_t result;
    const char *outcome = "query-failed";

    secure_bzero(&reply, sizeof(reply));
    result = IOConnectCallMethod(
        connection, kAirportItlwmIwnLabDirectSaeStimulusQueryOutcomeSelector,
        NULL, 0, NULL, 0, NULL, NULL, &reply, &reply_size);
    if (result == kIOReturnSuccess && reply_size == sizeof(reply) &&
        AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyIsWellFormed(&reply)) {
        switch (reply.outcome) {
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomePending:
            outcome = "pending";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeStarted:
            outcome = "started";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedPrecondition:
            outcome = "rejected-precondition";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedRequestBegin:
            outcome = "rejected-request-begin";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedAssociationOwner:
            outcome = "rejected-association-owner";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedCredentialStage:
            outcome = "rejected-stage";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedAuthType:
            outcome = "rejected-auth-type";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedScanResume:
            outcome = "rejected-scan-resume";
            break;
        case kAirportItlwmIwnLabDirectSaeStimulusOutcomeCancelled:
            outcome = "cancelled";
            break;
        default:
            break;
        }
    }
    secure_bzero(&reply, sizeof(reply));
    return outcome;
}

static int64_t
monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool
wait_for_stdin_until(int64_t deadline)
{
    struct pollfd descriptor;
    int64_t now;
    int remaining;
    int result;

    now = monotonic_milliseconds();
    if (now < 0 || now >= deadline)
        return false;
    remaining = deadline - now > INT_MAX ? INT_MAX : (int)(deadline - now);
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN | POLLHUP;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, remaining);
    } while (result < 0 && errno == EINTR);
    secure_bzero(&descriptor, sizeof(descriptor));
    return result > 0;
}

/* A request is accepted only if stdin contains precisely one ABI record. */
static bool
read_exact_request(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 *out)
{
    struct stat input_status;
    uint8_t extra = 0;
    size_t offset = 0;
    int64_t started;
    int64_t deadline;

    if (out == NULL)
        return false;
    secure_bzero(out, sizeof(*out));
    /* The descriptor itself must be a closing pipe. This does not establish
     * the provenance of its upstream writer; the runtime owner must bind one
     * in-memory producer and must not route a fixture file through it. */
    if (isatty(STDIN_FILENO) != 0 || fstat(STDIN_FILENO, &input_status) != 0 ||
        !S_ISFIFO(input_status.st_mode))
        return false;
    started = monotonic_milliseconds();
    if (started < 0 || started > INT64_MAX - kInputDeadlineMilliseconds)
        return false;
    deadline = started + kInputDeadlineMilliseconds;
    while (offset < sizeof(*out)) {
        if (!wait_for_stdin_until(deadline)) {
            AirportItlwmIwnLabDirectSaeStimulusRequestScrub(out);
            return false;
        }
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
        if (!wait_for_stdin_until(deadline)) {
            AirportItlwmIwnLabDirectSaeStimulusRequestScrub(out);
            secure_bzero(&extra, sizeof(extra));
            return false;
        }
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

static const char *
bounded_hold_with_outcome(io_connect_t connection, uint32_t milliseconds)
{
    struct timespec delay;
    const char *outcome = query_outcome(connection);
    uint32_t remaining = milliseconds;

    while (remaining != 0) {
        uint32_t slice = remaining < kOutcomePollMilliseconds
            ? remaining : kOutcomePollMilliseconds;
        delay.tv_sec = slice / 1000u;
        delay.tv_nsec = (long)(slice % 1000u) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
            ;
        remaining -= slice;
        outcome = query_outcome(connection);
    }
    secure_bzero(&delay, sizeof(delay));
    return outcome;
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
        if (strcmp(readiness, "unsupported") == 0)
            printf("lab-client=unsupported\n");
        else if (strcmp(readiness, "query-failed") == 0)
            printf("lab-client=query-failed\n");
        else
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
    /* Queue acknowledgement is deliberately distinct from any SAE outcome.
     * Flush it before holding the UserClient open so the trace owner can
     * synchronize on dispatch admission without closing this connection. */
    printf("lab-client=queued\n");
    fflush(stdout);
    printf("lab-client-outcome=%s\n",
           bounded_hold_with_outcome(connection, hold_milliseconds));
    exit_code = 0;

out:
    AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request);
    if (connection != MACH_PORT_NULL)
        IOServiceClose(connection);
    return exit_code;
}
