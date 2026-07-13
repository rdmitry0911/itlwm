//
//  AirportAWDL.cpp
//  AirportItlwm
//
//  Created by qcwap on 2020/9/4.
//  Copyright © 2020 钟先耀. All rights reserved.
//

#include "AirportItlwm.hpp"

#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>

#define INTERFACE_NAME(object) \
 OSDynamicCast(IO80211Interface, object) == nullptr ? (OSDynamicCast(IO80211P2PInterface, object) == nullptr ? "???" : OSDynamicCast(IO80211P2PInterface, object)->getBSDName()) : OSDynamicCast(IO80211Interface, object)->getBSDName()

IOReturn AirportItlwm::
getIE(OSObject *object, struct apple80211_ie_data *data)
{
    XYLog("%s %s Error\n", __FUNCTION__,  INTERFACE_NAME(object));
    return kIOReturnError;
}

IOReturn AirportItlwm::
setIE(OSObject *object, struct apple80211_ie_data *data)
{
    if (data == nullptr || data->ie_len == 0 || data->ie_len > sizeof(data->ie))
        return static_cast<IOReturn>(0x16);

    // The legacy controller entry has no Intel implementation of the Apple
    // WAPI or vndr_ie paths. Keep the same visible invalid range, but do not
    // acknowledge non-null IE data after local bookkeeping alone.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwm::
setP2P_SCAN(OSObject *object, struct apple80211_scan_data *data)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setP2P_LISTEN(OSObject *object, struct apple80211_p2p_listen_data *data)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setP2P_GO_CONF(OSObject *object, struct apple80211_p2p_go_conf_data *data)
{
    return kIOReturnSuccess;
}
