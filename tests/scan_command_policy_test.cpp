#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htole16(x) OSSwapHostToLittleInt16(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#include <endian.h>
#endif
#define __packed __attribute__((packed))
#include <vector>
#include "scan_policy_test_support.hpp"

using bus_addr_t = uint64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using __le16 = uint16_t;
using __le32 = uint32_t;
using __le64 = uint64_t;
using __be16 = uint16_t;
#define __aligned(x) __attribute__((aligned(x)))
#define ETHER_ADDR_LEN 6
#define BIT(x) (1U << (x))
#define __BIT(x) (1U << (x))
#define le32_to_cpup(x) le32toh(*(x))
#define cpu_to_le16(x) htole16(x)
#define IEEE80211_ELEMID_SSID 0
#include "itlwm/hal_iwm/if_iwmreg.h"
#include "itlwm/hal_iwx/if_iwxreg.h"
#include "scan-policy-host-cmd.inc"
#include "scan-policy-defines.inc"

enum class ItlIwmWclScanPhase { Idle, Initial };
enum class ItlIwxWclScanPhase { Idle, Initial };
struct iwm_softc {
    ieee80211com sc_ic;
    uint8_t sc_ucode_api[128] = {};
    uint8_t sc_enabled_capa[128] = {};
    uint8_t sc_capa_n_scan_channels = 8;
    struct { bool sku_cap_band_52GHz_enable = true; } sc_nvm;
};
struct iwx_softc {
    ieee80211com sc_ic;
    uint8_t sc_ucode_api[128] = {};
    uint8_t sc_enabled_capa[128] = {};
    uint8_t sc_capa_n_scan_channels = 8;
};

static std::function<void()> allocationHook, versionHook;
static bool allocationFails;
static int probeError, senderError;
static unsigned submitted, probeCalls, allocations;
static uint8_t scanVersion;
static uint64_t submittedSerial;
static std::vector<uint8_t> commandBytes;

enum { M_DEVBUF = 0, M_NOWAIT = 1, M_WAITOK = 2, M_ZERO = 4 };
static void *commandAllocation(size_t size, int, int flags)
{
    ++allocations;
    if (allocationHook) {
        auto hook = allocationHook;
        allocationHook = nullptr;
        hook();
    }
    if (allocationFails)
        return nullptr;
    return flags & M_ZERO ? std::calloc(1, size) : std::malloc(size);
}
static uint8_t iwx_lookup_cmd_ver(iwx_softc *, unsigned, unsigned)
{
    if (versionHook) {
        auto hook = versionHook;
        versionHook = nullptr;
        hook();
    }
    return scanVersion;
}
template<class Command> static int captureCommand(Command *command)
{
    ++submitted;
    submittedSerial = command->scan_serial;
    const auto *bytes = static_cast<const uint8_t *>(command->data[0]);
    commandBytes.assign(bytes, bytes + command->len[0]);
    return senderError;
}

struct ReservedPolicyFixture {
    ItlScanCommandPolicy policy = {};
    uint64_t serial = 0;
    int reserve(ieee80211com *ic, bool exact, uint64_t value) {
        const int error = capturePolicyAtFixtureReservation(ic, exact, &policy);
        serial = error ? 0 : value;
        return error;
    }
    bool copyScanCommandPolicy(uint64_t value, ItlScanCommandPolicy *out) {
        assert(value == serial && serial != 0);
        *out = policy; // Only the reserved value; no common-policy resampling.
        return true;
    }
};
struct ItlIwm : ReservedPolicyFixture {
    ItlIwmWclScanPhase wclScanPhase = ItlIwmWclScanPhase::Initial;
#ifdef SCAN_POLICY_BUILDERS_NEGATIVE
    int iwm_lmac_scan(iwm_softc *, int);
    int iwm_umac_scan(iwm_softc *, int);
    int iwm_lmac_scan(iwm_softc *sc, int bg, uint64_t) { return iwm_lmac_scan(sc, bg); }
    int iwm_umac_scan(iwm_softc *sc, int bg, uint64_t) { return iwm_umac_scan(sc, bg); }
#else
    int iwm_lmac_scan(iwm_softc *, int, uint64_t);
    int iwm_umac_scan(iwm_softc *, int, uint64_t);
#endif
    int iwm_umac_scan_size(iwm_softc *);
    iwm_scan_umac_chan_param *iwm_get_scan_req_umac_chan_param(iwm_softc *, iwm_scan_req_umac *);
    void *iwm_get_scan_req_umac_data(iwm_softc *, iwm_scan_req_umac *);
#ifdef SCAN_POLICY_BUILDERS_NEGATIVE
    uint8_t iwm_lmac_scan_fill_channels(iwm_softc *, iwm_scan_channel_cfg_lmac *, int, int);
    uint8_t iwm_umac_scan_fill_channels(iwm_softc *, iwm_scan_channel_cfg_umac *, int, int);
#else
    uint8_t iwm_lmac_scan_fill_channels(iwm_softc *, iwm_scan_channel_cfg_lmac *, int, int, const ieee80211_wcl_scan_plan *);
    uint8_t iwm_umac_scan_fill_channels(iwm_softc *, iwm_scan_channel_cfg_umac *, int, int, const ieee80211_wcl_scan_plan *);
#endif
    uint16_t iwm_scan_rx_chain(iwm_softc *) { return 0; }
    uint32_t iwm_scan_rate_n_flags(iwm_softc *, int, int) { return 0; }
    int iwm_fill_probe_req_v1(iwm_softc *, iwm_scan_probe_req_v1 *) { ++probeCalls; return probeError; }
    int iwm_fill_probe_req(iwm_softc *, iwm_scan_probe_req *) { ++probeCalls; return probeError; }
    int iwm_send_cmd(iwm_softc *, iwm_host_cmd *command) { return captureCommand(command); }
};
struct ItlIwx : ReservedPolicyFixture {
    ItlIwxWclScanPhase wclScanPhase = ItlIwxWclScanPhase::Initial;
#ifdef SCAN_POLICY_BUILDERS_NEGATIVE
    int iwx_umac_scan(iwx_softc *, int);
    int iwx_umac_scan(iwx_softc *sc, int bg, uint64_t) { return iwx_umac_scan(sc, bg); }
#else
    int iwx_umac_scan(iwx_softc *, int, uint64_t);
#endif
    int iwx_umac_scan_size(iwx_softc *);
    iwx_scan_umac_chan_param *iwx_get_scan_req_umac_chan_param(iwx_softc *, iwx_scan_req_umac *);
    void *iwx_get_scan_req_umac_data(iwx_softc *, iwx_scan_req_umac *);
#ifdef SCAN_POLICY_BUILDERS_NEGATIVE
    int iwx_umac_scan_v12(iwx_softc *, int);
    int iwx_umac_scan_v14(iwx_softc *, int);
    uint8_t iwx_umac_scan_fill_channels(iwx_softc *, iwx_scan_channel_cfg_umac *, int, int);
#else
    int iwx_umac_scan_v12(iwx_softc *, int, uint64_t, const ItlScanCommandPolicy &);
    int iwx_umac_scan_v14(iwx_softc *, int, uint64_t, const ItlScanCommandPolicy &);
    uint8_t iwx_umac_scan_fill_channels(iwx_softc *, iwx_scan_channel_cfg_umac *, int, int, const ieee80211_wcl_scan_plan *);
#endif
    int iwx_fill_probe_req_v1(iwx_softc *, iwx_scan_probe_req_v1 *) { ++probeCalls; return probeError; }
    int iwx_fill_probe_req(iwx_softc *, iwx_scan_probe_req *) { ++probeCalls; return probeError; }
    int iwx_send_cmd(iwx_softc *, iwx_host_cmd *command) { return captureCommand(command); }
};

#define malloc commandAllocation
#include "scan-policy-builders.inc"
#undef malloc

static void initializeChannels(ieee80211com &ic)
{
    ic.ic_channels[1].ic_freq = 2452;
    ic.ic_channels[1].ic_flags = IEEE80211_CHAN_2GHZ;
    ic.ic_channels[2].ic_freq = 2472;
    ic.ic_channels[2].ic_flags = IEEE80211_CHAN_2GHZ;
    ic.ic_channels[3].ic_freq = 5745;
    ic.ic_channels[3].ic_flags = IEEE80211_CHAN_5GHZ;
    ic.ic_channels[4].ic_freq = 5885;
    ic.ic_channels[4].ic_flags = IEEE80211_CHAN_5GHZ;
    ic.ic_channels[5].ic_freq = 5045;
    ic.ic_channels[5].ic_flags = IEEE80211_CHAN_5GHZ;
    ic.ic_des_esslen = 4;
    std::memcpy(ic.ic_des_essid, "join", 4);
}

struct Decoded {
    uint8_t activeDwell = 0, passiveDwell = 0, ssidLength = 0;
    uint8_t ssid[IEEE80211_NWID_LEN] = {};
    uint32_t maxOut = 0, suspend = 0;
    bool passive = false, preconnect = false;
    std::vector<unsigned> channels, selectors;
};
template<class Ssid> static void decodeSsid(Decoded &out, const Ssid &ssid)
{
    out.ssidLength = ssid.len;
    std::memcpy(out.ssid, ssid.ssid, sizeof(out.ssid));
}
static Decoded decodeIwm(ItlIwm &hal, iwm_softc &sc, bool lmac, unsigned layout, bool extended)
{
    Decoded out;
    if (lmac) {
        const auto *req = reinterpret_cast<const iwm_scan_req_lmac *>(commandBytes.data());
        out.activeDwell = req->active_dwell;
        out.passiveDwell = req->passive_dwell;
        out.maxOut = le32toh(req->max_out_time);
        out.suspend = le32toh(req->suspend_time);
        out.passive = le32toh(req->scan_flags) & IWM_LMAC_SCAN_FLAG_PASSIVE;
        out.preconnect = le32toh(req->scan_flags) & IWM_LMAC_SCAN_FLAG_PRE_CONNECTION;
        decodeSsid(out, req->direct_scan[0]);
        const auto *channels = reinterpret_cast<const iwm_scan_channel_cfg_lmac *>(req->data);
        for (unsigned i = 0; i < req->n_channels; ++i) {
            out.channels.push_back(le16toh(channels[i].channel_num));
            out.selectors.push_back((le32toh(channels[i].flags) >> 1) & 1);
        }
        return out;
    }
    auto *req = reinterpret_cast<iwm_scan_req_umac *>(commandBytes.data());
    out.passive = le32toh(req->general_flags) & IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE;
    out.preconnect = le32toh(req->general_flags) & IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT;
    if (layout == 8) {
        out.activeDwell = req->v8.active_dwell[0];
        out.passiveDwell = req->v8.passive_dwell[0];
        out.maxOut = le32toh(req->v8.max_out_time[0]);
        out.suspend = le32toh(req->v8.suspend_time[0]);
    } else if (layout == 7) {
        out.activeDwell = req->v7.active_dwell;
        out.passiveDwell = req->v7.passive_dwell;
        out.maxOut = le32toh(req->v7.max_out_time[0]);
        out.suspend = le32toh(req->v7.suspend_time[0]);
    } else {
        out.activeDwell = req->v1.active_dwell;
        out.passiveDwell = req->v1.passive_dwell;
        out.maxOut = le32toh(req->v1.max_out_time);
        out.suspend = le32toh(req->v1.suspend_time);
    }
    const auto *channels = static_cast<const iwm_scan_channel_cfg_umac *>(hal.iwm_get_scan_req_umac_data(&sc, req));
    const auto *param = hal.iwm_get_scan_req_umac_chan_param(&sc, req);
    for (unsigned i = 0; i < param->count; ++i) {
        out.channels.push_back(channels[i].channel_num);
        out.selectors.push_back(le32toh(channels[i].flags) & 1);
    }
    const auto *tail = channels + sc.sc_capa_n_scan_channels;
    if (extended)
        decodeSsid(out, reinterpret_cast<const iwm_scan_req_umac_tail_v2 *>(tail)->direct_scan[0]);
    else
        decodeSsid(out, reinterpret_cast<const iwm_scan_req_umac_tail_v1 *>(tail)->direct_scan[0]);
    return out;
}

static Decoded decodeIwx(ItlIwx &hal, iwx_softc &sc, unsigned layout, bool extended)
{
    Decoded out;
    const iwx_scan_channel_cfg_umac *channels;
    unsigned count;
    if (scanVersion == 12 || scanVersion == 14) {
        const iwx_scan_general_params_v10 *general;
        if (scanVersion == 12) {
            const auto *req = reinterpret_cast<const iwx_scan_req_umac_v12 *>(commandBytes.data());
            general = &req->scan_params.general_params;
            count = req->scan_params.channel_params.count;
            channels = reinterpret_cast<const iwx_scan_channel_cfg_umac *>(req->scan_params.channel_params.channel_config);
            decodeSsid(out, req->scan_params.probe_params.direct_scan[0]);
            assert(req->scan_params.probe_params.ssid_num ==
                   ((general->flags & IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE) ? 0 : 1));
        } else {
            const auto *req = reinterpret_cast<const iwx_scan_req_umac_v14 *>(commandBytes.data());
            general = &req->scan_params.general_params;
            count = req->scan_params.channel_params.count;
            channels = reinterpret_cast<const iwx_scan_channel_cfg_umac *>(req->scan_params.channel_params.channel_config);
            decodeSsid(out, req->scan_params.probe_params.direct_scan[0]);
        }
        out.activeDwell = general->active_dwell[0];
        out.passiveDwell = general->passive_dwell[0];
        assert(general->active_dwell[1] == out.activeDwell);
        assert(general->passive_dwell[1] == out.passiveDwell);
        out.maxOut = le32toh(general->max_out_of_time[0]);
        out.suspend = le32toh(general->suspend_time[0]);
        out.passive = general->flags & IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE;
        out.preconnect = !out.passive && out.ssidLength != 0;
    } else {
        auto *req = reinterpret_cast<iwx_scan_req_umac *>(commandBytes.data());
        out.passive = le32toh(req->general_flags) & IWX_UMAC_SCAN_GEN_FLAGS_PASSIVE;
        out.preconnect = le32toh(req->general_flags) & IWX_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT;
        if (layout == 8) {
            out.activeDwell = req->v8.active_dwell[0];
            out.passiveDwell = req->v8.passive_dwell[0];
        } else if (layout == 7) {
            out.activeDwell = req->v7.active_dwell;
            out.passiveDwell = req->v7.passive_dwell;
        } else {
            out.activeDwell = req->v1.active_dwell;
            out.passiveDwell = req->v1.passive_dwell;
        }
        if (layout >= 7) {
            out.maxOut = le32toh(req->v7.max_out_time[0]);
            out.suspend = le32toh(req->v7.suspend_time[0]);
        } else {
            out.maxOut = le32toh(req->v6.max_out_time[0]);
            out.suspend = le32toh(req->v6.suspend_time[0]);
        }
        channels = static_cast<const iwx_scan_channel_cfg_umac *>(hal.iwx_get_scan_req_umac_data(&sc, req));
        count = hal.iwx_get_scan_req_umac_chan_param(&sc, req)->count;
        const auto *tail = channels + sc.sc_capa_n_scan_channels;
        if (extended)
            decodeSsid(out, reinterpret_cast<const iwx_scan_req_umac_tail_v2 *>(tail)->direct_scan[0]);
        else
            decodeSsid(out, reinterpret_cast<const iwx_scan_req_umac_tail_v1 *>(tail)->direct_scan[0]);
    }
    for (unsigned i = 0; i < count; ++i) {
        out.channels.push_back(extended ? channels[i].v2.channel_num : channels[i].v1.channel_num);
        if (extended) {
            const unsigned channel = out.channels.back();
            assert(channels[i].v2.band == (channel == 149 || channel == 177 ? IWX_PHY_BAND_5 :
                (i == 4 ? IWX_PHY_BAND_5 : IWX_PHY_BAND_24)));
        }
        out.selectors.push_back(le32toh(channels[i].flags) & 1);
    }
    return out;
}

static void resetFixture()
{
    allocationHook = versionHook = nullptr;
    allocationFails = false;
    senderError = probeError = 0;
    submitted = submittedSerial = probeCalls = allocations = homeAwayReads = 0;
    configuredHomeAway = 121;
    homeAwayAvailable = true;
    commandBytes.clear();
}

static unsigned runVariant(unsigned variant, unsigned layout, bool extended)
{
    unsigned cases = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (bool background : {false, true}) {
            for (unsigned replacement = 0; replacement < 4; ++replacement) {
                IOSimpleLock lock;
                ItlIwm iwm;
                ItlIwx iwx;
                iwm_softc wm;
                iwx_softc wx;
                ieee80211com &ic = variant < 2 ? wm.sc_ic : wx.sc_ic;
                ic.ic_pae_selected_bss_lock = &lock;
                initializeChannels(ic);
                resetFixture();
                scanVersion = variant == 3 ? 12 : variant == 4 ? 14 : 6;
                auto plan = makePlan(11, 1);
                if (mode == 1)
                    plan.scan_type = IEEE80211_WCL_SCAN_TYPE_PASSIVE;
                if (mode == 2)
                    plan.ssid_len = 0;
                stagePlan(ic, plan);
                if (mode == 3) {
                    iwm.wclScanPhase = ItlIwmWclScanPhase::Idle;
                    iwx.wclScanPhase = ItlIwxWclScanPhase::Idle;
                }
                if (layout >= 7) {
                    setbit(wm.sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL);
                    setbit(wx.sc_ucode_api, IWX_UCODE_TLV_API_ADAPTIVE_DWELL);
                }
                if (layout == 8) {
                    setbit(wm.sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2);
                    setbit(wx.sc_ucode_api, IWX_UCODE_TLV_API_ADAPTIVE_DWELL_V2);
                }
                setbit(wx.sc_enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT);
                if (extended) {
                    setbit(wm.sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER);
                    setbit(wx.sc_ucode_api, IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER);
                }
                auto replace = [&] {
                    auto next = makePlan(12, 2);
                    std::memset(next.channel_2ghz, 0, sizeof(next.channel_2ghz));
                    std::memset(next.channel_5ghz, 0, sizeof(next.channel_5ghz));
                    setbit(next.channel_2ghz, 13);
                    stagePlan(ic, next);
                    ic.ic_des_esslen = 4;
                    std::memcpy(ic.ic_des_essid, "next", 4);
                    configuredHomeAway = 222;
                };
                if (replacement == 1)
                    allocationHook = replace;
                else if (replacement == 2)
                    allocationHook = [&] { ieee80211_wcl_scan_plan_clear(&ic, 0); };
                else if (replacement == 3 && variant >= 2)
                    versionHook = replace;
#ifndef SCAN_POLICY_BUILDERS_NEGATIVE
                auto &owner = variant < 2 ? static_cast<ReservedPolicyFixture &>(iwm) :
                                          static_cast<ReservedPolicyFixture &>(iwx);
                assert(owner.reserve(&ic, mode != 3, 77) == 0);
#endif
                const int error = variant == 0 ? iwm.iwm_lmac_scan(&wm, background, 77) :
                    variant == 1 ? iwm.iwm_umac_scan(&wm, background, 77) :
                                   iwx.iwx_umac_scan(&wx, background, 77);
                assert(error == 0 && submitted == 1);
#ifndef SCAN_POLICY_BUILDERS_NEGATIVE
                assert(submittedSerial == 77);
#endif
                assert(homeAwayReads == 1 && "version dispatch must retain the same policy");
                const auto out = variant < 2 ? decodeIwm(iwm, wm, variant == 0, layout, extended) :
                                              decodeIwx(iwx, wx, layout, extended);
                const std::vector<unsigned> expected = mode == 3 ?
                    std::vector<unsigned>{9, 13, 149, 177, 9} : std::vector<unsigned>{9, 149};
                assert(out.channels == expected && "SSID/dwell and channels must share one captured plan");
                const bool passive = mode == 1 || (mode == 3 && variant >= 2 && background);
                assert(out.passive == passive);
                const bool selected = !passive;
                const unsigned expectedSsid = selected ? (mode == 2 ? 0 : mode == 3 ? 4 : 32) : 0;
                assert(out.ssidLength == expectedSsid);
                if (expectedSsid == 32)
                    for (auto byte : out.ssid)
                        assert(byte == 1);
                if (expectedSsid == 4)
                    assert(std::memcmp(out.ssid, "join", 4) == 0);
                if (out.activeDwell != (mode == 3 ? 10 : 18))
                    std::fprintf(stderr, "variant=%u layout=%u ext=%d mode=%u bg=%d replace=%u active=%u passive=%u\n",
                        variant, layout, extended, mode, background, replacement,
                        out.activeDwell, out.passiveDwell);
                assert(out.activeDwell == (mode == 3 ? 10 : 18));
                assert(out.passiveDwell == (mode == 3 ? 110 : 118));
                assert(out.maxOut == (background ? 121 : 0));
                assert(out.suspend == (background ? (mode == 3 ? 121 : 218) : 0));
                const bool probe = mode == 3 ? !background : !passive;
                for (auto selector : out.selectors)
                    assert(selector == unsigned(probe));
                assert(out.preconnect == (selected && expectedSsid != 0));
                ++cases;
            }
        }
    }
    return cases;
}

static unsigned runFailuresAndDefaults()
{
    unsigned cases = 0;
    for (unsigned variant = 0; variant < 5; ++variant) {
        for (unsigned failure = 0; failure < 10; ++failure) {
            IOSimpleLock lock;
            ItlIwm iwm;
            ItlIwx iwx;
            iwm_softc wm;
            iwx_softc wx;
            auto &ic = variant < 2 ? wm.sc_ic : wx.sc_ic;
            ic.ic_pae_selected_bss_lock = &lock;
            initializeChannels(ic);
            resetFixture();
            scanVersion = variant == 3 ? 12 : variant == 4 ? 14 : 6;
            auto plan = makePlan(31, 1);
            stagePlan(ic, plan);
            int expected = 0;
            switch (failure) {
            case 0:
                ieee80211_wcl_scan_plan_clear(&ic, 0);
                expected = ECANCELED;
                break;
            case 1:
                allocationFails = true;
                expected = ENOMEM;
                break;
            case 2:
                probeError = EIO;
                expected = EIO;
                break;
            case 3:
                senderError = EBUSY;
                expected = EBUSY;
                break;
            case 4:
                std::memset(plan.channel_2ghz, 0, sizeof(plan.channel_2ghz));
                std::memset(plan.channel_5ghz, 0, sizeof(plan.channel_5ghz));
                stagePlan(ic, plan);
                expected = EINVAL;
                break;
            case 5:
                ic.ic_des_esslen = IEEE80211_NWID_LEN + 1;
                iwm.wclScanPhase = ItlIwmWclScanPhase::Idle;
                iwx.wclScanPhase = ItlIwxWclScanPhase::Idle;
                expected = EINVAL;
                break;
            case 6:
                plan.active_dwell_ms = plan.passive_dwell_ms = plan.home_dwell_ms = 0;
                stagePlan(ic, plan);
                homeAwayAvailable = false;
                break;
            case 7:
                plan.active_dwell_ms = plan.passive_dwell_ms = plan.home_dwell_ms = UINT32_MAX;
                stagePlan(ic, plan);
                break;
            case 8:
                wm.sc_capa_n_scan_channels = wx.sc_capa_n_scan_channels = 1;
                break;
            case 9:
                plan.channel_filter = 0;
                stagePlan(ic, plan);
                break;
            }
            auto &owner = variant < 2 ? static_cast<ReservedPolicyFixture &>(iwm) :
                                      static_cast<ReservedPolicyFixture &>(iwx);
            const int reservationError = owner.reserve(&ic, failure != 5, 99);
            const int error = reservationError != 0 ? reservationError :
                variant == 0 ? iwm.iwm_lmac_scan(&wm, 1, 99) :
                variant == 1 ? iwm.iwm_umac_scan(&wm, 1, 99) :
                               iwx.iwx_umac_scan(&wx, 1, 99);
            assert(error == expected);
            if (failure == 0 || failure == 5)
                assert(allocations == 0 && submitted == 0);
            if (failure == 1 || failure == 2 || failure == 4)
                assert(submitted == 0);
            if (failure == 3 || failure >= 6)
                assert(submitted == 1 && submittedSerial == 99);
            if (failure >= 6) {
                const auto out = variant < 2 ? decodeIwm(iwm, wm, variant == 0, 1, false) :
                                              decodeIwx(iwx, wx, 6, false);
                if (failure == 6 || failure == 7) {
                    assert(out.activeDwell == 10 && out.passiveDwell == 110);
                    assert(out.maxOut == (failure == 6 ? 120 : 121));
                    assert(out.suspend == out.maxOut);
                }
                if (failure == 8)
                    assert(out.channels == std::vector<unsigned>{9});
                if (failure == 9)
                    assert((out.channels == std::vector<unsigned>{9, 13, 149, 177, 9}));
            }
            ++cases;
        }
    }
    return cases;
}

int main()
{
    unsigned cases = 0;
    const char *family = std::getenv("SCAN_POLICY_TEST_FAMILY");
    if (!family || std::strcmp(family, "iwm") == 0) {
        cases += runVariant(0, 1, false);
        for (unsigned layout : {1U, 7U, 8U})
            for (bool extended : {false, true})
                cases += runVariant(1, layout, extended);
    }
    if (!family || std::strcmp(family, "iwx") == 0) {
        for (unsigned layout : {6U, 7U, 8U})
            for (bool extended : {false, true})
                cases += runVariant(2, layout, extended);
        cases += runVariant(3, 8, true);
        cases += runVariant(4, 8, true);
    }
    assert(cases != 0);
#ifndef SCAN_POLICY_BUILDERS_NEGATIVE
    cases += runFailuresAndDefaults();
#endif
    std::printf("scan policy: %u complete production firmware-builder cases PASS\n", cases);
}
