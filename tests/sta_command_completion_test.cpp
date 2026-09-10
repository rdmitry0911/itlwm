/* Full production add/drain/remove methods and firmware headers. Kernel
 * softc/node fields and transport completions are explicit fixture boundaries;
 * this verifies ownership publication, not an on-air association. */
#include "scan_test_byte_order.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <type_traits>
#include <vector>
using std::min;
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using u64 = uint64_t; using s8 = int8_t; using s16 = int16_t;
using s32 = int32_t;
using __le16 = uint16_t; using __le32 = uint32_t; using __le64 = uint64_t;
using __be16 = uint16_t;
using bus_addr_t = uint64_t;
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#ifndef NBBY
#define NBBY 8
#endif
#ifndef howmany
#define howmany(x, y) (((x) + (y) - 1) / (y))
#endif
#define ETHER_ADDR_LEN 6
#define BIT(x) (1U << (x))
#define __BIT(x) BIT(x)
#define le32_to_cpup(x) le32toh(*(x))
#define cpu_to_le16(x) htole16(x)
#define cpu_to_le32(x) htole32(x)
#define isset(a, b) ((a)[(b) / 8] & (1U << ((b) % 8)))
#define setbit(a, b) ((a)[(b) / 8] |= (1U << ((b) % 8)))
#define IEEE80211_ADDR_COPY(a, b) std::memcpy((a), (b), 6)
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "fixture"
#include "sta-defines.inc"
#include "itlwm/hal_iwm/if_iwmreg.h"
#include "itlwm/hal_iwx/if_iwxreg.h"

enum { IEEE80211_M_STA, IEEE80211_M_MONITOR, IEEE80211_S_ASSOC = 3 };
enum { IEEE80211_NODE_HT = 1, IEEE80211_NODE_VHT = 2, IEEE80211_NODE_HE = 4 };
enum { IEEE80211_CHAN_WIDTH_20, IEEE80211_CHAN_WIDTH_40,
       IEEE80211_CHAN_WIDTH_80, IEEE80211_CHAN_WIDTH_160,
       IEEE80211_CHAN_WIDTH_80P80 };
enum { EDCA_NUM_AC = 4, IEEE80211_AMPDU_PARAM_SS = 7,
       IEEE80211_AMPDU_PARAM_SS_2 = 4, IEEE80211_AMPDU_PARAM_SS_4 = 5,
       IEEE80211_AMPDU_PARAM_SS_8 = 6, IEEE80211_AMPDU_PARAM_SS_16 = 7 };
constexpr uint32_t IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK = 7U << 23;
constexpr uint32_t IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT = 23;
constexpr uint8_t IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK = 0x18;
#define splassert(x) ((void)0)
#define IPL_NET 0
constexpr int IEEE80211_NUM_TID = 16, IEEE80211_BA_AGREED = 1;
struct ieee80211_tx_ba { int ba_state = 0; };
static uint8_t u8_get_bits(uint8_t value, uint8_t mask)
{ return (value & mask) / (mask & -mask); }
static uint8_t etheranyaddr[6];
struct ieee80211_node {
    unsigned ni_flags = 0, ni_chw = 0, ni_rx_nss = 2;
    uint32_t ni_vhtcaps = 3U << 23;
    struct { uint8_t mac_cap_info[6] = {}; } ni_he_cap_elem;
    ieee80211_tx_ba ni_tx_ba[IEEE80211_NUM_TID];
};
struct ieee80211com {
    int ic_opmode = IEEE80211_M_STA, ic_state = IEEE80211_S_ASSOC;
    unsigned ic_ampdu_params = 4;
    ieee80211_node *ic_bss = nullptr;
};
struct iwm_node { ieee80211_node in_ni; unsigned in_id = 1, in_color = 2; uint8_t in_macaddr[6] = {2,3,4,5,6,7}; };
struct iwx_node { ieee80211_node in_ni; unsigned in_id = 1, in_color = 2; uint8_t in_macaddr[6] = {2,3,4,5,6,7}; };
struct Softc {
    ieee80211com sc_ic;
    uint32_t sc_flags = 0;
    int sc_generation = 7;
    uint32_t agg_queue_mask = 0x3000, agg_tid_disable = 0xfeed;
    uint8_t sc_ucode_api[128] = {}, sc_enabled_capa[128] = {};
    int first_data_qid = 4, sc_rx_ba_sessions = 2;
    struct { int start_tidmask = 1, stop_tidmask = 2; } ba_rx, ba_tx;
};
struct iwm_softc : Softc {};
struct iwx_softc : Softc {};
static bool iwm_mimo_enabled(iwm_softc *) { return true; }
static bool iwx_mimo_enabled(iwx_softc *) { return true; }

enum Edge { NoEdge = 0, Add, DrainOn, Flush, DrainOff, DisableQueue, Remove, Delba };
static std::vector<int> edges;
static int failEdge, resetEdge, statusEdge, transportError;
static uint32_t replacementFlags;
static unsigned cases;
static Softc *baDevice;
static constexpr uint32_t unrelatedFlag = 0x40000000;
static int completion(Softc *sc, int edge)
{
    edges.push_back(edge);
    if (resetEdge == edge) {
        ++sc->sc_generation;
        sc->sc_flags = replacementFlags;
        sc->agg_queue_mask = 0xface;
        sc->agg_tid_disable = 0xbeef;
    }
    return failEdge == edge ? transportError : 0;
}
static void cleanFixture()
{
    edges.clear(); failEdge = resetEdge = statusEdge = 0;
    transportError = ETIMEDOUT; replacementFlags = unrelatedFlag;
    baDevice = nullptr;
}
static void ieee80211_delba_request(ieee80211com *, ieee80211_node *, int, int, int)
{ assert(baDevice); (void)completion(baDevice, Delba); }
class ItlIwm {
public:
    int iwm_add_sta_cmd(iwm_softc *, iwm_node *, int, unsigned);
    int iwm_drain_sta(iwm_softc *, iwm_node *, bool);
    int iwm_rm_sta_cmd(iwm_softc *, iwm_node *);
    int iwm_send_cmd_pdu_status(iwm_softc *sc, int id, size_t size,
                                const void *bytes, uint32_t *status) {
        assert(id == IWM_ADD_STA);
        assert(size == sizeof(struct iwm_add_sta_cmd) || size == sizeof(iwm_add_sta_cmd_v7));
        const auto *cmd = static_cast<const struct iwm_add_sta_cmd *>(bytes);
        const bool drain = cmd->station_flags_msk == cpu_to_le32(IWM_STA_FLG_DRAIN_FLOW);
        const int edge = drain ? (cmd->station_flags ? DrainOn : DrainOff) : Add;
        assert(cmd->mac_id_n_color == htole32(IWM_FW_CMD_ID_AND_COLOR(1, 2)));
        *status = statusEdge == edge ? 0 : IWM_ADD_STA_SUCCESS;
        return completion(sc, edge);
    }
    int iwm_send_cmd_pdu(iwm_softc *sc, int id, int flags, size_t size, const void *bytes) {
        assert(id == IWM_REMOVE_STA && flags == 0 && size == sizeof(struct iwm_rm_sta_cmd));
        const auto *cmd = static_cast<const struct iwm_rm_sta_cmd *>(bytes);
        assert(cmd->sta_id == (sc->sc_ic.ic_opmode == IEEE80211_M_MONITOR ? IWM_MONITOR_STA_ID : IWM_STATION_ID));
        assert(cmd->reserved[0] == 0 && cmd->reserved[1] == 0 && cmd->reserved[2] == 0);
        return completion(sc, Remove);
    }
    int iwm_flush_tx_path(iwm_softc *sc, uint32_t mask) {
        assert(mask == sc->agg_queue_mask); return completion(sc, Flush);
    }
    void iwm_disable_txq(iwm_softc *sc, int qid, int, int) {
        assert(sc->agg_queue_mask & (1U << qid)); (void)completion(sc, DisableQueue);
    }
};
class ItlIwx {
public:
    int iwx_add_sta_cmd(iwx_softc *, iwx_node *, int);
    int iwx_rm_sta_cmd(iwx_softc *, iwx_node *);
    int iwx_drain_sta(iwx_softc *, iwx_node *, int);
    int iwx_flush_sta(iwx_softc *, iwx_node *);
    int iwx_rm_sta(iwx_softc *, iwx_node *);
    int iwx_send_cmd_pdu_status(iwx_softc *sc, int id, size_t size,
                                const void *bytes, uint32_t *status) {
        assert(id == IWX_ADD_STA && size == sizeof(struct iwx_add_sta_cmd));
        const auto *cmd = static_cast<const struct iwx_add_sta_cmd *>(bytes);
        assert(cmd->mac_id_n_color == htole32(IWX_FW_CMD_ID_AND_COLOR(1, 2)));
        const bool drain = cmd->station_flags_msk == htole32(IWX_STA_FLG_DRAIN_FLOW);
        const int edge = drain ? (cmd->station_flags ? DrainOn : DrainOff) : Add;
        *status = statusEdge == edge ? 0 : IWX_ADD_STA_SUCCESS;
        return completion(sc, edge);
    }
    int iwx_send_cmd_pdu(iwx_softc *sc, int id, int flags, size_t size, const void *bytes) {
        assert(id == IWX_REMOVE_STA && flags == 0 && size == sizeof(struct iwx_rm_sta_cmd));
        const auto *cmd = static_cast<const struct iwx_rm_sta_cmd *>(bytes);
        assert(cmd->sta_id == (sc->sc_ic.ic_opmode == IEEE80211_M_MONITOR ? IWX_MONITOR_STA_ID : IWX_STATION_ID));
        assert(cmd->reserved[0] == 0 && cmd->reserved[1] == 0 && cmd->reserved[2] == 0);
        return completion(sc, Remove);
    }
    int iwx_flush_sta_tids(iwx_softc *sc, int id, int tids) {
        assert(id == IWX_STATION_ID && tids == 0xffff);
        assert(sc->sc_flags & IWX_FLAG_TXFLUSH);
        return completion(sc, Flush);
    }
    int iwx_disable_txq(iwx_softc *sc, int id, int qid, int tid) {
        assert(id == IWX_STATION_ID && qid == sc->first_data_qid && tid == IWX_MGMT_TID);
        assert(sc->sc_flags & IWX_FLAG_TXFLUSH);
        return completion(sc, DisableQueue);
    }
};
#include "sta-commands.inc"

template<class Driver, class Device, class Node>
static void testFamily(Driver &driver, int active, bool iwm, const char *selected)
{
    auto add = [&](Device &sc, Node &node, int update) {
        if constexpr (std::is_same<Driver, ItlIwm>::value)
            return driver.iwm_add_sta_cmd(&sc, &node, update, 0);
        else return driver.iwx_add_sta_cmd(&sc, &node, update);
    };
    auto remove = [&](Device &sc, Node &node) {
        if constexpr (std::is_same<Driver, ItlIwm>::value)
            return driver.iwm_rm_sta_cmd(&sc, &node);
        else return driver.iwx_rm_sta_cmd(&sc, &node);
    };
    if (!std::strcmp(selected, "all") || !std::strcmp(selected, "add")) {
        for (int mode : {IEEE80211_M_STA, IEEE80211_M_MONITOR})
        for (bool update : {false, true})
        for (unsigned phy : {0U, 1U, 2U, 4U})
        for (int failure : {0, 1, 2}) {
            cleanFixture(); Device sc; Node node; sc.sc_ic.ic_bss = &node.in_ni;
            sc.sc_ic.ic_opmode = mode; node.in_ni.ni_flags = phy;
            sc.sc_flags = unrelatedFlag | (update ? active : 0);
            setbit(sc.sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE);
            if (failure == 1) failEdge = Add;
            if (failure == 2) statusEdge = Add;
            assert(add(sc, node, update) == (failure == 0 ? 0 : failure == 1 ? ETIMEDOUT : EIO));
            assert(sc.sc_flags == (unrelatedFlag | ((update || !failure) ? active : 0)));
            if (failure) { failEdge = statusEdge = 0; assert(add(sc, node, update) == 0); }
            assert(sc.sc_flags == (unrelatedFlag | active));
            ++cases;
        }
        for (uint32_t successor : {unrelatedFlag, unrelatedFlag | static_cast<uint32_t>(active)}) {
            cleanFixture(); Device sc; Node node; sc.sc_ic.ic_bss = &node.in_ni;
            resetEdge = Add; replacementFlags = successor;
            assert(add(sc, node, 0) == ENXIO);
            assert(sc.sc_flags == successor && sc.agg_queue_mask == 0xface);
            ++cases;
        }
    }
    if (!std::strcmp(selected, "all") || !std::strcmp(selected, "remove")) {
        for (int mode : {IEEE80211_M_STA, IEEE80211_M_MONITOR})
        for (int edge : {DrainOn, Flush, DrainOff, Remove}) {
            if (edge != Remove && (!iwm || mode == IEEE80211_M_MONITOR)) continue;
            cleanFixture(); Device sc; Node node; sc.sc_flags = unrelatedFlag | active;
            sc.sc_ic.ic_opmode = mode; const auto mask = sc.agg_queue_mask;
            failEdge = edge;
            assert(remove(sc, node) == ETIMEDOUT);
            assert(sc.sc_flags == (unrelatedFlag | active));
            assert(sc.agg_queue_mask == mask && sc.agg_tid_disable == 0xfeed);
            assert(edges.back() == edge);
            failEdge = 0; assert(remove(sc, node) == 0);
            assert(sc.sc_flags == unrelatedFlag);
            if (iwm) assert(sc.agg_queue_mask == 0 && sc.agg_tid_disable == 0xffff);
            const auto count = edges.size(); assert(remove(sc, node) == 0);
            assert(edges.size() == count); ++cases;
        }
        for (int edge : {DrainOn, Flush, DrainOff, DisableQueue, Remove}) {
            if (!iwm && edge != Remove) continue;
            for (uint32_t successor : {unrelatedFlag, unrelatedFlag | static_cast<uint32_t>(active)}) {
                cleanFixture(); Device sc; Node node; sc.sc_flags = active;
                resetEdge = edge; replacementFlags = successor;
                assert(remove(sc, node) == ENXIO);
                assert(sc.sc_flags == successor && sc.agg_queue_mask == 0xface && sc.agg_tid_disable == 0xbeef);
                assert(edges.back() == edge); ++cases;
            }
        }
    }
}
int main(int argc, char **argv)
{
    const char *selected = argc > 1 ? argv[1] : "all";
    const char *family = argc > 2 ? argv[2] : "all";
    ItlIwm iwm; ItlIwx iwx;
    if (std::strcmp(family, "iwx"))
        testFamily<ItlIwm, iwm_softc, iwm_node>(iwm, IWM_FLAG_STA_ACTIVE, true, selected);
    if (std::strcmp(family, "iwm"))
        testFamily<ItlIwx, iwx_softc, iwx_node>(iwx, IWX_FLAG_STA_ACTIVE, false, selected);
    if (std::strcmp(family, "iwx") &&
        (!std::strcmp(selected, "all") || !std::strcmp(selected, "drain"))) {
        for (int edge : {DrainOn, DrainOff}) {
            cleanFixture(); iwm_softc sc; iwm_node node;
            sc.sc_flags = IWM_FLAG_STA_ACTIVE; statusEdge = edge;
            assert(iwm.iwm_rm_sta_cmd(&sc, &node) == EIO);
            assert(sc.sc_flags == IWM_FLAG_STA_ACTIVE && sc.agg_queue_mask == 0x3000);
            assert(edges.back() == edge); ++cases;
        }
    }
    if (std::strcmp(family, "iwm") &&
        (!std::strcmp(selected, "all") || !std::strcmp(selected, "flush"))) {
        for (bool inherited : {false, true})
        for (bool outer : {false, true})
        for (int edge : {NoEdge, DrainOn, Flush, DrainOff, DisableQueue, Remove})
        for (bool statusFailure : {false, true}) {
            if (!outer && (edge == DisableQueue || edge == Remove)) continue;
            if (statusFailure && edge != DrainOn && edge != DrainOff) continue;
            cleanFixture(); iwx_softc sc; iwx_node node;
            sc.sc_flags = unrelatedFlag | IWX_FLAG_STA_ACTIVE | (inherited ? IWX_FLAG_TXFLUSH : 0);
            baDevice = &sc; node.in_ni.ni_tx_ba[3].ba_state = IEEE80211_BA_AGREED;
            if (statusFailure) statusEdge = edge; else failEdge = edge;
            int error = outer ? iwx.iwx_rm_sta(&sc, &node) : iwx.iwx_flush_sta(&sc, &node);
            assert(error == (edge == 0 ? 0 : statusFailure ? EIO : ETIMEDOUT));
            if (edge) assert(edges.back() == edge);
            assert((sc.sc_flags & IWX_FLAG_TXFLUSH) == (inherited ? IWX_FLAG_TXFLUSH : 0));
            assert((sc.sc_flags & IWX_FLAG_STA_ACTIVE) == (outer && !edge ? 0 : IWX_FLAG_STA_ACTIVE));
            assert(sc.sc_rx_ba_sessions == (outer && !edge ? 0 : 2));
            ++cases;
        }
        for (bool outer : {false, true})
        for (int edge : {DrainOn, Flush, DrainOff, DisableQueue, Remove, Delba})
        for (bool successorFlush : {false, true}) {
            if (!outer && (edge == DisableQueue || edge == Remove || edge == Delba)) continue;
            cleanFixture(); iwx_softc sc; iwx_node node;
            sc.sc_flags = IWX_FLAG_STA_ACTIVE; baDevice = &sc;
            node.in_ni.ni_tx_ba[3].ba_state = IEEE80211_BA_AGREED;
            resetEdge = edge;
            replacementFlags = unrelatedFlag | IWX_FLAG_STA_ACTIVE | (successorFlush ? IWX_FLAG_TXFLUSH : 0);
            int error = outer ? iwx.iwx_rm_sta(&sc, &node) : iwx.iwx_flush_sta(&sc, &node);
            assert(error == ENXIO && edges.back() == edge);
            assert(sc.sc_flags == replacementFlags && sc.agg_queue_mask == 0xface);
            ++cases;
        }
    }
    assert(cases > 0);
    std::printf("actual IWM/IWX STA add/drain/remove completion: PASS (%u scenarios)\n", cases);
}
