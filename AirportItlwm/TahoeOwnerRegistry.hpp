//
//  TahoeOwnerRegistry.hpp
//  AirportItlwm
//

#ifndef TahoeOwnerRegistry_hpp
#define TahoeOwnerRegistry_hpp

#include <stdint.h>

struct TahoeOwnerRegistry {
    struct UsbHostNotificationOwner {
        uint32_t sequenceNumber = 0;
        uint32_t change = 0;
        uint32_t present = 0;
        uint32_t lastAsymMitExtUsb = 0;
        uint32_t lastAsymMitExtUsbChange = 0;
        bool hasCarrier = false;
        bool hasChangePayload = false;
    } usbHostNotification;

    struct BtcoexOwner {
        uint8_t profileTable[10][0x38] = {};
        uint16_t profileValidMask = 0;
        uint32_t activeProfile = 0;
        uint16_t chainDisable = 0;
    } btcoex;

    struct TxPowerCapBypassOwner {
        bool enabled = false;
        bool sendEligible = false;
        uint32_t lastPayload = 0;
    } txPowerCapBypass;

    struct DualPowerModeOwner {
        int32_t primary = -1;
        int32_t secondary = -1;
    } dualPowerMode;

    void reset()
    {
        *this = TahoeOwnerRegistry();
    }

    void syncDualPowerMode(int32_t primaryValue, int32_t secondaryValue)
    {
        dualPowerMode.primary = primaryValue;
        dualPowerMode.secondary = secondaryValue;
        txPowerCapBypass.sendEligible =
            (primaryValue != -1) && (secondaryValue != -1);
    }
};

#endif /* TahoeOwnerRegistry_hpp */
