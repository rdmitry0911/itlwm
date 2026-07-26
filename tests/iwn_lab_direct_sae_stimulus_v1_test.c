#include <assert.h>
#include <string.h>

#include <ClientKit/AirportItlwmIwnLabDirectSaeStimulusV1.h>

static struct AirportItlwmIwnLabDirectSaeStimulusRequestV1
valid_request(void)
{
    struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 request;

    memset(&request, 0, sizeof(request));
    request.version = kAirportItlwmIwnLabDirectSaeStimulusV1Version;
    request.size = sizeof(request);
    request.profile = kAirportItlwmIwnLabDirectSaeStimulusPureSae;
    request.ssid_len = 1;
    request.password_len =
        kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMinLength;
    request.bssid[0] = 0x02;
    request.ssid[0] = 'x';
    memset(request.password, 'p', request.password_len);
    return request;
}

int
main(void)
{
    struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 request =
        valid_request();
    struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1 ready = {
        kAirportItlwmIwnLabDirectSaeStimulusV1Version,
        sizeof(ready),
        kAirportItlwmIwnLabDirectSaeStimulusReady,
        0,
    };
    size_t index;

    assert(AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    assert(AirportItlwmIwnLabDirectSaeStimulusReadyReplyIsWellFormed(&ready));

    request.profile = 0;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request.profile =
        kAirportItlwmIwnLabDirectSaeStimulusSaeWpa2PskTransition;
    assert(AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));

    request.ssid_len = 0;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request.ssid_len =
        kAirportItlwmIwnLabDirectSaeStimulusV1SsidMaxLength + 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();

    request.password_len =
        kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMinLength - 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request.password_len =
        kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMaxLength + 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();

    memset(request.bssid, 0, sizeof(request.bssid));
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();
    request.bssid[0] |= 0x01;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();

    request.reserved0[0] = 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();
    request.ssid[request.ssid_len] = 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();
    request.password[request.password_len] = 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();
    request.reserved[0] = 1;
    assert(!AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(&request));
    request = valid_request();

    AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request);
    for (index = 0; index < sizeof(request); ++index)
        assert(((const unsigned char *)&request)[index] == 0);

    ready.readiness = 3;
    assert(!AirportItlwmIwnLabDirectSaeStimulusReadyReplyIsWellFormed(&ready));
    return 0;
}
