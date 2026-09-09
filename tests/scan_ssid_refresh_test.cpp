#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sys/types.h>

constexpr unsigned IEEE80211_NWID_LEN = 32;
enum { IEEE80211_STA_CACHE, IEEE80211_STA_BSS, IEEE80211_STA_AUTH,
       IEEE80211_STA_ASSOC, IEEE80211_STA_COLLECT };
struct ieee80211_node {
    unsigned ni_state = IEEE80211_STA_CACHE;
    uint8_t ni_esslen = 0, ni_essid[IEEE80211_NWID_LEN]{};
};
struct ieee80211com { ieee80211_node *ic_bss = nullptr; };

// PRODUCTION_FUNCTION

int main()
{
    ieee80211com ic;
    ieee80211_node node;
    uint8_t first[] = {0, 8, 'o', 'l', 'd', '-', 'n', 'a', 'm', 'e'};
    uint8_t second[] = {0, 3, 'n', 'e', 'w'};
    ieee80211_refresh_scan_ssid(&ic, &node, first);
    assert(node.ni_esslen == 8 && std::memcmp(node.ni_essid, first + 2, 8) == 0);
    ieee80211_refresh_scan_ssid(&ic, &node, second);
    assert(node.ni_esslen == 3 && std::memcmp(node.ni_essid, second + 2, 3) == 0);
    for (unsigned i = 3; i < 32; ++i) assert(node.ni_essid[i] == 0);
    uint8_t hidden[34]{};
    hidden[1] = 32;
    ieee80211_refresh_scan_ssid(&ic, &node, hidden);
    hidden[1] = 0;
    ieee80211_refresh_scan_ssid(&ic, &node, hidden);
    assert(node.ni_esslen == 3 && node.ni_essid[0] == 'n');

    // SSIDs are counted bytes, not C strings: a leading/embedded zero is valid.
    uint8_t binary[] = {0, 4, 0, 'b', 0, 'x'};
    ieee80211_refresh_scan_ssid(&ic, &node, binary);
    assert(node.ni_esslen == 4 && std::memcmp(node.ni_essid, binary + 2, 4) == 0);
    uint8_t longest[34]{};
    longest[1] = 32;
    std::memset(longest + 2, 'z', 32);
    ieee80211_refresh_scan_ssid(&ic, &node, longest);
    assert(node.ni_esslen == 32 && std::memcmp(node.ni_essid, longest + 2, 32) == 0);
    uint8_t oversized[] = {0, 33};
    ieee80211_refresh_scan_ssid(&ic, &node, oversized);
    assert(node.ni_esslen == 32);

    for (unsigned state = IEEE80211_STA_BSS; state <= IEEE80211_STA_COLLECT; ++state) {
        node.ni_state = state;
        ieee80211_refresh_scan_ssid(&ic, &node, second);
        assert(node.ni_esslen == 32 && node.ni_essid[0] == 'z');
    }
    node.ni_state = IEEE80211_STA_CACHE;
    ic.ic_bss = &node; // Also protect the published BSS during state transitions.
    ieee80211_refresh_scan_ssid(&ic, &node, second);
    assert(node.ni_esslen == 32);
    ieee80211_refresh_scan_ssid(nullptr, &node, second);
    ieee80211_refresh_scan_ssid(&ic, nullptr, second);
    ieee80211_refresh_scan_ssid(&ic, &node, nullptr);

    ieee80211_node unseen;
    hidden[1] = 8;
    ieee80211_refresh_scan_ssid(&ic, &unseen, hidden);
    assert(unseen.ni_esslen == 0);
    ieee80211_refresh_scan_ssid(&ic, &unseen, second);
    assert(unseen.ni_esslen == 3);
    std::puts("PASS: production scan SSID refresh, hidden/binary names and association-owner isolation");
}
