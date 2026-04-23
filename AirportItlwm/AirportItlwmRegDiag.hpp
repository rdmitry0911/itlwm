#ifndef AirportItlwmRegDiag_hpp
#define AirportItlwmRegDiag_hpp

#include <ClientKit/AirportItlwmRegDiag.h>
#include <IOKit/IOTypes.h>

class AirportItlwm;

void airportItlwmRegDiagPoll(AirportItlwm *driver);
void airportItlwmRegDiagTrace(uint32_t kind, uint32_t path, IOReturn result,
                              int32_t arg0, uint64_t arg1, uint64_t arg2);
void airportItlwmRegDiagRecordAssoc(uint32_t path, const uint8_t *ssid,
                                    uint32_t ssidLen, const uint8_t *bssid,
                                    uint32_t authLower, uint32_t authUpper,
                                    uint32_t rsnIeLen, IOReturn result);
void airportItlwmRegDiagRecordData(uint32_t path, uint32_t length, bool eapol,
                                   IOReturn result);
bool airportItlwmRegDiagShouldBlock(uint32_t blockMask);
void airportItlwmRegDiagRecordBlock(uint32_t blockMask, uint32_t path,
                                    uint32_t length);

#endif /* AirportItlwmRegDiag_hpp */
