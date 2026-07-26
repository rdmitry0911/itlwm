/*
 * Bounded laboratory credential broker for the public-CoreWLAN recovery
 * probe.  This Linux-only relay has three canonical decimal file-descriptor
 * arguments:
 *
 *   credential FIFO read end, host credential pipe write end,
 *   private AF_UNIX SOCK_SEQPACKET control socket.
 *
 * It accepts one WPA passphrase record (8..63 printable ASCII bytes, a
 * newline, then EOF) from the first descriptor during fifteen seconds.  It
 * forwards that one record to the host pipe and closes it.  The passphrase is
 * retained only in locked, non-dumpable anonymous memory until a controller
 * supplies an exact, opaque START packet and a guest stdin write end through
 * SCM_RIGHTS.  START writes only the credential to that guest pipe.  Once the
 * controller has observed the recovery client's exact initial-ready proof it
 * sends ARM, which writes the pre-withdrawal arm token.  RELEASE writes the
 * withdrawal token and closes the guest pipe; ABORT only closes it.
 *
 * Control packets deliberately contain only two lower-case SHA-256 strings
 * and fixed protocol words.  This broker does not echo either hash and emits
 * neither standard output nor standard error.  It does not inspect the
 * environment, open paths, execute children, or persist any input.
 */

#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    kCredentialMinimumLength = 8u,
    kCredentialMaximumLength = 63u,
    kCredentialDeadlineMilliseconds = 15000u,
    kPipeWriteDeadlineMilliseconds = 15000u,
    /* The public recovery client can spend about one hundred seconds proving
     * initial readiness: discovery, exact association, and a post-association
     * same-ESS scan.  Keep the controller's START-to-ARM phase above that
     * bounded work while still expiring well before a laboratory lease. */
    kStartedToArmDeadlineMilliseconds = 120000u,
    kArmedToReleaseDeadlineMilliseconds = 90000u,
    kHostFedToStartDeadlineMilliseconds = 60000u,
    kControlPacketCapacity = 160u,
    kDigestTextLength = 64u,
};

enum broker_phase {
    kBrokerPhaseHostFed = 0,
    kBrokerPhaseStarted,
    kBrokerPhaseArmed,
};

struct protected_secret {
    uint8_t *bytes;
    size_t length;
    size_t mapping_length;
};

struct control_packet {
    uint8_t bytes[kControlPacketCapacity];
    size_t length;
    int received_fd;
};

static void
secure_zero(void *bytes, size_t length)
{
    volatile uint8_t *volatile cursor = bytes;

    while (length-- != 0)
        *cursor++ = 0;
}

static void
close_discard(int *fd)
{
    int local;

    if (fd == NULL || *fd < 0)
        return;
    local = *fd;
    *fd = -1;
    (void)close(local);
}

/* Do not retry close after EINTR: Linux may already have released the number. */
static int
close_once(int *fd)
{
    int local;
    int result;

    if (fd == NULL || *fd < 0)
        return 0;
    local = *fd;
    *fd = -1;
    result = close(local);
    return result == 0;
}

static int
parse_decimal_fd(const char *text, int *fd)
{
    unsigned long value = 0;
    size_t index;

    if (text == NULL || fd == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0'))
        return 0;
    for (index = 0; text[index] != '\0'; index++) {
        const unsigned char byte = (unsigned char)text[index];

        if (byte < (unsigned char)'0' || byte > (unsigned char)'9')
            return 0;
        if (value > ((unsigned long)INT_MAX - (byte - (unsigned char)'0')) /
                        10u)
            return 0;
        value = value * 10u + (unsigned long)(byte - (unsigned char)'0');
    }
    /* Keep the relay's descriptors out of its standard-stream namespace. */
    if (value < 3u)
        return 0;
    *fd = (int)value;
    return 1;
}

static int
has_requested_fifo_access(int fd, int requested_access)
{
    struct stat status;
    int flags;

    if (fd < 3 || isatty(fd) != 0 || fstat(fd, &status) != 0 ||
        !S_ISFIFO(status.st_mode) || (flags = fcntl(fd, F_GETFL)) < 0 ||
        (flags & O_ACCMODE) != requested_access)
        return 0;
    return 1;
}

static int
is_private_control_socket(int fd)
{
    struct ucred peer_credentials;
    struct stat status;
    struct sockaddr_storage peer;
    socklen_t peer_length = sizeof(peer);
    socklen_t value_length;
    int socket_type = 0;
    int socket_domain = 0;

    if (fd < 3 || fstat(fd, &status) != 0 || !S_ISSOCK(status.st_mode))
        return 0;
    value_length = sizeof(socket_type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &value_length) != 0 ||
        value_length != sizeof(socket_type) || socket_type != SOCK_SEQPACKET)
        return 0;
    value_length = sizeof(socket_domain);
    if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &socket_domain, &value_length) != 0 ||
        value_length != sizeof(socket_domain) || socket_domain != AF_UNIX)
        return 0;
    if (getpeername(fd, (struct sockaddr *)&peer, &peer_length) != 0 ||
        peer.ss_family != AF_UNIX)
        return 0;
    value_length = sizeof(peer_credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer_credentials,
                   &value_length) != 0 ||
        value_length != sizeof(peer_credentials) || peer_credentials.pid <= 0 ||
        peer_credentials.uid != geteuid()) {
        secure_zero(&peer, sizeof(peer));
        secure_zero(&peer_credentials, sizeof(peer_credentials));
        return 0;
    }
    secure_zero(&peer, sizeof(peer));
    secure_zero(&peer_credentials, sizeof(peer_credentials));
    return 1;
}

static int
monotonic_milliseconds(int64_t *out)
{
    struct timespec now;

    if (out == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        now.tv_sec < 0 || now.tv_sec > INT64_MAX / 1000)
        return 0;
    *out = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 1;
}

static int
deadline_after(uint32_t milliseconds, int64_t *deadline)
{
    int64_t started;

    if (deadline == NULL || !monotonic_milliseconds(&started) ||
        started > INT64_MAX - (int64_t)milliseconds)
        return 0;
    *deadline = started + (int64_t)milliseconds;
    return 1;
}

static int
wait_for_fd_until(int fd, short events, int64_t deadline)
{
    struct pollfd descriptor;
    int64_t now;
    int remaining;
    int result;
    int ready = 0;

    if (!monotonic_milliseconds(&now) || now >= deadline)
        return 0;
    remaining = deadline - now > INT_MAX ? INT_MAX : (int)(deadline - now);
    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, remaining);
    } while (result < 0 && errno == EINTR);
    if (result > 0 && (descriptor.revents & (POLLERR | POLLNVAL)) == 0) {
        if ((events & POLLIN) != 0)
            ready = (descriptor.revents & (POLLIN | POLLHUP)) != 0;
        else
            ready = (descriptor.revents & events) == events;
    }
    secure_zero(&descriptor, sizeof(descriptor));
    return ready;
}

static int
write_all_until(int fd, const uint8_t *bytes, size_t length, int64_t deadline)
{
    size_t offset = 0;

    if ((bytes == NULL && length != 0) || fd < 0)
        return 0;
    while (offset < length) {
        ssize_t count;

        if (!wait_for_fd_until(fd, POLLOUT, deadline))
            return 0;
        do {
            count = write(fd, bytes + offset, length - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0 || (size_t)count > length - offset)
            return 0;
        offset += (size_t)count;
    }
    return 1;
}

static int
secret_buffer_create(struct protected_secret *secret)
{
    long page_size;
    void *mapping;

    if (secret == NULL)
        return 0;
    secure_zero(secret, sizeof(*secret));
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || (unsigned long)page_size < kCredentialMaximumLength)
        return 0;
    mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
        return 0;
    secure_zero(mapping, (size_t)page_size);
    if (mlock(mapping, (size_t)page_size) != 0 ||
        madvise(mapping, (size_t)page_size, MADV_DONTDUMP) != 0) {
        secure_zero(mapping, (size_t)page_size);
        (void)munlock(mapping, (size_t)page_size);
        (void)munmap(mapping, (size_t)page_size);
        return 0;
    }
    secret->bytes = mapping;
    secret->mapping_length = (size_t)page_size;
    return 1;
}

static void
secret_buffer_destroy(struct protected_secret *secret)
{
    if (secret == NULL)
        return;
    if (secret->bytes != NULL && secret->mapping_length != 0) {
        secure_zero(secret->bytes, secret->mapping_length);
        (void)munlock(secret->bytes, secret->mapping_length);
        (void)munmap(secret->bytes, secret->mapping_length);
    }
    secure_zero(secret, sizeof(*secret));
}

static int
read_exactly_one_credential(int fd, struct protected_secret *secret)
{
    int64_t deadline;
    uint8_t byte = 0;
    size_t length = 0;
    int accepted = 0;

    if (secret == NULL || secret->bytes == NULL)
        return 0;
    if (!deadline_after(kCredentialDeadlineMilliseconds, &deadline))
        goto out;
    secure_zero(secret->bytes, kCredentialMaximumLength);
    secret->length = 0;
    for (;;) {
        ssize_t count;

        if (!wait_for_fd_until(fd, POLLIN, deadline))
            goto out;
        do {
            count = read(fd, &byte, sizeof(byte));
        } while (count < 0 && errno == EINTR);
        if (count != 1)
            goto out;
        if (byte == (uint8_t)'\n') {
            if (length < kCredentialMinimumLength)
                goto out;
            break;
        }
        if (byte < 0x20u || byte > 0x7eu || length >= kCredentialMaximumLength)
            goto out;
        secret->bytes[length++] = byte;
    }
    if (!wait_for_fd_until(fd, POLLIN, deadline))
        goto out;
    {
        ssize_t count;

        do {
            count = read(fd, &byte, sizeof(byte));
        } while (count < 0 && errno == EINTR);
        if (count != 0)
            goto out;
    }
    secret->length = length;
    accepted = 1;

out:
    secure_zero(&byte, sizeof(byte));
    if (!accepted) {
        secure_zero(secret->bytes, kCredentialMaximumLength);
        secret->length = 0;
    }
    return accepted;
}

static int
feed_host_pipe_once(int *host_fd, const struct protected_secret *secret)
{
    static const uint8_t newline[] = "\n";
    int64_t deadline;
    int sent = 0;

    if (host_fd == NULL || secret == NULL || secret->bytes == NULL ||
        secret->length < kCredentialMinimumLength ||
        !deadline_after(kCredentialDeadlineMilliseconds, &deadline))
        return 0;
    if (write_all_until(*host_fd, secret->bytes, secret->length, deadline) &&
        write_all_until(*host_fd, newline, sizeof(newline) - 1u, deadline))
        sent = 1;
    return close_once(host_fd) && sent;
}

static int
is_lower_hex(uint8_t byte)
{
    return (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') ||
        (byte >= (uint8_t)'a' && byte <= (uint8_t)'f');
}

static int
is_start_packet(const struct control_packet *packet)
{
    size_t index;
    const size_t first_digest_offset = sizeof("START ") - 1u;
    const size_t second_digest_offset = first_digest_offset + kDigestTextLength + 1u;
    const size_t expected_length = second_digest_offset + kDigestTextLength;

    if (packet == NULL || packet->length != expected_length ||
        memcmp(packet->bytes, "START ", first_digest_offset) != 0 ||
        packet->bytes[first_digest_offset + kDigestTextLength] != (uint8_t)' ')
        return 0;
    for (index = 0; index < kDigestTextLength; index++) {
        if (!is_lower_hex(packet->bytes[first_digest_offset + index]) ||
            !is_lower_hex(packet->bytes[second_digest_offset + index]))
            return 0;
    }
    return 1;
}

static int
is_fixed_packet(const struct control_packet *packet,
                const char *expected, size_t expected_length)
{
    if (packet == NULL || expected == NULL || packet->length != expected_length)
        return 0;
    return memcmp(packet->bytes, expected, expected_length) == 0;
}

static void
close_received_rights(const struct msghdr *message)
{
    struct cmsghdr *header;

    if (message == NULL)
        return;
    for (header = CMSG_FIRSTHDR(message); header != NULL;
         header = CMSG_NXTHDR((struct msghdr *)message, header)) {
        size_t data_length;
        size_t index;
        int *fds;

        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
            header->cmsg_len < CMSG_LEN(0))
            continue;
        data_length = header->cmsg_len - CMSG_LEN(0);
        if (data_length % sizeof(int) != 0)
            continue;
        fds = (int *)CMSG_DATA(header);
        for (index = 0; index < data_length / sizeof(*fds); index++) {
            if (fds[index] >= 0)
                (void)close(fds[index]);
        }
    }
}

static void
control_packet_destroy(struct control_packet *packet)
{
    if (packet == NULL)
        return;
    close_discard(&packet->received_fd);
    secure_zero(packet, sizeof(*packet));
    packet->received_fd = -1;
}

static int
receive_control_packet(int control_fd, int64_t deadline,
                       struct control_packet *packet)
{
    uint8_t ancillary[CMSG_SPACE(sizeof(int) * 2u)];
    struct iovec vector;
    struct msghdr message;
    struct cmsghdr *header;
    int received_fd = -1;
    int accepted = 0;
    ssize_t count;

    if (packet == NULL || !wait_for_fd_until(control_fd, POLLIN, deadline))
        return 0;
    secure_zero(packet, sizeof(*packet));
    packet->received_fd = -1;
    secure_zero(ancillary, sizeof(ancillary));
    secure_zero(&vector, sizeof(vector));
    secure_zero(&message, sizeof(message));
    vector.iov_base = packet->bytes;
    vector.iov_len = sizeof(packet->bytes);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = ancillary;
    message.msg_controllen = sizeof(ancillary);
    do {
        count = recvmsg(control_fd, &message, MSG_CMSG_CLOEXEC);
    } while (count < 0 && errno == EINTR);
    if (count <= 0 || (size_t)count > sizeof(packet->bytes) ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0)
        goto out;
    for (header = CMSG_FIRSTHDR(&message); header != NULL;
         header = CMSG_NXTHDR(&message, header)) {
        size_t data_length;

        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
            header->cmsg_len != CMSG_LEN(sizeof(int)) || received_fd >= 0)
            goto out;
        data_length = header->cmsg_len - CMSG_LEN(0);
        if (data_length != sizeof(int))
            goto out;
        received_fd = *(int *)CMSG_DATA(header);
    }
    packet->length = (size_t)count;
    packet->received_fd = received_fd;
    received_fd = -1;
    accepted = 1;

out:
    if (!accepted) {
        close_received_rights(&message);
        /* Every SCM_RIGHTS descriptor belongs to the message on this path. */
        received_fd = -1;
    }
    close_discard(&received_fd);
    secure_zero(ancillary, sizeof(ancillary));
    secure_zero(&vector, sizeof(vector));
    secure_zero(&message, sizeof(message));
    if (!accepted)
        control_packet_destroy(packet);
    return accepted;
}

static int
is_guest_write_pipe(int fd)
{
    return has_requested_fifo_access(fd, O_WRONLY);
}

static int
write_guest_credential(int guest_fd, const struct protected_secret *secret)
{
    static const uint8_t newline[] = "\n";
    int64_t deadline;

    if (secret == NULL || secret->bytes == NULL || secret->length == 0 ||
        !deadline_after(kPipeWriteDeadlineMilliseconds, &deadline))
        return 0;
    return write_all_until(guest_fd, secret->bytes, secret->length, deadline) &&
        write_all_until(guest_fd, newline, sizeof(newline) - 1u, deadline);
}

static int
write_guest_arm(int guest_fd)
{
    static const uint8_t arm_token[] = "arm-withdraw\n";
    int64_t deadline;

    if (!deadline_after(kPipeWriteDeadlineMilliseconds, &deadline))
        return 0;
    return write_all_until(guest_fd, arm_token, sizeof(arm_token) - 1u,
                           deadline);
}

static int
write_guest_release(int guest_fd)
{
    static const uint8_t withdraw_token[] = "withdraw\n";
    int64_t deadline;

    if (!deadline_after(kPipeWriteDeadlineMilliseconds, &deadline))
        return 0;
    return write_all_until(guest_fd, withdraw_token,
                           sizeof(withdraw_token) - 1u, deadline);
}

static int
send_status(int control_fd, const uint8_t *status, size_t status_length)
{
    int64_t deadline;
    ssize_t count;

    if (status == NULL || status_length == 0 ||
        !deadline_after(kPipeWriteDeadlineMilliseconds, &deadline) ||
        !wait_for_fd_until(control_fd, POLLOUT, deadline))
        return 0;
    do {
        count = send(control_fd, status, status_length, MSG_NOSIGNAL);
    } while (count < 0 && errno == EINTR);
    return count >= 0 && (size_t)count == status_length;
}

static int
harden_process(void)
{
    struct sigaction action;
    struct rlimit core_limit;

    secure_zero(&action, sizeof(action));
    secure_zero(&core_limit, sizeof(core_limit));
    action.sa_handler = SIG_IGN;
    core_limit.rlim_cur = 0;
    core_limit.rlim_max = 0;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGPIPE, &action, NULL) != 0 ||
        prctl(PR_SET_DUMPABLE, 0UL, 0UL, 0UL, 0UL) != 0 ||
        setrlimit(RLIMIT_CORE, &core_limit) != 0) {
        secure_zero(&action, sizeof(action));
        secure_zero(&core_limit, sizeof(core_limit));
        return 0;
    }
    secure_zero(&action, sizeof(action));
    secure_zero(&core_limit, sizeof(core_limit));
    return 1;
}

int
airport_itlwm_lab_credential_broker_main(int argc, char *argv[])
{
    static const uint8_t host_fed_status[] = "HOST_FED";
    static const uint8_t started_status[] = "STARTED";
    static const uint8_t armed_status[] = "ARMED";
    static const uint8_t released_status[] = "RELEASED";
    static const uint8_t aborted_status[] = "ABORTED";
    struct protected_secret secret;
    struct control_packet packet;
    enum broker_phase phase = kBrokerPhaseHostFed;
    int credential_fd = -1;
    int host_fd = -1;
    int control_fd = -1;
    int guest_fd = -1;
    int exit_code = 1;

    secure_zero(&secret, sizeof(secret));
    secure_zero(&packet, sizeof(packet));
    packet.received_fd = -1;
    if (argc != 4 || !parse_decimal_fd(argv[1], &credential_fd) ||
        !parse_decimal_fd(argv[2], &host_fd) ||
        !parse_decimal_fd(argv[3], &control_fd) ||
        credential_fd == host_fd || credential_fd == control_fd ||
        host_fd == control_fd || !harden_process() ||
        !has_requested_fifo_access(credential_fd, O_RDONLY) ||
        !has_requested_fifo_access(host_fd, O_WRONLY) ||
        !is_private_control_socket(control_fd) || !secret_buffer_create(&secret) ||
        !read_exactly_one_credential(credential_fd, &secret) ||
        !close_once(&credential_fd) || !feed_host_pipe_once(&host_fd, &secret))
        goto out;
    if (!send_status(control_fd, host_fed_status,
                     sizeof(host_fed_status) - 1u))
        goto out;
    for (;;) {
        int64_t deadline;
        uint32_t phase_deadline;

        switch (phase) {
        case kBrokerPhaseHostFed:
            phase_deadline = kHostFedToStartDeadlineMilliseconds;
            break;
        case kBrokerPhaseStarted:
            phase_deadline = kStartedToArmDeadlineMilliseconds;
            break;
        case kBrokerPhaseArmed:
            phase_deadline = kArmedToReleaseDeadlineMilliseconds;
            break;
        default:
            goto out;
        }
        control_packet_destroy(&packet);
        if (!deadline_after(phase_deadline, &deadline) ||
            !receive_control_packet(control_fd, deadline, &packet))
            goto out;
        if (is_fixed_packet(&packet, "ABORT", sizeof("ABORT") - 1u) &&
            packet.received_fd < 0) {
            close_discard(&guest_fd);
            if (!send_status(control_fd, aborted_status,
                             sizeof(aborted_status) - 1u))
                goto out;
            exit_code = 0;
            goto out;
        }
        if (phase == kBrokerPhaseHostFed) {
            if (!is_start_packet(&packet) || packet.received_fd < 0 ||
                !is_guest_write_pipe(packet.received_fd))
                goto out;
            guest_fd = packet.received_fd;
            packet.received_fd = -1;
            if (!write_guest_credential(guest_fd, &secret))
                goto out;
            secret_buffer_destroy(&secret);
            if (!send_status(control_fd, started_status,
                             sizeof(started_status) - 1u))
                goto out;
            phase = kBrokerPhaseStarted;
            continue;
        }
        if (phase == kBrokerPhaseStarted) {
            if (!is_fixed_packet(&packet, "ARM", sizeof("ARM") - 1u) ||
                packet.received_fd >= 0 || !write_guest_arm(guest_fd) ||
                !send_status(control_fd, armed_status,
                             sizeof(armed_status) - 1u))
                goto out;
            phase = kBrokerPhaseArmed;
            continue;
        }
        if (phase == kBrokerPhaseArmed) {
            if (!is_fixed_packet(&packet, "RELEASE", sizeof("RELEASE") - 1u) ||
                packet.received_fd >= 0 || !write_guest_release(guest_fd) ||
                !close_once(&guest_fd) ||
                !send_status(control_fd, released_status,
                             sizeof(released_status) - 1u))
                goto out;
            exit_code = 0;
            goto out;
        }
        goto out;
    }

out:
    control_packet_destroy(&packet);
    close_discard(&guest_fd);
    close_discard(&credential_fd);
    close_discard(&host_fd);
    close_discard(&control_fd);
    secret_buffer_destroy(&secret);
    return exit_code;
}

#ifndef AIRPORT_ITLWM_LAB_CREDENTIAL_BROKER_NO_MAIN
int
main(int argc, char *argv[])
{
    return airport_itlwm_lab_credential_broker_main(argc, argv);
}
#endif
