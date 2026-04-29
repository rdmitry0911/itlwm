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

#ifndef IO80211BssManager_h
#define IO80211BssManager_h

class IO80211BssManager
{
public:
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
