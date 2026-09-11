/* Host-only identity of one deferred source-to-target BSS handoff.
 * This is neither a node reference nor a hardware drain receipt. */
#ifndef _NET80211_IEEE80211_BSS_SWITCH_H_
#define _NET80211_IEEE80211_BSS_SWITCH_H_

struct ieee80211_bss_switch_identity {
    u_int64_t source_epoch;
    u_int64_t continuation_epoch;
    u_int64_t join_sequence;
    u_int64_t reassoc_sequence;
    u_int64_t reassoc_serial;
    u_int8_t source_macaddr[IEEE80211_ADDR_LEN];
    u_int8_t source_bssid[IEEE80211_ADDR_LEN];
    u_int8_t target_macaddr[IEEE80211_ADDR_LEN];
    u_int8_t target_bssid[IEEE80211_ADDR_LEN];
};

struct ieee80211com;
/* Caller holds ic_pae_selected_bss_lock; no callbacks or allocation. */
int ieee80211_bss_switch_identity_current_locked(struct ieee80211com *,
    const struct ieee80211_bss_switch_identity *);
u_int64_t ieee80211_pae_assoc_epoch_begin_bss_switch(struct ieee80211com *,
    const struct ieee80211_bss_switch_identity *);

#endif
