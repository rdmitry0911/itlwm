#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <strings.h>
#include "definitions.inc"

// Field-only fixtures, not kernel ABI layouts. Crypto primitives are sinks;
// the production policy clear, key selector and encryption dispatcher run below.
struct ieee80211_key { unsigned k_flags; ieee80211_cipher k_cipher; int k_id; };
struct ieee80211_node {
    unsigned ni_flags;
    ieee80211_cipher ni_rsncipher;
    ieee80211_key ni_pairwise_key;
    uint8_t ni_bssid[6], ni_essid[32], ni_pmk[32], ni_pmkid[16];
    unsigned ni_esslen;
};
struct ieee80211_sae_wcl_request { uint8_t ssid[32]; unsigned ssid_len; };
struct ieee80211_sae_wcl_pmk_claim { unsigned active; uint8_t bssid[6], sta[6]; };
struct ieee80211com {
    ieee80211_node *ic_bss;
    ieee80211_sae_wcl_request ic_sae_wcl_request;
    ieee80211_sae_wcl_pmk_claim ic_sae_wcl_pmk_claim;
    uint64_t ic_sae_wcl_policy_generation;
    unsigned ic_flags, ic_pae_mfp_requested, ic_external_pmk_owner;
    unsigned ic_rsnprotos, ic_rsnakms, ic_rsnciphers, ic_des_esslen;
    ieee80211_cipher ic_rsngroupcipher, ic_rsngroupmgmtcipher;
    uint8_t ic_myaddr[6], ic_psk[32], ic_des_essid[32], ic_des_bssid[6];
    uint8_t ic_rsn_ie_override[256];
    ieee80211_key ic_nw_keys[6];
    int ic_def_txkey;
    unsigned pin_clears;
};
struct ieee80211_frame { uint8_t i_fc[2], i_addr1[6]; };
struct packet { bool freed = false; ieee80211_key *used = nullptr; };
using mbuf_t = packet *;
#define IEEE80211_ADDR_EQ(a, b) (std::memcmp((a), (b), 6) == 0)
#define IEEE80211_IS_MULTICAST(a) ((a)[0] & 1)
static void ieee80211_public_initial_bssid_pin_clear_locked(ieee80211com *ic)
{ ++ic->pin_clears; }
static ieee80211_key *ieee80211_bip_active_slot(ieee80211com *ic)
{ return &ic->ic_nw_keys[4]; }
static bool ieee80211_bip_key_is_slot(ieee80211com *ic, ieee80211_key *k)
{ return k == &ic->ic_nw_keys[4] || k == &ic->ic_nw_keys[5]; }
static void mbuf_freem(mbuf_t m) { assert(!m->freed); m->freed = true; }
static unsigned key_errors;
#define XYLog(...) (++key_errors)
[[noreturn]] static void panic(const char *, ...) { std::abort(); }
static mbuf_t cipher_sink(ieee80211com *, mbuf_t m, ieee80211_key *k)
{ m->used = k; return m; }
#define ieee80211_bip_encap cipher_sink
#define ieee80211_wep_encrypt cipher_sink
#define ieee80211_tkip_encrypt cipher_sink
#define ieee80211_ccmp_encrypt cipher_sink
#include "production.inc"

static void cancel_policy(ieee80211com &ic, ieee80211_node &ni)
{
    ic.ic_bss = &ni;
    ic.ic_flags = IEEE80211_F_RSNON | IEEE80211_F_PSK |
        IEEE80211_F_MFPR | IEEE80211_F_DESBSSID;
    ic.ic_sae_wcl_policy_generation = 19;
    ic.ic_sae_wcl_pmk_claim.active = 1;
    ic.ic_pae_mfp_requested = 1;
    ic.ic_rsnprotos = ic.ic_rsnakms = ic.ic_rsnciphers = 0xff;
    ic.ic_external_pmk_owner = 1;
    std::memset(ic.ic_psk, 0x35, sizeof(ic.ic_psk));
    std::memset(ni.ni_pmk, 0x36, sizeof(ni.ni_pmk));
    std::memset(ni.ni_pmkid, 0x37, sizeof(ni.ni_pmkid));
    ni.ni_flags |= IEEE80211_NODE_PMK | IEEE80211_NODE_PMKID;
    ieee80211_sae_wcl_request_policy_clear_locked(&ic);
    assert(ic.ic_flags == 0 && ic.ic_sae_wcl_policy_generation == 0);
    assert(ic.ic_rsnprotos == 0 && ic.ic_rsnakms == 0 && ic.ic_rsnciphers == 0);
    assert(ic.ic_external_pmk_owner == 0 && ic.ic_pae_mfp_requested == 0);
    assert(ic.ic_sae_wcl_pmk_claim.active == 0 && ic.pin_clears == 1);
    for (auto b : ic.ic_psk) assert(b == 0);
    for (auto b : ni.ni_pmk) assert(b == 0);
    for (auto b : ni.ni_pmkid) assert(b == 0);
    assert(!(ni.ni_flags & (IEEE80211_NODE_PMK | IEEE80211_NODE_PMKID)));
}

static void leave_regression()
{
    for (unsigned subtype : {IEEE80211_FC0_SUBTYPE_DEAUTH,
            IEEE80211_FC0_SUBTYPE_DISASSOC, IEEE80211_FC0_SUBTYPE_ACTION}) {
        ieee80211com ic{};
        ieee80211_node ni{};
        ni.ni_flags = IEEE80211_NODE_MFP | IEEE80211_NODE_TXMGMTPROT;
        ni.ni_rsncipher = IEEE80211_CIPHER_CCMP;
        ni.ni_pairwise_key = {IEEE80211_KEY_SWCRYPTO, IEEE80211_CIPHER_CCMP, 0};
        cancel_policy(ic, ni);
        ieee80211_frame wh{{static_cast<uint8_t>(subtype), IEEE80211_FC1_PROTECTED}, {2}};
        auto *key = ieee80211_get_txkey(&ic, &wh, &ni);
        assert(key == &ni.ni_pairwise_key);
        packet m;
        assert(ieee80211_encrypt(&ic, &m, key) == &m);
        assert(!m.freed && m.used == &ni.ni_pairwise_key && key_errors == 0);
        // Once the PTK is actually retired, retain rejection. Never fall back
        // to a populated GTK, fabricate a key or emit this frame unprotected.
        ni.ni_pairwise_key = {};
        ic.ic_nw_keys[0] = {IEEE80211_KEY_SWCRYPTO, IEEE80211_CIPHER_CCMP, 0};
        key = ieee80211_get_txkey(&ic, &wh, &ni);
        assert(key == &ni.ni_pairwise_key);
        packet retired;
        assert(ieee80211_encrypt(&ic, &retired, key) == nullptr);
        assert(retired.freed && !retired.used && key_errors == 1);
        key_errors = 0;
    }
}

static void selector_matrix()
{
    unsigned cases = 0;
    for (unsigned fc = 0; fc < 256; ++fc)
    for (unsigned protected_bit = 0; protected_bit < 2; ++protected_bit)
    for (unsigned multicast = 0; multicast < 2; ++multicast)
    for (unsigned policy = 0; policy < 2; ++policy)
    for (unsigned flags = 0; flags < 4; ++flags)
    for (unsigned group = 0; group < 2; ++group) {
        ieee80211com ic{};
        ieee80211_node ni{};
        ic.ic_flags = policy ? IEEE80211_F_RSNON : 0;
        ic.ic_def_txkey = 2;
        ni.ni_flags = (flags & 1 ? IEEE80211_NODE_MFP : 0) |
            (flags & 2 ? IEEE80211_NODE_TXMGMTPROT : 0);
        ni.ni_rsncipher = group ? IEEE80211_CIPHER_USEGROUP : IEEE80211_CIPHER_CCMP;
        ieee80211_frame wh{{static_cast<uint8_t>(fc),
            static_cast<uint8_t>(protected_bit ? IEEE80211_FC1_PROTECTED : 0)},
            {static_cast<uint8_t>(multicast ? 3 : 2)}};
        bool mgmt = (fc & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT;
        unsigned subtype = fc & IEEE80211_FC0_SUBTYPE_MASK;
        bool robust = mgmt && (subtype == IEEE80211_FC0_SUBTYPE_DEAUTH ||
            subtype == IEEE80211_FC0_SUBTYPE_DISASSOC || subtype == IEEE80211_FC0_SUBTYPE_ACTION);
        ieee80211_key *expected = &ic.ic_nw_keys[2];
        if (!multicast && ((policy && !group) ||
                (flags == 3 && protected_bit && robust)))
            expected = &ni.ni_pairwise_key;
        else if (multicast && mgmt && (flags & 1))
            expected = &ic.ic_nw_keys[4];
        assert(ieee80211_get_txkey(&ic, &wh, &ni) == expected);
        ++cases;
    }
    std::printf("PASS: %u TX selector cases; real policy cancellation, retained PTK, retired-key rejection\n", cases);
}

int main()
{
    leave_regression();
    selector_matrix();
}
