#include <atomic>
#include <thread>
#include <vector>
#include "scan_policy_test_support.hpp"

int main()
{
    IOSimpleLock lock;
    ieee80211com ic;
    ieee80211_wcl_scan_plan out = {}, a = makePlan(1, 1), b = makePlan(2, 2);
    ic.ic_pae_selected_bss_lock = &lock;
    stagePlan(ic, a);
    ieee80211_wcl_scan_plan_clear(&ic, 0);
    ic.ic_pae_selected_bss_lock = nullptr;
    assert(ieee80211_wcl_scan_plan_stage(nullptr, &a) == EINVAL);
    assert(ieee80211_wcl_scan_plan_stage(&ic, nullptr) == EINVAL);
    assert(ieee80211_wcl_scan_plan_stage(&ic, &a) == ENXIO);
    assert(!ieee80211_wcl_scan_plan_snapshot(&ic, &out));
    ic.ic_pae_selected_bss_lock = &lock;
    assert(!ieee80211_wcl_scan_plan_snapshot(&ic, nullptr));
    assert(!ieee80211_wcl_scan_plan_snapshot(&ic, &out));
    assert(!out.active && !out.generation);
    auto bad = a;
    bad.generation = 0;
    assert(ieee80211_wcl_scan_plan_stage(&ic, &bad) == EINVAL);
    bad = a;
    bad.ssid_len = IEEE80211_NWID_LEN + 1;
    assert(ieee80211_wcl_scan_plan_stage(&ic, &bad) == EINVAL);
    bad = a;
    bad.requested_channel_count = IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS + 1;
    assert(ieee80211_wcl_scan_plan_stage(&ic, &bad) == EINVAL);
    stagePlan(ic, a);
    assert(ieee80211_wcl_scan_plan_stage(&ic, &b) == EBUSY);
    ieee80211_wcl_scan_plan_clear(&ic, b.generation);
    assert(ieee80211_wcl_scan_plan_snapshot(&ic, &out));
    assert(out.generation == a.generation && out.ssid[31] == 1);
    stagePlan(ic, b);
    assert(out.generation == a.generation && out.ssid[31] == 1);
    ieee80211_wcl_scan_plan_clear(&ic, a.generation);
    assert(ieee80211_wcl_scan_plan_snapshot(&ic, &out));
    assert(out.generation == b.generation);
    ieee80211_wcl_scan_plan_clear(&ic, b.generation);
    assert(!ieee80211_wcl_scan_plan_snapshot(&ic, &out));
    assert(!out.active && !out.generation && !out.ssid[31]);

    ItlScanCommandPolicy policy = {};
    assert(capturePolicyAtFixtureReservation(&ic, true, &policy) == ECANCELED);
    assert(!policy.plan.active);
    stagePlan(ic, a);
    homeAwayReads = 0;
    assert(capturePolicyAtFixtureReservation(&ic, true, &policy) == 0);
    assert(homeAwayReads == 1);
    stagePlan(ic, b);
    assert(policy.plan.generation == 1 && policy.plan.ssid[31] == 1);
    ic.ic_des_esslen = IEEE80211_NWID_LEN;
    std::memset(ic.ic_des_essid, 3, sizeof(ic.ic_des_essid));
    assert(capturePolicyAtFixtureReservation(&ic, false, &policy) == 0);
    std::memset(ic.ic_des_essid, 4, sizeof(ic.ic_des_essid));
    assert(!policy.plan.active && policy.plan.ssid[31] == 3);
    ic.ic_des_esslen = IEEE80211_NWID_LEN + 1;
    assert(capturePolicyAtFixtureReservation(&ic, false, &policy) == EINVAL);

    // Production channel predicate, including band-specific channel-number
    // overlap and explicit empty/inactive plan behaviour.
    ieee80211_channel channel = {};
    channel.ic_freq = 2452;
    channel.ic_flags = IEEE80211_CHAN_2GHZ;
    a.active = 1;
    assert(ieee80211_wcl_scan_plan_channel_allowed(&ic, &a, &channel));
    channel.ic_freq = 5045;
    channel.ic_flags = IEEE80211_CHAN_5GHZ;
    assert(!ieee80211_wcl_scan_plan_channel_allowed(&ic, &a, &channel));
    setbit(a.channel_any, 9);
    assert(ieee80211_wcl_scan_plan_channel_allowed(&ic, &a, &channel));

    std::atomic<bool> done{false};
    std::atomic<unsigned> samples{0};
    std::vector<std::thread> readers;
    for (unsigned reader = 0; reader < 4; ++reader) {
        readers.emplace_back([&] {
            do {
                ieee80211_wcl_scan_plan snapshot = {};
                if (ieee80211_wcl_scan_plan_snapshot(&ic, &snapshot)) {
                    const uint8_t marker = snapshot.generation % 80;
                    assert(snapshot.active == 1);
                    assert(snapshot.ssid_len == IEEE80211_NWID_LEN);
                    for (uint8_t byte : snapshot.ssid)
                        assert(byte == marker);
                    assert(snapshot.active_dwell_ms == 17U + marker);
                    assert(snapshot.passive_dwell_ms == 117U + marker);
                    assert(snapshot.home_dwell_ms == 217U + marker);
                    ++samples;
                }
            } while (!done.load());
        });
    }
    for (uint64_t generation = 3; generation < 20003; ++generation)
        stagePlan(ic, makePlan(generation, generation % 80));
    done = true;
    for (auto &reader : readers)
        reader.join();
    assert(samples > 0);
    std::printf("scan policy: common validation/retirement/copy and 20000 concurrent replacements PASS (%u snapshots)\n", samples.load());
}
