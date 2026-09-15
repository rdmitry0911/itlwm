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
};

#endif /* ItlDriverInfo_h */
