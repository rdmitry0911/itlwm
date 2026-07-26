/* Synthetic, local-only behavioral checks for the bounded credential broker. */

#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int airport_itlwm_lab_credential_broker_main(int argc, char *argv[]);

enum {
    kDigestTextLength = 64u,
    kStartPacketLength = 6u + kDigestTextLength + 1u + kDigestTextLength,
    kTestTimeoutMilliseconds = 2000,
};

static void
close_fd(int *fd)
{
    if (fd != NULL && *fd >= 0) {
        const int local = *fd;

        *fd = -1;
        (void)close(local);
    }
}

static int
elevate_fd(int *fd)
{
    int duplicate;

    if (fd == NULL || *fd < 0)
        return 0;
    if (*fd >= 3)
        return 1;
    duplicate = fcntl(*fd, F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0)
        return 0;
    (void)close(*fd);
    *fd = duplicate;
    return 1;
}

static int
make_pipe(int pair[2])
{
    if (pair == NULL || pipe2(pair, O_CLOEXEC) != 0)
        return 0;
    if (elevate_fd(&pair[0]) && elevate_fd(&pair[1]))
        return 1;
    close_fd(&pair[0]);
    close_fd(&pair[1]);
    return 0;
}

static int
make_control_pair(int pair[2])
{
    if (pair == NULL ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0)
        return 0;
    if (elevate_fd(&pair[0]) && elevate_fd(&pair[1]))
        return 1;
    close_fd(&pair[0]);
    close_fd(&pair[1]);
    return 0;
}

static int
wait_for_fd(int fd, short events)
{
    struct pollfd descriptor;
    int result;

    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, kTestTimeoutMilliseconds);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & (POLLERR | POLLNVAL)) == 0 &&
        (((events & POLLIN) != 0 &&
          (descriptor.revents & (POLLIN | POLLHUP)) != 0) ||
         ((events & POLLIN) == 0 && (descriptor.revents & events) == events));
}

static int
write_all(int fd, const uint8_t *bytes, size_t length)
{
    size_t offset = 0;

    if (bytes == NULL && length != 0)
        return 0;
    while (offset < length) {
        ssize_t count;

        if (!wait_for_fd(fd, POLLOUT))
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
read_exact(int fd, uint8_t *bytes, size_t length)
{
    size_t offset = 0;

    if (bytes == NULL && length != 0)
        return 0;
    while (offset < length) {
        ssize_t count;

        if (!wait_for_fd(fd, POLLIN))
            return 0;
        do {
            count = read(fd, bytes + offset, length - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0 || (size_t)count > length - offset)
            return 0;
        offset += (size_t)count;
    }
    return 1;
}

static int
read_eof(int fd)
{
    uint8_t byte = 0;
    ssize_t count;

    if (!wait_for_fd(fd, POLLIN))
        return 0;
    do {
        count = read(fd, &byte, sizeof(byte));
    } while (count < 0 && errno == EINTR);
    return count == 0;
}

static int
no_data_available(int fd)
{
    struct pollfd descriptor;
    int result;

    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

static int
send_packet(int control_fd, const uint8_t *bytes, size_t length, int passed_fd)
{
    uint8_t ancillary[CMSG_SPACE(sizeof(int))];
    struct iovec vector;
    struct msghdr message;
    struct cmsghdr *header;
    ssize_t count;

    if (bytes == NULL || length == 0)
        return 0;
    memset(ancillary, 0, sizeof(ancillary));
    memset(&vector, 0, sizeof(vector));
    memset(&message, 0, sizeof(message));
    vector.iov_base = (void *)bytes;
    vector.iov_len = length;
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    if (passed_fd >= 0) {
        message.msg_control = ancillary;
        message.msg_controllen = sizeof(ancillary);
        header = CMSG_FIRSTHDR(&message);
        if (header == NULL)
            return 0;
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        *(int *)CMSG_DATA(header) = passed_fd;
    }
    do {
        count = sendmsg(control_fd, &message, MSG_NOSIGNAL);
    } while (count < 0 && errno == EINTR);
    return count >= 0 && (size_t)count == length;
}

static int
receive_status(int control_fd, const char *expected)
{
    uint8_t packet[32];
    const size_t expected_length = strlen(expected);
    ssize_t count;
    int matched;

    if (expected == NULL || expected_length == 0 || expected_length > sizeof(packet) ||
        !wait_for_fd(control_fd, POLLIN))
        return 0;
    do {
        count = recv(control_fd, packet, sizeof(packet), 0);
    } while (count < 0 && errno == EINTR);
    matched = count >= 0 && (size_t)count == expected_length &&
        memcmp(packet, expected, expected_length) == 0;
    memset(packet, 0, sizeof(packet));
    return matched;
}

static int
wait_for_broker(pid_t child, int expect_success)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 200u; attempt++) {
        int status;
        const pid_t result = waitpid(child, &status, WNOHANG);

        if (result == child)
            return WIFEXITED(status) && ((WEXITSTATUS(status) == 0) == expect_success);
        if (result < 0)
            return 0;
        {
            const struct timespec delay = { 0, 10000000L };

            (void)nanosleep(&delay, NULL);
        }
    }
    (void)kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
    return 0;
}

static pid_t
spawn_broker(int credential_read, int credential_write,
             int host_write, int host_read,
             int control_child, int control_parent,
             int extra_close_one, int extra_close_two)
{
    const pid_t child = fork();

    if (child != 0)
        return child;
    if (child < 0)
        return -1;
    (void)close(credential_write);
    (void)close(host_read);
    (void)close(control_parent);
    if (extra_close_one >= 0)
        (void)close(extra_close_one);
    if (extra_close_two >= 0)
        (void)close(extra_close_two);
    {
        char credential_text[32];
        char host_text[32];
        char control_text[32];
        char *arguments[] = {
            (char *)"credential-broker-test",
            credential_text,
            host_text,
            control_text,
            NULL,
        };

        if (snprintf(credential_text, sizeof(credential_text), "%d", credential_read) <= 0 ||
            snprintf(host_text, sizeof(host_text), "%d", host_write) <= 0 ||
            snprintf(control_text, sizeof(control_text), "%d", control_child) <= 0)
            _exit(127);
        _exit(airport_itlwm_lab_credential_broker_main(4, arguments));
    }
}

static void
make_start_packet(uint8_t packet[kStartPacketLength], int uppercase_first_hash)
{
    memset(packet, 0, kStartPacketLength);
    memcpy(packet, "START ", 6u);
    memset(packet + 6u, 'a', kDigestTextLength);
    packet[6u + kDigestTextLength] = (uint8_t)' ';
    memset(packet + 6u + kDigestTextLength + 1u, 'b', kDigestTextLength);
    if (uppercase_first_hash)
        packet[6u] = (uint8_t)'A';
}

static int
write_credential_and_close(int *writer, const uint8_t *credential,
                           size_t credential_length)
{
    static const uint8_t newline[] = "\n";
    int written;

    if (writer == NULL || *writer < 0)
        return 0;
    written = write_all(*writer, credential, credential_length) &&
        write_all(*writer, newline, sizeof(newline) - 1u);
    close_fd(writer);
    return written;
}

static int
test_successful_start_and_release(void)
{
    static const uint8_t credential[] = "synthetic-passphrase";
    static const uint8_t arm_token[] = "arm-withdraw\n";
    static const uint8_t withdraw_token[] = "withdraw\n";
    uint8_t start[kStartPacketLength];
    uint8_t expected[128];
    uint8_t observed[128];
    int credential_pipe[2] = { -1, -1 };
    int host_pipe[2] = { -1, -1 };
    int guest_pipe[2] = { -1, -1 };
    int control[2] = { -1, -1 };
    size_t expected_length;
    pid_t child = -1;
    int passed = 0;

    if (!make_pipe(credential_pipe) || !make_pipe(host_pipe) ||
        !make_pipe(guest_pipe) || !make_control_pair(control))
        goto out;
    child = spawn_broker(credential_pipe[0], credential_pipe[1], host_pipe[1],
                         host_pipe[0], control[0], control[1], guest_pipe[0],
                         guest_pipe[1]);
    if (child < 0)
        goto out;
    close_fd(&credential_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    if (!write_credential_and_close(&credential_pipe[1], credential,
                                    sizeof(credential) - 1u))
        goto out;
    if (!receive_status(control[1], "HOST_FED"))
        goto out;
    make_start_packet(start, 0);
    if (!send_packet(control[1], start, sizeof(start), guest_pipe[1]))
        goto out;
    close_fd(&guest_pipe[1]);
    if (!receive_status(control[1], "STARTED"))
        goto out;
    expected_length = sizeof(credential) - 1u;
    memcpy(expected, credential, expected_length);
    expected[expected_length++] = (uint8_t)'\n';
    if (!read_exact(host_pipe[0], observed, sizeof(credential)) ||
        memcmp(observed, expected, sizeof(credential) - 1u) != 0 ||
        observed[sizeof(credential) - 1u] != (uint8_t)'\n' ||
        !read_eof(host_pipe[0]) ||
        !read_exact(guest_pipe[0], observed, expected_length) ||
        memcmp(observed, expected, expected_length) != 0 ||
        !no_data_available(guest_pipe[0]))
        goto out;
    if (!send_packet(control[1], (const uint8_t *)"ARM",
                     sizeof("ARM") - 1u, -1) ||
        !receive_status(control[1], "ARMED") ||
        !read_exact(guest_pipe[0], observed, sizeof(arm_token) - 1u) ||
        memcmp(observed, arm_token, sizeof(arm_token) - 1u) != 0)
        goto out;
    if (!send_packet(control[1], (const uint8_t *)"RELEASE",
                     sizeof("RELEASE") - 1u, -1) ||
        !receive_status(control[1], "RELEASED") ||
        !read_exact(guest_pipe[0], observed, sizeof(withdraw_token) - 1u) ||
        memcmp(observed, withdraw_token, sizeof(withdraw_token) - 1u) != 0 ||
        !read_eof(guest_pipe[0]) || !wait_for_broker(child, 1))
        goto out;
    child = -1;
    passed = 1;

out:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    close_fd(&credential_pipe[0]);
    close_fd(&credential_pipe[1]);
    close_fd(&host_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&guest_pipe[0]);
    close_fd(&guest_pipe[1]);
    close_fd(&control[0]);
    close_fd(&control[1]);
    memset(start, 0, sizeof(start));
    memset(expected, 0, sizeof(expected));
    memset(observed, 0, sizeof(observed));
    return passed;
}

static int
test_rejects_short_credential(void)
{
    static const uint8_t short_credential[] = "short";
    int credential_pipe[2] = { -1, -1 };
    int host_pipe[2] = { -1, -1 };
    int control[2] = { -1, -1 };
    pid_t child = -1;
    int passed = 0;

    if (!make_pipe(credential_pipe) || !make_pipe(host_pipe) ||
        !make_control_pair(control))
        goto out;
    child = spawn_broker(credential_pipe[0], credential_pipe[1], host_pipe[1],
                         host_pipe[0], control[0], control[1], -1, -1);
    if (child < 0)
        goto out;
    close_fd(&credential_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    if (!write_credential_and_close(&credential_pipe[1], short_credential,
                                    sizeof(short_credential) - 1u) ||
        !read_eof(host_pipe[0]) || !wait_for_broker(child, 0))
        goto out;
    child = -1;
    passed = 1;

out:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    close_fd(&credential_pipe[0]);
    close_fd(&credential_pipe[1]);
    close_fd(&host_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    close_fd(&control[1]);
    return passed;
}

static int
test_rejects_additional_credential_record(void)
{
    static const uint8_t multiple_records[] =
        "synthetic-passphrase\nsecond-record\n";
    int credential_pipe[2] = { -1, -1 };
    int host_pipe[2] = { -1, -1 };
    int control[2] = { -1, -1 };
    pid_t child = -1;
    int passed = 0;

    if (!make_pipe(credential_pipe) || !make_pipe(host_pipe) ||
        !make_control_pair(control))
        goto out;
    child = spawn_broker(credential_pipe[0], credential_pipe[1], host_pipe[1],
                         host_pipe[0], control[0], control[1], -1, -1);
    if (child < 0)
        goto out;
    close_fd(&credential_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    if (!write_all(credential_pipe[1], multiple_records,
                   sizeof(multiple_records) - 1u))
        goto out;
    close_fd(&credential_pipe[1]);
    if (!read_eof(host_pipe[0]) || !wait_for_broker(child, 0))
        goto out;
    child = -1;
    passed = 1;

out:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    close_fd(&credential_pipe[0]);
    close_fd(&credential_pipe[1]);
    close_fd(&host_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    close_fd(&control[1]);
    return passed;
}

static int
test_rejects_noncanonical_start(void)
{
    static const uint8_t credential[] = "synthetic-passphrase";
    uint8_t start[kStartPacketLength];
    int credential_pipe[2] = { -1, -1 };
    int host_pipe[2] = { -1, -1 };
    int guest_pipe[2] = { -1, -1 };
    int control[2] = { -1, -1 };
    pid_t child = -1;
    int passed = 0;

    if (!make_pipe(credential_pipe) || !make_pipe(host_pipe) ||
        !make_pipe(guest_pipe) || !make_control_pair(control))
        goto out;
    child = spawn_broker(credential_pipe[0], credential_pipe[1], host_pipe[1],
                         host_pipe[0], control[0], control[1], guest_pipe[0],
                         guest_pipe[1]);
    if (child < 0)
        goto out;
    close_fd(&credential_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    if (!write_credential_and_close(&credential_pipe[1], credential,
                                    sizeof(credential) - 1u))
        goto out;
    if (!receive_status(control[1], "HOST_FED"))
        goto out;
    make_start_packet(start, 1);
    if (!send_packet(control[1], start, sizeof(start), guest_pipe[1]))
        goto out;
    close_fd(&guest_pipe[1]);
    if (!read_eof(guest_pipe[0]) || !wait_for_broker(child, 0))
        goto out;
    child = -1;
    passed = 1;

out:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    close_fd(&credential_pipe[0]);
    close_fd(&credential_pipe[1]);
    close_fd(&host_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&guest_pipe[0]);
    close_fd(&guest_pipe[1]);
    close_fd(&control[0]);
    close_fd(&control[1]);
    memset(start, 0, sizeof(start));
    return passed;
}

static int
test_abort_is_explicit_and_secret_free(void)
{
    static const uint8_t credential[] = "synthetic-passphrase";
    int credential_pipe[2] = { -1, -1 };
    int host_pipe[2] = { -1, -1 };
    int control[2] = { -1, -1 };
    pid_t child = -1;
    int passed = 0;

    if (!make_pipe(credential_pipe) || !make_pipe(host_pipe) ||
        !make_control_pair(control))
        goto out;
    child = spawn_broker(credential_pipe[0], credential_pipe[1], host_pipe[1],
                         host_pipe[0], control[0], control[1], -1, -1);
    if (child < 0)
        goto out;
    close_fd(&credential_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    if (!write_credential_and_close(&credential_pipe[1], credential,
                                    sizeof(credential) - 1u) ||
        !receive_status(control[1], "HOST_FED") ||
        !send_packet(control[1], (const uint8_t *)"ABORT",
                     sizeof("ABORT") - 1u, -1) ||
        !receive_status(control[1], "ABORTED") ||
        !wait_for_broker(child, 1))
        goto out;
    child = -1;
    passed = 1;

out:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    close_fd(&credential_pipe[0]);
    close_fd(&credential_pipe[1]);
    close_fd(&host_pipe[0]);
    close_fd(&host_pipe[1]);
    close_fd(&control[0]);
    close_fd(&control[1]);
    return passed;
}

int
main(void)
{
    if (!test_successful_start_and_release() || !test_rejects_short_credential() ||
        !test_rejects_additional_credential_record() ||
        !test_rejects_noncanonical_start() ||
        !test_abort_is_explicit_and_secret_free())
        return 1;
    puts("PASS: bounded credential broker synthetic behavior");
    return 0;
}
