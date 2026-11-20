/*
* Copyright (C) 2020  钟先耀
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef ItlDriverInfo_h
#define ItlDriverInfo_h

#include <stdint.h>

enum {
    ITL_TX_RATE_LEGACY,
    ITL_TX_RATE_HT,
    ITL_TX_RATE_VHT,
    ITL_TX_RATE_HE,     // and later
};

// A transmit rate in plain terms: see ItlDriverInfo::getTxRate().
struct ItlTxRate {
    uint32_t kbps;
    uint8_t  mode;      // ITL_TX_RATE_*
    uint8_t  mcs;       // HT: 0-31; VHT and later: per stream
    uint8_t  nss;       // VHT and later
    uint8_t  bw;        // 0 = 20 MHz, 1 = 40, 2 = 80, 3 = 160
    bool     sgi;       // short guard interval (HT, VHT)
};

class ItlDriverInfo {
    
public:
    
    virtual const char *getFirmwareVersion() = 0;
    
    virtual int16_t getBSSNoise() = 0;

    // Airtime-based channel-occupancy estimate (0-100%) for the LQM CCA feed.
    // Negative == unavailable. Defaulted so HALs without a wired airtime
    // source stay fail-open (LQM marks CCA invalid) rather than publishing a
    // fabricated percentage; the iwx HAL overrides it with the firmware
    // statistics-derived occupancy.
    virtual int16_t getChannelLoad() { return -1; }

    virtual bool is5GBandSupport() = 0;
    
    virtual int getTxNSS() = 0;

    virtual uint8_t getTxChainMask() = 0;

    virtual uint8_t getRxChainMask() = 0;

    virtual uint32_t getLqmBeaconCount() = 0;
    
    virtual const char *getFirmwareName() = 0;
    
    virtual UInt32 supportedFeatures() = 0;

    virtual const char *getFirmwareCountryCode() = 0;

    virtual uint32_t getTxQueueSize() = 0;

    // For a HAL whose firmware reports the current transmit rate and the
    // transmit power limit in plain terms instead of Intel rate_n_flags and
    // NVM fields.  False == no value here; the Airport layer then reads the
    // HAL-specific sources it knows.
    virtual bool getTxRate(struct ItlTxRate *rate) { return false; }

    virtual bool getMaxTxPowerHalfDbm(uint8_t *halfDbm) { return false; }

    // True when the HAL wants 802.11ax published in the card capabilities,
    // which is what the system reads the supported PHY modes from.  The
    // Intel HALs keep their published capability set.
    virtual bool publishesHeCapability() { return false; }
};

#endif /* ItlDriverInfo_h */
