#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define SIOCSA80211 2150656456UL
#define SIOCGA80211 3224398281UL
#define APPLE80211_IOC_CARD_CAPABILITIES 12
#define APPLE80211_IOC_CHANNEL 4
#define APPLE80211_IOC_POWER 19
#define APPLE80211_IOC_HOST_AP_MODE 25
#define APPLE80211_IOC_HOST_AP_MODE_START 1
#define APPLE80211_IOC_HOST_AP_MODE_HIDDEN 336
#define APPLE80211_IOC_SOFTAP_TRIGGER_CSA 349
#define APPLE80211_IOC_VIRTUAL_IF_CREATE 94
#define APPLE80211_VERSION 1
#define APPLE80211_VIF_SOFT_AP 7
#define APPLE80211_MAX_SSID_LEN 32
#define APPLE80211_MAX_RADIO 4
#define APPLE80211_AUTHTYPE_OPEN 0
#define APPLE80211_AUTHTYPE_WPA2_PSK 0x8
#define APPLE80211_AUTHTYPE_WPA3_SAE 0x1000
#define APPLE80211_CHANNEL_2GHZ_20MHZ 0x8a
#define KIORETURN_BUSY ((int32_t)0xe00002d5)
#define KIORETURN_NOT_READY ((int32_t)0xe00002d8)

struct apple80211req {
    char req_if_name[IFNAMSIZ];
    int req_type;
    int req_val;
    uint32_t req_len;
    void *req_data;
};

struct apple80211_virt_if_create_data {
    uint32_t version;
    uint8_t mac[6];
    uint16_t reserved;
    uint32_t role;
    uint8_t bsd_name[IFNAMSIZ];
} __attribute__((packed));

struct apple80211_channel {
    uint32_t version;
    uint32_t channel;
    uint32_t flags;
};

struct apple80211_channel_data {
    uint32_t version;
    struct apple80211_channel channel;
};

struct apple80211_power_data {
    uint32_t version;
    uint32_t num_radios;
    uint32_t power_state[APPLE80211_MAX_RADIO];
};

struct apple80211_capability_data {
    uint32_t version;
    uint8_t capabilities[24];
};

struct airport_itlwm_host_ap_mode {
    uint32_t version00;
    uint32_t flags04;
    uint32_t auth_lower08;
    uint32_t auth_upper0c;
    uint32_t channel_version10;
    uint32_t channel_number14;
    uint32_t channel_flags18;
    uint32_t ssid_length1c;
    uint8_t ssid20[APPLE80211_MAX_SSID_LEN];
    uint32_t reserved40;
    uint32_t credential_length44;
    uint8_t reserved48[0x08];
    uint8_t credential50[0x40];
    uint8_t reserved0090[0x24c];
    uint32_t vendor_ie_length2dc;
    uint8_t vendor_ie_data2e0[1];
} __attribute__((packed));

struct airport_itlwm_host_ap_mode_hidden {
    uint32_t version00;
    uint32_t hidden04;
} __attribute__((packed));

struct airport_itlwm_softap_csa {
    uint32_t version00;
    uint32_t channel_version04;
    uint32_t channel_number08;
    uint32_t channel_flags0c;
    uint8_t mode10;
    uint8_t reserved11[3];
    uint8_t feature_gate14;
} __attribute__((packed));

static int
set_apple80211(int fd, const char *ifname, int selector, int value,
               void *data, uint32_t length)
{
    struct apple80211req request;
    memset(&request, 0, sizeof(request));
    snprintf(request.req_if_name, sizeof(request.req_if_name), "%s", ifname);
    request.req_type = selector;
    request.req_val = value;
    request.req_len = length;
    request.req_data = data;
    errno = 0;
    const int result = ioctl(fd, SIOCSA80211, &request);
    const int saved_errno = errno;
    printf("selector=%d interface=%s result=%d errno=%d (%s)\n",
           selector, ifname, result, saved_errno, strerror(saved_errno));
    errno = saved_errno;
    return result;
}

static int
get_card_capabilities(int fd, const char *ifname)
{
    struct apple80211req request;
    struct apple80211_capability_data capabilities;

    memset(&request, 0, sizeof(request));
    memset(&capabilities, 0, sizeof(capabilities));
    snprintf(request.req_if_name, sizeof(request.req_if_name), "%s", ifname);
    request.req_type = APPLE80211_IOC_CARD_CAPABILITIES;
    request.req_len = sizeof(capabilities);
    request.req_data = &capabilities;
    errno = 0;
    const int result = ioctl(fd, SIOCGA80211, &request);
    const int saved_errno = errno;
    printf("selector=%d interface=%s result=%d errno=%d (%s) version=%u caps=",
           APPLE80211_IOC_CARD_CAPABILITIES, ifname, result, saved_errno,
           strerror(saved_errno), capabilities.version);
    for (size_t i = 0; i < sizeof(capabilities.capabilities); i++)
        printf("%02x", capabilities.capabilities[i]);
    printf("\n");
    errno = saved_errno;
    return result;
}

static int
bring_interface_up(int fd, const char *ifname)
{
    struct ifreq request;
    int result = -1;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        memset(&request, 0, sizeof(request));
        snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", ifname);
        result = ioctl(fd, SIOCGIFFLAGS, &request);
        if (result == 0 || errno != ENXIO)
            break;
        usleep(10000);
    }
    if (result != 0) {
        perror("SIOCGIFFLAGS");
        return -1;
    }
    if ((request.ifr_flags & IFF_UP) != 0)
        return 0;
    request.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &request) != 0) {
        perror("SIOCSIFFLAGS");
        return -1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--card-capabilities") == 0) {
        const char *ifname = argc > 2 ? argv[2] : "en1";
        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            perror("socket");
            return 2;
        }
        const int result = get_card_capabilities(fd, ifname);
        close(fd);
        return result == 0 ? 0 : 1;
    }

    const char *station_ifname = argc > 1 ? argv[1] : "en1";
    const char *ssid = argc > 2 ? argv[2] : "AIAMap6235";
    const unsigned long requested_channel =
        argc > 3 ? strtoul(argv[3], NULL, 10) : 153;
    const unsigned long hold_seconds =
        argc > 4 ? strtoul(argv[4], NULL, 10) : 0;
    const char *security = argc > 5 ? argv[5] : "open";
    const char *password = argc > 6 ? argv[6] : "";
    const char *hidden_mode = argc > 7 ? argv[7] : "visible";
    const int hidden_requested = strcmp(hidden_mode, "hidden") == 0 ||
        strcmp(hidden_mode, "toggle") == 0;
    const int toggle_hidden = strcmp(hidden_mode, "toggle") == 0;
    const unsigned long csa_channel =
        argc > 8 ? strtoul(argv[8], NULL, 10) : 0;
    const unsigned long csa_mode =
        argc > 9 ? strtoul(argv[9], NULL, 10) : 0;
    const unsigned long csa_delay_seconds =
        argc > 10 ? strtoul(argv[10], NULL, 10) : 0;
    const int stop_only = strcmp(ssid, "--stop-only") == 0;
    const int create_only = strcmp(ssid, "--create-only") == 0;
    const int hidden_only = strcmp(ssid, "--hidden-only") == 0;
    const size_t ssid_length = strlen(ssid);
    const size_t password_length = strlen(password);
    uint32_t auth_upper = APPLE80211_AUTHTYPE_OPEN;
    if (strcmp(security, "wpa2") == 0)
        auth_upper = APPLE80211_AUTHTYPE_WPA2_PSK;
    else if (strcmp(security, "wpa3") == 0)
        auth_upper = APPLE80211_AUTHTYPE_WPA3_SAE;
    else if (strcmp(security, "open") != 0) {
        fprintf(stderr, "invalid security mode\n");
        return 2;
    }
    if (strcmp(hidden_mode, "visible") != 0 && !hidden_requested) {
        fprintf(stderr, "invalid hidden mode\n");
        return 2;
    }
    if ((!stop_only && !create_only && !hidden_only &&
         (ssid_length == 0 || ssid_length > APPLE80211_MAX_SSID_LEN)) ||
        requested_channel == 0 || requested_channel > UINT32_MAX ||
        csa_channel > UINT8_MAX || csa_mode > 1 ||
        (auth_upper == APPLE80211_AUTHTYPE_OPEN && password_length != 0) ||
        (auth_upper != APPLE80211_AUTHTYPE_OPEN &&
         (password_length < 8 || password_length > 63))) {
        fprintf(stderr, "invalid SSID, channel, or credential\n");
        return 2;
    }

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 2;
    }

    if (hidden_only) {
        struct airport_itlwm_host_ap_mode_hidden hidden;
        memset(&hidden, 0, sizeof(hidden));
        hidden.version00 = APPLE80211_VERSION;
        hidden.hidden04 = hidden_requested ? 1 : 0;
        const int result = set_apple80211(
            fd, station_ifname, APPLE80211_IOC_HOST_AP_MODE_HIDDEN, 0,
            &hidden, sizeof(hidden));
        close(fd);
        return result == 0 ? 0 : 1;
    }

    struct apple80211_virt_if_create_data create;
    memset(&create, 0, sizeof(create));
    create.version = APPLE80211_VERSION;
    create.role = APPLE80211_VIF_SOFT_AP;
    memcpy(create.bsd_name, "ap1", 4);
    if (set_apple80211(fd, station_ifname,
                       APPLE80211_IOC_VIRTUAL_IF_CREATE, 0,
                       &create, sizeof(create)) != 0) {
        close(fd);
        return 1;
    }
    if (create_only) {
        close(fd);
        return 0;
    }

    /*
     * VIRTUAL_IF_CREATE publishes the role but does not administratively
     * enable its BSD interface.  Internet Sharing brings the role interface
     * up before it applies CHANNEL/HOST_AP_MODE; reproduce that public
     * lifecycle so the driver's availability gate is not tested while ap1
     * is deliberately down.
     */
    if (bring_interface_up(fd, "ap1") != 0) {
        close(fd);
        return 1;
    }

    /*
     * A previous lab client can leave the persistent role-7 owner marked up
     * after its public control socket closes.  Drive the public HostAP stop
     * edge before powering/configuring a fresh run so repeated on-air tests
     * exercise a real lower-HAL start instead of a stale successful no-op.
     */
    struct airport_itlwm_host_ap_mode host_ap;
    memset(&host_ap, 0, sizeof(host_ap));
    int host_ap_stop_result = -1;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        host_ap_stop_result =
            set_apple80211(fd, "ap1", APPLE80211_IOC_HOST_AP_MODE, 0,
                           &host_ap, sizeof(host_ap));
        if (host_ap_stop_result == 0 || errno != ENXIO)
            break;
        usleep(10000);
    }
    if (host_ap_stop_result != 0) {
        close(fd);
        return 1;
    }
    if (stop_only) {
        close(fd);
        return 0;
    }

    /*
     * Role creation does not implicitly change the user's radio state.
     * Internet Sharing requests radio power before configuring its AP role;
     * reproduce that public lifecycle so AP start also works after Tahoe has
     * powered an idle station interface down.
     */
    struct apple80211_power_data power;
    memset(&power, 0, sizeof(power));
    power.version = APPLE80211_VERSION;
    power.num_radios = 1;
    power.power_state[0] = 1;
    if (set_apple80211(fd, station_ifname, APPLE80211_IOC_POWER, 0,
                       &power, sizeof(power)) != 0) {
        close(fd);
        return 1;
    }

    struct apple80211_channel_data channel;
    memset(&channel, 0, sizeof(channel));
    channel.version = APPLE80211_VERSION;
    channel.channel.version = APPLE80211_VERSION;
    channel.channel.channel = (uint32_t)requested_channel;
    int channel_result = -1;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        channel_result =
            set_apple80211(fd, "ap1", APPLE80211_IOC_CHANNEL, 0,
                           &channel, sizeof(channel));
        if (channel_result == 0 || errno != ENXIO)
            break;
        usleep(10000);
    }
    if (channel_result != 0) {
        close(fd);
        return 1;
    }

    memset(&host_ap, 0, sizeof(host_ap));
    /*
     * Reproduce the recovered CoreWLAN/airportd network-data carrier.  The
     * separate CHANNEL selector above is part of the public lifecycle, while
     * HOST_AP_MODE also embeds the selected CWChannel at +0x10/+0x14/+0x18.
     */
    host_ap.version00 = APPLE80211_VERSION;
    host_ap.flags04 = 2;
    host_ap.auth_lower08 = auth_upper == APPLE80211_AUTHTYPE_OPEN ? 0 : 1;
    host_ap.auth_upper0c = auth_upper;
    host_ap.channel_version10 = APPLE80211_VERSION;
    host_ap.channel_number14 = (uint32_t)requested_channel;
    host_ap.channel_flags18 = APPLE80211_CHANNEL_2GHZ_20MHZ;
    host_ap.ssid_length1c = (uint32_t)ssid_length;
    memcpy(host_ap.ssid20, ssid, ssid_length);
    host_ap.credential_length44 = (uint32_t)password_length;
    memcpy(host_ap.credential50, password, password_length);
    int host_ap_result = -1;
    /*
     * A cold Tahoe boot can schedule an initial active scan followed by one
     * or more passive maintenance scans.  Keep this bounded, but cover the
     * complete public scan cadence rather than assuming a ten-second window.
     */
    for (unsigned int attempt = 0; attempt < 10000; attempt++) {
        host_ap_result =
            set_apple80211(fd, "ap1", APPLE80211_IOC_HOST_AP_MODE,
                           APPLE80211_IOC_HOST_AP_MODE_START,
                           &host_ap, sizeof(host_ap));
        if (host_ap_result == 0)
            break;

        /*
         * Tahoe can start its automatic foreground scan after POWER returns.
         * The Apple80211 ioctl bridge exposes the transient IOReturn as a
         * negative errno value.  Let that public lifecycle finish rather than
         * bypassing the driver's scan/AP serialization.
         */
        if (errno != KIORETURN_BUSY && errno != KIORETURN_NOT_READY)
            break;
        usleep(10000);
    }
    if (host_ap_result != 0) {
        close(fd);
        return 1;
    }

    struct airport_itlwm_host_ap_mode_hidden hidden;
    memset(&hidden, 0, sizeof(hidden));
    hidden.version00 = APPLE80211_VERSION;
    hidden.hidden04 = hidden_requested ? 1 : 0;
    if (hidden_requested &&
        set_apple80211(fd, station_ifname,
                       APPLE80211_IOC_HOST_AP_MODE_HIDDEN, 0,
                       &hidden, sizeof(hidden)) != 0) {
        close(fd);
        return 1;
    }

    if (csa_channel != 0) {
        if (csa_delay_seconds != 0) {
            printf("waiting %lu seconds before CSA to channel %lu\n",
                   csa_delay_seconds, csa_channel);
            fflush(stdout);
            sleep((unsigned int)csa_delay_seconds);
        }
        struct airport_itlwm_softap_csa csa;
        memset(&csa, 0, sizeof(csa));
        csa.version00 = APPLE80211_VERSION;
        csa.channel_version04 = APPLE80211_VERSION;
        csa.channel_number08 = (uint32_t)csa_channel;
        csa.channel_flags0c = APPLE80211_CHANNEL_2GHZ_20MHZ;
        csa.mode10 = (uint8_t)csa_mode;
        if (set_apple80211(fd, station_ifname,
                           APPLE80211_IOC_SOFTAP_TRIGGER_CSA, 0,
                           &csa, sizeof(csa)) != 0) {
            close(fd);
            return 1;
        }
    }

    if (hold_seconds != 0) {
        printf("holding Apple80211 control socket for %lu seconds\n",
               hold_seconds);
        fflush(stdout);
        sleep((unsigned int)hold_seconds);
    }

    if (toggle_hidden) {
        hidden.hidden04 = 0;
        if (set_apple80211(fd, station_ifname,
                          APPLE80211_IOC_HOST_AP_MODE_HIDDEN, 0,
                          &hidden, sizeof(hidden)) != 0) {
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
