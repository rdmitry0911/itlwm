//
//  IO80211BssManager.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211BssManager surface exported by IO80211Family on macOS Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Callers only ever hold an `IO80211BssManager *` returned by
//  the kernel; the local kext does not allocate, subclass, or take
//  `sizeof` of this class.
//
//  CR-201 — primitive-only batch (BootKC IO80211Family.kc, recovered
//  2026-04-28). Each declaration's return type is matched verbatim
//  against the C decompile produced by Ghidra 12.2 against the
//  BootKernelExtensions.kc program in /srv/project/ghidra_output on
//  the remote host. Evidence is captured in
//  analysis/cr201_bssmgr_decomp.c. Helpers whose decompile yielded an
//  ambiguous Ghidra placeholder (`undefined4`, `undefined8`) for the
//  return type are deferred — the BootKC mangling alone does not prove
//  the C++ return type, so they are excluded per the CR-200 rejection
//  guidance. Helpers whose decompile failed (`MISSING` at the recorded
//  address — Ghidra could not locate a function entry there in this
//  pass) are also deferred. Helpers whose signatures reference kernel-
//  internal struct/enum types are likewise deferred.
//
//  BootKC anchors (return types per Ghidra C decompile):
//    ffffff80022665a0  IO80211BssManager::isAssociatedOnHighBand()        // byte
//    ffffff8002266722  IO80211BssManager::isAssociatedToAdhoc()           // bool
//    ffffff8002266cfc  IO80211BssManager::resetRateAndIndexSet()          // void
//    ffffff8002266da8  IO80211BssManager::isAssociatedToiOSDevice()       // ulong
//    ffffff8002268030  IO80211BssManager::setAdHocCreated(bool)           // void
//    ffffff8002268054  IO80211BssManager::setSISOAssoc(bool)              // void
//    ffffff8002268066  IO80211BssManager::setPrivateMacJoinStatus(bool)   // void
//    ffffff8002268078  IO80211BssManager::setDeviceTypeInDhcpAllowStatus(bool)   // void
//    ffffff800226808a  IO80211BssManager::getPrivateMacJoinStatus()       // ulong
//    ffffff800226809c  IO80211BssManager::getDeviceTypeInDhcpAllowStatus()        // ulong
//    ffffff80022680ae  IO80211BssManager::setAssociateToHotspotInWoWMode(bool)   // void
//    ffffff80022680c0  IO80211BssManager::isAssociateToHotspotInWoWMode() // ulong
//    ffffff8002268106  IO80211BssManager::get6gStandAloneTopology()       // ulong
//    ffffff8002268118  IO80211BssManager::set6gStandAloneTopology(bool)   // void
//
//  CR-479 writer addendum (BootKC IO80211Family.kc, recovered from
//  caller-side AppleBCMWLANNetAdapter disassembly and symbol metadata):
//    ffffff8002266b24  IO80211BssManager::setMCSIndexSet(apple80211_mcs_index_set_data&)
//    ffffff8002266bc2  IO80211BssManager::setVHTMCSIndexSet(apple80211_vht_mcs_index_set_data&)
//    ffffff8002266c4a  IO80211BssManager::setHEMCSIndexSet(apple80211_he_mcs_index_set_data&)
//    ffffff8002266cb2  IO80211BssManager::setRateSet(apple80211_rate_set_data&)
//    ffffff800226682e  IO80211BssManager::setLastBSSRssi()
//    ffffff8002266884  IO80211BssManager::getCurrentBand(Bands&)
//    ffffff8002266fee  IO80211BssManager::setBandInfoBitmap(unsigned int)
//    ffffff800226701e  IO80211BssManager::setAuthContext(IO80211AuthContext&)
//    ffffff800226713c  IO80211BssManager::setAssocSSID(unsigned char const*, unsigned long)
//    ffffff8002267afa  IO80211BssManager::setAssocRSNIE(unsigned char const*, unsigned long)
//
//  These writer declarations are not part of the CR-201 primitive-only
//  fourteen-helper batch. They are live current-BSS cache producers used by
//  Apple's rate/MCS update path and by the local Tahoe bridge once the
//  framework-owned BssManager object is recovered from WCLConfigManager.
//

#ifndef IO80211BssManager_h
#define IO80211BssManager_h

#include <stdint.h>

struct apple80211_mcs_index_set_data;
struct apple80211_vht_mcs_index_set_data;
struct apple80211_he_mcs_index_set_data;
struct apple80211_rate_set_data;
enum Bands : unsigned int;

struct IO80211AuthContext {
    uint32_t authLower;
    uint32_t authUpper;
    uint32_t authFlags;
    uint32_t bssInfoFlags;
};

static_assert(sizeof(IO80211AuthContext) == 0x10,
              "IO80211AuthContext must match the BssManager qword-copy ABI");

#ifdef TAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST
typedef int IOReturn;
#endif

class IO80211BssManager
{
public:
    // Tahoe exports used by the native WCL driver to seed current-BSS rate
    // and MCS caches consumed by IO80211InfraInterface link properties.
    void setMCSIndexSet(apple80211_mcs_index_set_data &);
    void setVHTMCSIndexSet(apple80211_vht_mcs_index_set_data &);
    void setHEMCSIndexSet(apple80211_he_mcs_index_set_data &);
    void setRateSet(apple80211_rate_set_data &);
    void setLastBSSRssi();
    IOReturn getCurrentBand(Bands &);
    void setBandInfoBitmap(unsigned int);
    void setAuthContext(IO80211AuthContext &);
    IOReturn setAssocSSID(const unsigned char *, unsigned long);
    IOReturn setAssocRSNIE(const unsigned char *, unsigned long);
    unsigned char isAssociatedOnHighBand();
    bool isAssociatedToAdhoc();
    void resetRateAndIndexSet();
    unsigned long isAssociatedToiOSDevice();
    void setAdHocCreated(bool);
    void setSISOAssoc(bool);
    void setPrivateMacJoinStatus(bool);
    void setDeviceTypeInDhcpAllowStatus(bool);
    unsigned long getPrivateMacJoinStatus();
    unsigned long getDeviceTypeInDhcpAllowStatus();
    void setAssociateToHotspotInWoWMode(bool);
    unsigned long isAssociateToHotspotInWoWMode();
    unsigned long get6gStandAloneTopology();
    void set6gStandAloneTopology(bool);
};

#endif /* IO80211BssManager_h */
