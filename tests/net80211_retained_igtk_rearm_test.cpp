#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define IEEE80211_BIP_KEYLEN 16
#define IEEE80211_CIPHER_BIP 0x80
#define IEEE80211_KEY_IGTK 0x10
#define IEEE80211_NODE_MFP 0x20000
#define IEEE80211_NODE_RXMGMTPROT 0x20
#define IEEE80211_NODE_TXMGMTPROT 0x40

struct ieee80211_key {
    unsigned k_id, k_cipher, k_flags, k_len;
    uint8_t k_key[32];
    uint64_t k_mgmt_rsc, k_mgmt_tsc;
    void *k_priv;
};
struct ieee80211_node { uint32_t ni_flags; };
struct ieee80211_bip_ctx { int published, retired; };
struct ieee80211com {
    ieee80211_node *ic_bss;
    ieee80211_key ic_nw_keys[6];
    unsigned ic_igtk_kid;
};

// PRODUCTION_FUNCTIONS

int main()
{
    for (unsigned kid : {4U, 5U}) {
        ieee80211_node node{IEEE80211_NODE_MFP | 0x18}, other{};
        ieee80211_bip_ctx context{1, 0};
        ieee80211com ic{};
        ic.ic_bss = &node;
        auto &slot = ic.ic_nw_keys[kid];
        slot.k_id = kid;
        slot.k_cipher = IEEE80211_CIPHER_BIP;
        slot.k_flags = IEEE80211_KEY_IGTK;
        slot.k_len = IEEE80211_BIP_KEYLEN;
        memset(slot.k_key, 0x5a, slot.k_len);
        slot.k_priv = &context;
        slot.k_mgmt_rsc = 900;
        slot.k_mgmt_tsc = 700;
        auto witness = slot;
        witness.k_priv = nullptr;
        witness.k_mgmt_rsc = 1; // An equal KDE must never roll IPN back.
        witness.k_mgmt_tsc = 0;
        const auto before = slot;
        for (int repeat = 0; repeat < 2; ++repeat) {
            node.ni_flags &= ~(IEEE80211_NODE_RXMGMTPROT |
                               IEEE80211_NODE_TXMGMTPROT);
            assert(ieee80211_bip_key_rearm_locked(&ic, &node, &witness) == 0);
            assert((node.ni_flags & 0x60) == 0x60);
            assert(ic.ic_igtk_kid == kid);
            assert(memcmp(&slot, &before, sizeof(slot)) == 0);
            assert(context.published == 1 && context.retired == 0);
        }

        auto rejected = [&](const ieee80211_key &value,
                            ieee80211_node *selected = nullptr) {
            node.ni_flags = IEEE80211_NODE_MFP | 0x18;
            ic.ic_igtk_kid = 0;
            const auto unchanged = slot;
            assert(ieee80211_bip_key_rearm_locked(&ic,
                selected ? selected : &node, &value) != 0);
            assert(node.ni_flags == (IEEE80211_NODE_MFP | 0x18));
            assert(ic.ic_igtk_kid == 0);
            assert(memcmp(&slot, &unchanged, sizeof(slot)) == 0);
        };
        auto changed = witness;
        changed.k_key[0] ^= 1;
        rejected(changed);
        rejected(witness, &other);
        changed = witness;
        changed.k_id = 6;
        rejected(changed);
        changed = witness;
        changed.k_len = 32;
        rejected(changed);
        changed = witness;
        changed.k_priv = &context;
        rejected(changed);
        context.retired = 1;
        rejected(witness);
        context.retired = 0;
        context.published = 0;
        rejected(witness);
        context.published = 1;
        slot.k_priv = nullptr;
        rejected(witness);
        slot.k_priv = &context;
        node.ni_flags = 0;
        assert(ieee80211_bip_key_rearm_locked(&ic, &node, &witness) != 0);
        assert(node.ni_flags == 0);
    }
    puts("PASS: retained IGTK rearms PMF without replacing keys or replay state");
}
