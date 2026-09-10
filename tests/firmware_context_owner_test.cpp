/* Complete production MAC common/fill/wrapper and binding wrapper, with
 * real firmware command definitions. Kernel storage/locks, ACK-rate input
 * and transport replies are fixtures; no radio qualification is implied. */
#include "scan_test_byte_order.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sys/types.h>
#include <type_traits>
#include <vector>
using std::min;
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using u64 = uint64_t; using s8 = int8_t; using s16 = int16_t; using s32 = int32_t;
using __le16 = uint16_t; using __le32 = uint32_t; using __le64 = uint64_t;
using __be16 = uint16_t; using bus_addr_t = uint64_t;
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
#define letoh64(x) le64toh(x)
#define isset(a, b) ((a)[(b) / 8] & (1U << ((b) % 8)))
#define setbit(a, b) ((a)[(b) / 8] |= (1U << ((b) % 8)))
#define IEEE80211_ADDR_COPY(a, b) std::memcpy((a), (b), 6)
#define XYLog(...) ((void)0)
#define panic(...) assert(false)
#define DEVNAME(sc) "fixture"
#define KASSERT(condition, message) assert(condition)
#include "context-defines.inc"
#include "itlwm/hal_iwm/if_iwmreg.h"
#include "itlwm/hal_iwx/if_iwxreg.h"
#include <HAL/ItlFirmwareContextLease.hpp>
#include "context-host-commands.inc"
using Lease = ItlFirmwareContextLease;
enum { IEEE80211_M_STA, IEEE80211_M_MONITOR, IEEE80211_M_HOSTAP };
enum { IEEE80211_CHAN_WIDTH_20, IEEE80211_CHAN_WIDTH_40 };
struct IOSimpleLock { unsigned rank; };
using IOInterruptState = unsigned;
static std::vector<unsigned> locks;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && (locks.empty() || locks.back() < lock->rank));
    const unsigned previous = locks.size(); locks.push_back(lock->rank); return previous;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState previous)
{
    assert(lock && !locks.empty() && locks.back() == lock->rank);
    locks.pop_back(); assert(locks.size() == previous);
}
struct ieee80211_channel { bool band2 = true; };
#define IEEE80211_IS_CHAN_2GHZ(c) ((c)->band2)
struct ieee80211_node {
    unsigned ni_flags = IEEE80211_NODE_QOS | IEEE80211_NODE_HT;
    unsigned ni_associd = 23, ni_dtimperiod = 2, ni_dtimcount = 1;
    unsigned ni_intval = 100, ni_rstamp = 1234;
    uint8_t ni_tstamp[8] = {1,2,3,4,5,6,7,8};
    unsigned ni_htop1 = IEEE80211_HTPROT_NONMEMBER;
    unsigned ni_chw = IEEE80211_CHAN_WIDTH_40;
};
struct ieee80211_edca_ac_params { int ac_ecwmin = 3, ac_ecwmax = 4, ac_aifsn = 2, ac_txoplimit = 5; };
struct ieee80211com {
    IOSimpleLock selected{1};
    IOSimpleLock *ic_pae_selected_bss_lock = &selected;
    ieee80211_node *ic_bss = nullptr;
    int ic_opmode = IEEE80211_M_STA;
    unsigned ic_flags = IEEE80211_F_SHPREAMBLE | IEEE80211_F_SHSLOT | IEEE80211_F_USEPROT;
    uint8_t ic_myaddr[6] = {2,3,4,5,6,7};
    ieee80211_edca_ac_params ic_edca_ac[4];
    ItlStateTransitionIdentity identity{11, 11, 12};
};
struct ItlScanCommandPolicy {
    static ItlStateTransitionIdentity identityLocked(const ieee80211com *ic)
    { assert(!locks.empty() && locks.front() == 1); return ic->identity; }
};
struct Phy { unsigned id = 1, color = 3; ieee80211_channel *channel = nullptr; };
struct iwm_phy_ctxt : Phy {};
struct iwx_phy_ctxt : Phy {};
struct Node {
    ieee80211_node in_ni;
    unsigned in_id = 2, in_color = 4;
    uint8_t in_macaddr[6] = {2,8,7,6,5,4};
};
struct iwm_node : Node { iwm_phy_ctxt *in_phyctxt = nullptr; };
struct iwx_node : Node { iwx_phy_ctxt *in_phyctxt = nullptr; };
struct Softc {
    ieee80211com sc_ic;
    int sc_generation = 7;
    uint32_t sc_flags = 0;
    uint8_t sc_enabled_capa[128] = {};
};
struct iwm_softc : Softc {};
struct iwx_softc : Softc {};
static uint8_t etherbroadcastaddr[6] = {255,255,255,255,255,255};
static const uint8_t iwm_ac_to_tx_fifo[] = {IWM_TX_FIFO_BE,IWM_TX_FIFO_BK,IWM_TX_FIFO_VI,IWM_TX_FIFO_VO};
static const uint8_t iwx_ac_to_tx_fifo[] = {IWX_TX_FIFO_BE,IWX_TX_FIFO_BK,IWX_TX_FIFO_VI,IWX_TX_FIFO_VO};
static uint8_t iwm_mvm_mac80211_ac_to_ucode_ac(ieee80211_edca_ac ac) { return static_cast<uint8_t>(ac); }
static uint8_t iwx_mvm_mac80211_ac_to_ucode_ac(ieee80211_edca_ac ac) { return static_cast<uint8_t>(ac); }
static std::vector<uint8_t> sent;
static std::function<void()> callback;
static std::function<void()> beforeSubmission;
static int transportError, firmwareStatus;
static unsigned replyAllocations, replyShape;
static unsigned calls, cases;
static int transmit(size_t length, const void *bytes)
{
    assert(locks.empty()); ++calls;
    const auto *begin = static_cast<const uint8_t *>(bytes);
    sent.assign(begin, begin + length);
    auto completion = callback; callback = nullptr;
    if (completion) completion();
    return transportError;
}
struct DriverState {
    IOSimpleLock leaf{2};
    IOSimpleLock *wclScanLock = &leaf;
    Lease primaryMacContext{}, primaryBindingContext{};
    struct { bool open = true; } scanCommand;
};
template<class Driver, class Device, class Command>
static int submitContext(Driver &driver, Device *sc, Command *hcmd)
{
    assert(locks.empty() && hcmd->context_command);
    auto before = beforeSubmission; beforeSubmission = nullptr;
    if (before) before();
    auto &context = *hcmd->context_command;
    IOInterruptState selectedIrq = 0;
    if (!context.cleanup)
        selectedIrq = IOSimpleLockLockDisableInterrupt(sc->sc_ic.ic_pae_selected_bss_lock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(driver.wclScanLock);
    const bool admitted = driver.firmwareContextCommandCurrentLocked(context);
    if (admitted) context.submitted = true;
    IOSimpleLockUnlockEnableInterrupt(driver.wclScanLock,irq);
    if (!context.cleanup)
        IOSimpleLockUnlockEnableInterrupt(sc->sc_ic.ic_pae_selected_bss_lock,selectedIrq);
    if (!admitted) return ENXIO;
    const int result = transmit(hcmd->len[0],hcmd->data[0]);
    if (result == 0 && hcmd->flags != 0 && replyShape != 1) {
        using Packet = typename std::remove_pointer<decltype(hcmd->resp_pkt)>::type;
        auto *packet = static_cast<Packet *>(std::calloc(1,sizeof(Packet) + sizeof(uint32_t)));
        assert(packet); ++replyAllocations; hcmd->resp_pkt = packet;
        packet->len_n_flags = htole32(sizeof(packet->hdr) + (replyShape == 2 ? 0 : sizeof(uint32_t)));
        const uint32_t status = htole32(firmwareStatus);
        std::memcpy(packet->data,&status,sizeof(status));
        if (replyShape == 3) {
            if constexpr (std::is_same<Device,iwm_softc>::value)
                packet->hdr.flags |= IWM_CMD_FAILED_MSK;
            else
                packet->hdr.group_id |= IWX_CMD_FAILED_MSK;
        }
    }
    return result;
}
template<class Command>
static void releaseReply(Command *command)
{
    assert(locks.empty());
    if (command->resp_pkt) {
        assert(replyAllocations); --replyAllocations;
        std::free(command->resp_pkt); command->resp_pkt = nullptr;
    }
}
class ItlIwm : public DriverState {
public:
    iwm_softc com;
    struct iwm_mac_ctx_cmd primaryMacCommand{};
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &) const;
    int iwm_mac_ctxt_cmd(iwm_softc *, iwm_node *, uint32_t, int);
    int iwm_binding_cmd(iwm_softc *, iwm_node *, uint32_t);
    void iwm_mac_ctxt_cmd_common(iwm_softc *, iwm_node *, struct iwm_mac_ctx_cmd *, uint32_t);
    void iwm_mac_ctxt_cmd_fill_sta(iwm_softc *, iwm_node *, iwm_mac_data_sta *, int);
    void iwm_ack_rates(iwm_softc *, iwm_node *, int *cck, int *ofdm) { *cck = 3; *ofdm = 21; }
    int iwm_send_cmd(iwm_softc *sc, iwm_host_cmd *cmd) { return submitContext(*this,sc,cmd); }
    int iwm_send_cmd_status(iwm_softc *, iwm_host_cmd *, uint32_t *);
    void iwm_free_resp(iwm_softc *, iwm_host_cmd *cmd) { releaseReply(cmd); }
    int iwm_send_cmd_pdu(iwm_softc *, int id, int flags, size_t len, const void *bytes)
    { assert(id == IWM_MAC_CONTEXT_CMD && flags == 0 && len == sizeof(struct iwm_mac_ctx_cmd)); return transmit(len, bytes); }
    int iwm_send_cmd_pdu_status(iwm_softc *, int id, size_t len, const void *bytes, uint32_t *status)
    { assert(id == IWM_BINDING_CONTEXT_CMD); *status = firmwareStatus; return transmit(len, bytes); }
};
class ItlIwx : public DriverState {
public:
    iwx_softc com;
    struct iwx_mac_ctx_cmd primaryMacCommand{};
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &) const;
    int iwx_mac_ctxt_cmd(iwx_softc *, iwx_node *, uint32_t, int);
    int iwx_binding_cmd(iwx_softc *, iwx_node *, uint32_t);
    void iwx_mac_ctxt_cmd_common(iwx_softc *, iwx_node *, struct iwx_mac_ctx_cmd *, uint32_t);
    void iwx_mac_ctxt_cmd_fill_sta(iwx_softc *, iwx_node *, iwx_mac_data_sta *, int);
    void iwx_ack_rates(iwx_softc *, iwx_node *, int *cck, int *ofdm) { *cck = 3; *ofdm = 21; }
    int iwx_send_cmd(iwx_softc *sc, iwx_host_cmd *cmd) { return submitContext(*this,sc,cmd); }
    int iwx_send_cmd_status(iwx_softc *, iwx_host_cmd *, uint32_t *);
    void iwx_free_resp(iwx_softc *, iwx_host_cmd *cmd) { releaseReply(cmd); }
    int iwx_send_cmd_pdu(iwx_softc *, int id, int flags, size_t len, const void *bytes)
    { assert(id == IWX_MAC_CONTEXT_CMD && flags == 0 && len == sizeof(struct iwx_mac_ctx_cmd)); return transmit(len, bytes); }
    int iwx_send_cmd_pdu_status(iwx_softc *, int id, size_t len, const void *bytes, uint32_t *status)
    { assert(id == IWX_BINDING_CONTEXT_CMD); *status = firmwareStatus; return transmit(len, bytes); }
};
#include "context-methods.inc"

static void clean()
{ assert(locks.empty() && replyAllocations == 0); sent.clear(); callback = beforeSubmission = nullptr; transportError = firmwareStatus = 0; calls = replyShape = 0; }

template<class Driver, class Device, class Peer, class Physical>
static void testFamily(const char *selected)
{
    constexpr bool iwm = std::is_same<Driver, ItlIwm>::value;
    const uint32_t add = iwm ? IWM_FW_CTXT_ACTION_ADD : IWX_FW_CTXT_ACTION_ADD;
    const uint32_t modify = iwm ? IWM_FW_CTXT_ACTION_MODIFY : IWX_FW_CTXT_ACTION_MODIFY;
    const uint32_t remove = iwm ? IWM_FW_CTXT_ACTION_REMOVE : IWX_FW_CTXT_ACTION_REMOVE;
    const uint32_t macFlag = iwm ? IWM_FLAG_MAC_ACTIVE : IWX_FLAG_MAC_ACTIVE;
    const uint32_t bindingFlag = iwm ? IWM_FLAG_BINDING_ACTIVE : IWX_FLAG_BINDING_ACTIVE;
    const uint32_t staFlag = iwm ? IWM_FLAG_STA_ACTIVE : IWX_FLAG_STA_ACTIVE;
    const uint32_t teFlag = iwm ? IWM_FLAG_TE_ACTIVE : IWX_FLAG_TE_ACTIVE;
    const uint32_t unrelated = 0x40000000;
    auto mac = [&](Driver &d, Device &s, Peer *n, uint32_t action, int assoc = 0) {
        if constexpr (iwm) return d.iwm_mac_ctxt_cmd(&s,n,action,assoc);
        else return d.iwx_mac_ctxt_cmd(&s,n,action,assoc);
    };
    auto binding = [&](Driver &d, Device &s, Peer *n, uint32_t action) {
        if constexpr (iwm) return d.iwm_binding_cmd(&s,n,action);
        else return d.iwx_binding_cmd(&s,n,action);
    };
    auto setup = [&](Driver &d, Device &s, Peer &n, Physical &p, ieee80211_channel &c) {
        clean(); p.channel = &c; n.in_phyctxt = &p; s.sc_ic.ic_bss = &n.in_ni;
        s.sc_flags = unrelated;
        setbit(s.sc_enabled_capa, iwm ? IWM_UCODE_TLV_CAPA_CDB_SUPPORT : IWX_UCODE_TLV_CAPA_CDB_SUPPORT);
        assert(mac(d,s,&n,add) == 0);
    };
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"status_reply")) {
        for (unsigned shape : {0U,1U,2U,3U}) {
            Driver d; Device &s = d.com; clean(); replyShape = shape;
            ItlFirmwareContextReceipt receipt{};
            receipt.serial = 1; receipt.generation = s.sc_generation;
            receipt.identity.attempt = s.sc_ic.identity;
            d.primaryBindingContext.owner = receipt;
            d.primaryBindingContext.stage = Lease::Stage::Adding;
            ItlFirmwareContextCommand context{receipt,ItlFirmwareContextCommand::Kind::Binding,false,false};
            using HostCommand = typename std::conditional<iwm,iwm_host_cmd,iwx_host_cmd>::type;
            HostCommand command{};
            command.context_command = &context;
            command.id = iwm ? IWM_BINDING_CONTEXT_CMD : IWX_BINDING_CONTEXT_CMD;
            uint32_t payload = 0, status = 0xbeef;
            command.len[0] = sizeof(payload); command.data[0] = &payload;
            firmwareStatus = 0x1324;
            int result;
            if constexpr (iwm) result = d.iwm_send_cmd_status(&s,&command,&status);
            else result = d.iwx_send_cmd_status(&s,&command,&status);
            assert(result == (shape == 0 ? 0 : EIO));
            assert(replyAllocations == 0 && command.resp_pkt == nullptr);
            assert(context.submitted && status == (shape == 0 ? 0x1324U : 0xbeefU));
            ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"submission")) {
        for (bool bind : {false,true}) for (uint32_t action : {add,modify,remove})
        for (unsigned failure = 0; failure != 3; ++failure) {
            if (bind && action == modify) continue;
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            if (!bind && action == add) assert(mac(d,s,nullptr,remove) == 0);
            if (bind && action == remove) assert(binding(d,s,&n,add) == 0);
            const unsigned previousCalls = calls;
            const auto previousCommand = d.primaryMacCommand;
            beforeSubmission = [&] {
                if (failure == 0) ++s.sc_ic.identity.associationEpoch;
                if (failure == 1) d.scanCommand.open = false;
                if (failure == 2) {
                    ++s.sc_generation; d.primaryMacContext.clear(); d.primaryBindingContext.clear();
                    s.sc_flags = unrelated;
                }
            };
            const bool allowed = failure == 0 && action == remove;
            assert((bind ? binding(d,s,&n,action) : mac(d,s,&n,action,1)) == (allowed ? 0 : ENXIO));
            assert(calls == previousCalls + (allowed ? 1U : 0U));
            const auto &context = bind ? d.primaryBindingContext : d.primaryMacContext;
            if (failure == 2 || action == add || allowed)
                assert(!context.occupied());
            else
                assert(context.stage == Lease::Stage::Active && !context.uncertain);
            if (!bind && action == modify && failure != 2)
                assert(!std::memcmp(&previousCommand,&d.primaryMacCommand,sizeof(previousCommand)));
            ++cases;
        }
        { // Cleanup permission cannot authorize an ADD or reopen a closed device.
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            auto &owner = d.primaryMacContext;
            owner.stage = Lease::Stage::Adding;
            ItlFirmwareContextCommand command{owner.owner,ItlFirmwareContextCommand::Kind::Mac,true,false};
            auto selectedIrq = IOSimpleLockLockDisableInterrupt(s.sc_ic.ic_pae_selected_bss_lock);
            auto irq = IOSimpleLockLockDisableInterrupt(d.wclScanLock);
            assert(!d.firmwareContextCommandCurrentLocked(command));
            command.cleanup = false;
            assert(d.firmwareContextCommandCurrentLocked(command));
            ++command.receipt.identity.peer[0];
            assert(!d.firmwareContextCommandCurrentLocked(command));
            command.receipt = owner.owner; command.submitted = true;
            assert(!d.firmwareContextCommandCurrentLocked(command));
            command.submitted = false; command.kind = static_cast<ItlFirmwareContextCommand::Kind>(255);
            assert(!d.firmwareContextCommandCurrentLocked(command));
            IOSimpleLockUnlockEnableInterrupt(d.wclScanLock,irq);
            IOSimpleLockUnlockEnableInterrupt(s.sc_ic.ic_pae_selected_bss_lock,selectedIrq);
            ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"reply")) {
        for (unsigned shape : {1U,2U,3U}) {
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c); replyShape = shape;
            assert(binding(d,s,&n,add) == EIO);
            assert(replyAllocations == 0);
            assert(d.primaryBindingContext.stage == Lease::Stage::Uncertain);
            assert(binding(d,s,nullptr,remove) == EIO && replyAllocations == 0);
            replyShape = 0;
            assert(binding(d,s,nullptr,remove) == 0 && replyAllocations == 0);
            ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"replacement") ||
        !std::strncmp(selected,"replace_",8)) {
        for (bool bind : {false,true}) for (bool deleting : {false,true}) {
            const char *caseName = bind ? (deleting ? "replace_binding_remove" : "replace_binding_add") :
                (deleting ? "replace_mac_remove" : "replace_mac_add");
            if (!std::strncmp(selected,"replace_",8) && std::strcmp(selected,caseName))
                continue;
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            /* Legacy AUTH set these flags outside the wrapper. Preserve that
             * old caller boundary so unchanged HEAD reaches the actual defect,
             * not an assertion about the newly introduced lease fields. */
            s.sc_flags |= macFlag;
            if (bind) { assert(binding(d,s,&n,add) == 0); s.sc_flags |= bindingFlag; }
            ++n.in_id; ++n.in_color; ++n.in_macaddr[5]; ++p.id; ++p.color;
            if (!deleting) {
                assert((bind ? binding(d,s,&n,add) : mac(d,s,&n,add)) == EBUSY);
            } else {
                assert((bind ? binding(d,s,&n,remove) : mac(d,s,&n,remove)) == 0);
                uint32_t wireId = 0; std::memcpy(&wireId,sent.data(),sizeof(wireId));
                assert(le32toh(wireId) == (bind ? (1U | (3U << 8)) : (2U | (4U << 8))));
            }
            ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"mac")) {
        for (int change = 0; change != 7; ++change) {
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            assert(d.primaryMacContext.stage == Lease::Stage::Active);
            assert(s.sc_flags == (unrelated | macFlag));
            assert(mac(d,s,&n,add) == 0 && calls == 1);
            const auto old = d.primaryMacContext.owner;
            if (change == 0) ++n.in_id;
            if (change == 1) ++n.in_color;
            if (change == 2) ++n.in_macaddr[5];
            if (change == 3) ++s.sc_ic.identity.associationEpoch;
            if (change == 4) ++s.sc_ic.identity.joinSequence;
            if (change == 5) ++s.sc_ic.identity.joinGeneration;
            if (change == 6) s.sc_ic.ic_opmode = IEEE80211_M_MONITOR;
            assert(mac(d,s,&n,add) == EBUSY && calls == 1);
            assert(d.primaryMacContext.owner.serial == old.serial);
            s.sc_ic.ic_bss = nullptr; s.sc_ic.ic_opmode = IEEE80211_M_HOSTAP;
            assert(mac(d,s,nullptr,remove) == 0 && calls == 2);
            assert(!d.primaryMacContext.occupied() && s.sc_flags == unrelated);
            decltype(d.primaryMacCommand) expected{};
            expected.id_and_color = htole32(old.identity.mac); expected.action = htole32(remove);
            assert(sent.size() == sizeof(expected) && !std::memcmp(sent.data(),&expected,sizeof(expected)));
            assert(mac(d,s,nullptr,remove) == 0 && calls == 2); ++cases;
        }
        for (bool uncertainAdd : {false,true}) {
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            clean(); p.channel = &c; n.in_phyctxt = &p; s.sc_ic.ic_bss = &n.in_ni;
            s.sc_flags = unrelated;
            transportError = uncertainAdd ? ETIMEDOUT : 0;
            callback = [&] { ++n.in_id; ++n.in_macaddr[5]; ++s.sc_ic.identity.associationEpoch; };
            assert(mac(d,s,&n,add) == transportError);
            const auto old = d.primaryMacContext.owner;
            assert(old.identity.mac == (2U | (4U << 8)) && old.identity.peer[5] == 4);
            assert(mac(d,s,&n,add) == EBUSY);
            transportError = EIO;
            assert(mac(d,s,nullptr,remove) == EIO);
            assert(d.primaryMacContext.occupied() && d.primaryMacContext.stage == Lease::Stage::Uncertain);
            assert(s.sc_flags == (unrelated | (uncertainAdd ? 0 : macFlag)));
            transportError = 0; assert(mac(d,s,nullptr,remove) == 0);
            assert(!d.primaryMacContext.occupied() && s.sc_flags == unrelated); ++cases;
        }
        { // Disassociation must retain the old wire fields after BSS replacement.
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c); assert(mac(d,s,&n,modify,1) == 0);
            auto expected = d.primaryMacCommand;
            expected.action = htole32(modify); expected.sta.is_assoc = 0;
            expected.filter_flags |= htole32(iwm ? IWM_MAC_FILTER_IN_BEACON : IWX_MAC_FILTER_IN_BEACON);
            s.sc_ic.ic_bss = nullptr; s.sc_ic.ic_opmode = IEEE80211_M_HOSTAP;
            ++s.sc_ic.identity.associationEpoch;
            assert(mac(d,s,nullptr,modify,0) == 0);
            assert(!std::memcmp(sent.data(),&expected,sizeof(expected))); ++cases;
        }
        { // Production common helper must use the supplied node, not ic_bss.
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            ieee80211_node other; other.ni_flags = 0;
            clean(); s.sc_ic.ic_bss = &other; n.in_phyctxt = &p; p.channel = &c;
            assert(mac(d,s,&n,add) == 0);
            assert(le32toh(d.primaryMacCommand.qos_flags) & (iwm ? IWM_MAC_QOS_FLG_TGN : IWX_MAC_QOS_FLG_TGN)); ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"binding")) {
        for (bool band2 : {false,true}) for (bool cdb : {false,true}) for (int failure : {0,1,2}) {
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            c.band2 = band2; setup(d,s,n,p,c);
            if (cdb) setbit(s.sc_enabled_capa, iwm ? IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT : IWX_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT);
            transportError = failure == 1 ? ETIMEDOUT : 0; firmwareStatus = failure == 2 ? 1 : 0;
            callback = [&] { ++p.id; ++p.color; c.band2 = !band2; ++n.in_id; ++n.in_macaddr[5]; };
            assert(binding(d,s,&n,add) == (failure == 1 ? ETIMEDOUT : failure == 2 ? EIO : 0));
            assert(s.sc_flags == (unrelated | macFlag | (failure == 0 ? bindingFlag : 0)));
            if (failure == 2) {
                assert(!d.primaryBindingContext.occupied());
                transportError = firmwareStatus = 0; assert(mac(d,s,nullptr,remove) == 0); ++cases; continue;
            }
            assert(d.primaryBindingContext.owner.identity.phy == (1U | (3U << 8)));
            assert(mac(d,s,nullptr,remove) == EBUSY);
            transportError = 0; firmwareStatus = 1;
            assert(binding(d,s,nullptr,remove) == EIO);
            assert(d.primaryBindingContext.occupied());
            assert(d.primaryBindingContext.stage == (failure ? Lease::Stage::Uncertain : Lease::Stage::Active));
            firmwareStatus = 0; s.sc_ic.ic_bss = nullptr; n.in_phyctxt = nullptr;
            assert(binding(d,s,nullptr,remove) == 0);
            assert(!d.primaryBindingContext.occupied() && s.sc_flags == (unrelated | macFlag));
            if constexpr (iwm) {
                struct iwm_binding_cmd expected{};
                expected.id_and_color = expected.phy = htole32(1U | (3U << 8)); expected.action = htole32(remove);
                expected.lmac_id = htole32(band2 ? IWM_LMAC_24G_INDEX : IWM_LMAC_5G_INDEX);
                for (auto &id : expected.macs) id = htole32(IWM_FW_CTXT_INVALID);
                assert(sent.size() == (cdb ? sizeof(expected) : sizeof(iwm_binding_cmd_v1)));
                assert(!std::memcmp(sent.data(),&expected,sent.size()));
            } else {
                struct iwx_binding_cmd expected{};
                expected.id_and_color = expected.phy = htole32(1U | (3U << 8)); expected.action = htole32(remove);
                expected.lmac_id = htole32(band2 ? IWX_LMAC_24G_INDEX : IWX_LMAC_5G_INDEX);
                for (auto &id : expected.macs) id = htole32(IWX_FW_CTXT_INVALID);
                assert(sent.size() == sizeof(expected) && !std::memcmp(sent.data(),&expected,sizeof(expected)));
            }
            assert(mac(d,s,nullptr,remove) == 0); ++cases;
        }
    }
    if (!std::strcmp(selected,"all") || !std::strcmp(selected,"lifecycle")) {
        for (bool bind : {false,true}) for (uint32_t action : {add,modify,remove}) {
            if (bind && action == modify) continue;
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            if (bind && action == remove) assert(binding(d,s,&n,add) == 0);
            if (!bind && action == add) { assert(mac(d,s,nullptr,remove) == 0); }
            callback = [&] {
                ++s.sc_generation; d.primaryMacContext.clear(); d.primaryBindingContext.clear();
                s.sc_flags = unrelated | staFlag;
            };
            assert((bind ? binding(d,s,&n,action) : mac(d,s,&n,action,1)) == ENXIO);
            assert(!d.primaryMacContext.occupied() && !d.primaryBindingContext.occupied());
            assert(s.sc_flags == (unrelated | staFlag)); ++cases;
        }
        { // A command already in flight excludes reentrant create/remove.
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            callback = [&] {
                assert(binding(d,s,&n,add) == EBUSY);
                assert(binding(d,s,nullptr,remove) == EBUSY);
                assert(mac(d,s,nullptr,remove) == EBUSY);
            };
            assert(binding(d,s,&n,add) == 0);
            s.sc_flags |= staFlag;
            assert(binding(d,s,nullptr,remove) == EBUSY);
            assert(mac(d,s,nullptr,remove) == EBUSY);
            s.sc_flags &= ~staFlag; assert(binding(d,s,nullptr,remove) == 0);
            s.sc_flags |= teFlag; assert(mac(d,s,nullptr,remove) == EBUSY);
            s.sc_flags &= ~teFlag; assert(mac(d,s,nullptr,remove) == 0); ++cases;
        }
        for (bool bind : {false,true}) {
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c);
            if (bind) assert(binding(d,s,&n,add) == 0);
            callback = [&] {
                ++s.sc_generation;
                d.primaryMacContext.clear(); d.primaryBindingContext.clear();
                s.sc_flags = unrelated; ++n.in_id; ++p.id;
                assert(mac(d,s,&n,add) == 0);
                if (bind) assert(binding(d,s,&n,add) == 0);
            };
            assert((bind ? binding(d,s,&n,remove) : mac(d,s,&n,remove)) == ENXIO);
            assert(d.primaryMacContext.stage == Lease::Stage::Active);
            assert(d.primaryMacContext.owner.generation == static_cast<uint32_t>(s.sc_generation));
            assert(d.primaryMacContext.owner.identity.mac == (3U | (4U << 8)));
            assert(s.sc_flags == (unrelated | macFlag | (bind ? bindingFlag : 0))); ++cases;
        }
        { // A failed MODIFY cannot become retryable ADD after failed removal.
            Driver d; Device &s = d.com; Peer n; Physical p; ieee80211_channel c;
            setup(d,s,n,p,c); transportError = ETIMEDOUT;
            assert(mac(d,s,&n,modify,1) == ETIMEDOUT);
            const auto retained = d.primaryMacCommand;
            assert(mac(d,s,nullptr,remove) == ETIMEDOUT);
            assert(!std::memcmp(&retained,&d.primaryMacCommand,sizeof(retained)));
            assert(mac(d,s,&n,add) == EBUSY);
            transportError = 0; assert(mac(d,s,nullptr,remove) == 0); ++cases;
        }
    }
}

int main(int argc, char **argv)
{
    const char *selected = argc > 1 ? argv[1] : "all";
    const char *family = argc > 2 ? argv[2] : "all";
    if (!std::strcmp(family,"all") || !std::strcmp(family,"iwm"))
        testFamily<ItlIwm,iwm_softc,iwm_node,iwm_phy_ctxt>(selected);
    if (!std::strcmp(family,"all") || !std::strcmp(family,"iwx"))
        testFamily<ItlIwx,iwx_softc,iwx_node,iwx_phy_ctxt>(selected);
    assert(cases != 0 && locks.empty());
    std::printf("production firmware context owner: %u scenarios PASS\n",cases);
}
