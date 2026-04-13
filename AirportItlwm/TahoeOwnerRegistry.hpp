//
//  TahoeOwnerRegistry.hpp
//  AirportItlwm
//

#ifndef TahoeOwnerRegistry_hpp
#define TahoeOwnerRegistry_hpp

#include <stdint.h>

struct TahoeOwnerRegistry {
    struct IEOwner {
        uint8_t assocIe[2048] = {};
        uint32_t assocIeLen = 0;
        bool hasAssocIe = false;

        uint8_t vendorIe[2048] = {};
        uint32_t vendorIeLen = 0;
        uint32_t vendorIeFlags = 0;
        bool hasVendorIe = false;
    } ie;

    struct NdpOwner {
        uint32_t count = 0;
        uint8_t addresses[4][16] = {};
        uint8_t linkLocalSeed[16] = {};
        bool hasCarrier = false;
        bool hiddenNotifyQueued = false;
    } ndp;

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
        uint16_t profileCommandMode = 0;
        uint8_t profileCommandBand = 0;
        uint8_t profileCommandIndex = 0;
        uint32_t activeProfile = 0;
        uint16_t chainDisable = 0;
    } btcoex;

    struct ActionFrameOwner {
        uint8_t category = 0;
        uint32_t channel = 0;
        uint16_t frameLen = 0;
        uint8_t frame[0x708] = {};
        bool useV2 = false;
        bool hasFrame = false;
    } actionFrame;

    struct RangingOwner {
        uint16_t pmkLen = 0;
        uint32_t role = 0;
        uint32_t proximityOwnerId = 0;
        bool postedCallback = false;
        bool hasCarrier = false;
    } ranging;

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
