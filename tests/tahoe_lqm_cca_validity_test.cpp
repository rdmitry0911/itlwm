#include "AirportItlwm/TahoeLqmContracts.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "LQM CCA validity: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    using namespace TahoeLqmContracts;
    const CounterSnapshot current{2, 3, 100, 200, 50};
    const CounterSnapshot old{1, 2, 90, 180, 40};
    const int32_t noises[]{-95, -90, 0, -127};
    const CounterSnapshot *previous[]{nullptr, &old, &current};

    // Inspect the actual wire offsets, independently of the field names.
    // This same test compiles against the previous production header and
    // rejects its RSSI-as-CCA bytes rather than merely a renamed member.
    for (int32_t rssi = -100; rssi <= 0; ++rssi) {
        for (int32_t noise : noises) {
            for (const auto *prior : previous) {
                EventData event;
                std::memset(&event, 0xa5, sizeof(event));
                require(buildEventData(rssi, noise, current, prior, &event),
                        "real RSSI sample was rejected");
                const auto *wire = reinterpret_cast<const uint8_t *>(&event);
                require(wire[0x12] == 0 && wire[0x13] == 0,
                        "missing independent CCA sample was published as valid");
                require(wire[0] == 1 && event.rssi == rssi && wire[0x1d8] == 1,
                        "CCA omission must preserve the real signal event");
                const bool hasNoise = noise != 0 && noise != -127;
                require(event.hasNoise == hasNoise && event.hasSnr == hasNoise,
                        "CCA omission changed independent noise/SNR validity");
                if (hasNoise) {
                    const int32_t snr = rssi - noise < 0 ? 0 : rssi - noise;
                    require(event.noise == noise && event.snr == snr,
                            "CCA omission changed measured noise or derived SNR");
                }
                const bool changed = prior != &current;
                require(event.countersValid == changed &&
                            event.counterSnapshotChanged == changed,
                        "CCA omission changed the counter generation gates");
                require(event.txErrors == (changed ? current.txErrors : 0) &&
                            event.rxErrors == (changed ? current.rxErrors : 0) &&
                            event.txFrames == (changed ? current.txFrames : 0) &&
                            event.rxFrames == (changed ? current.rxFrames : 0) &&
                            event.beaconFrames == (changed ? current.beaconFrames : 0),
                        "CCA omission changed counter contents");
            }
        }
    }
    const int32_t invalidSamples[]{-101, 1};
    for (int32_t invalid : invalidSamples) {
        EventData event;
        std::memset(&event, 0xa5, sizeof(event));
        const EventData before = event;
        require(!buildEventData(invalid, -95, current, nullptr, &event),
                "invalid RSSI was accepted");
        require(std::memcmp(&event, &before, sizeof(event)) == 0,
                "rejected sample changed the output");
    }
    require(!buildEventData(-33, -90, current, nullptr, nullptr),
            "null output was accepted");
    std::puts("PASS: production LQM CCA validity, 1212 signal/counter combinations");
}
