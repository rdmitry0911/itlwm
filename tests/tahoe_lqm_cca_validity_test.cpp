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
    // Right-domain channel-occupancy inputs: unavailable (<0), the 0-100 range,
    // and an over-range value that must clamp to 100.
    const int32_t ccaInputs[]{-1, 0, 1, 37, 100, 250};

    // Inspect the actual wire offsets, independently of the field names, so a
    // renamed member cannot pass without the real +0x12/+0x13 bytes changing.
    for (int32_t rssi = -100; rssi <= 0; ++rssi) {
        for (int32_t noise : noises) {
            for (int32_t cca : ccaInputs) {
                for (const auto *prior : previous) {
                    EventData event;
                    std::memset(&event, 0xa5, sizeof(event));
                    require(buildEventData(rssi, noise, cca, current, prior,
                                           &event),
                            "real RSSI sample was rejected");
                    const auto *wire =
                        reinterpret_cast<const uint8_t *>(&event);
                    if (cca < 0) {
                        require(wire[0x12] == 0 && wire[0x13] == 0,
                                "unavailable CCA must leave +0x12/+0x13 clear");
                    } else {
                        const uint8_t expected =
                            static_cast<uint8_t>(cca > 100 ? 100 : cca);
                        require(wire[0x12] == 1 && wire[0x13] == expected,
                                "valid CCA must publish clamped occupancy");
                    }
                    require(wire[0] == 1 && event.rssi == rssi &&
                                wire[0x1d8] == 1,
                            "CCA feed must preserve the real signal event");
                    const bool hasNoise = noise != 0 && noise != -127;
                    require(event.hasNoise == hasNoise &&
                                event.hasSnr == hasNoise,
                            "CCA feed changed independent noise/SNR validity");
                    if (hasNoise) {
                        const int32_t snr =
                            rssi - noise < 0 ? 0 : rssi - noise;
                        require(event.noise == noise && event.snr == snr,
                                "CCA feed changed measured noise or SNR");
                    }
                    const bool changed = prior != &current;
                    require(event.countersValid == changed &&
                                event.counterSnapshotChanged == changed,
                            "CCA feed changed the counter generation gates");
                    require(event.txErrors ==
                                    (changed ? current.txErrors : 0) &&
                                event.rxErrors ==
                                    (changed ? current.rxErrors : 0) &&
                                event.txFrames ==
                                    (changed ? current.txFrames : 0) &&
                                event.rxFrames ==
                                    (changed ? current.rxFrames : 0) &&
                                event.beaconFrames ==
                                    (changed ? current.beaconFrames : 0),
                            "CCA feed changed counter contents");
                }
            }
        }
    }
    const int32_t invalidSamples[]{-101, 1};
    for (int32_t invalid : invalidSamples) {
        EventData event;
        std::memset(&event, 0xa5, sizeof(event));
        const EventData before = event;
        require(!buildEventData(invalid, -95, 42, current, nullptr, &event),
                "invalid RSSI was accepted");
        require(std::memcmp(&event, &before, sizeof(event)) == 0,
                "rejected sample changed the output");
    }
    require(!buildEventData(-33, -90, 42, current, nullptr, nullptr),
            "null output was accepted");
    std::puts("PASS: production LQM CCA occupancy feed, right-domain 0-100 clamp");
}
