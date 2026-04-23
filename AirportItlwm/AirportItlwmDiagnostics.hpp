#ifndef AirportItlwmDiagnostics_hpp
#define AirportItlwmDiagnostics_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <ClientKit/AirportItlwmDiagnostics.h>

class AirportItlwm;
struct ieee80211com;
struct ieee80211_node;

class AirportItlwmDiagnosticsService : public IOService {
    OSDeclareDefaultStructors(AirportItlwmDiagnosticsService)
public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
                           OSDictionary *properties, IOUserClient **handler) override;
    AirportItlwm *getDriver() const;

private:
    AirportItlwm *fDriver;
};

class AirportItlwmDiagnosticUserClient : public IOUserClient {
    OSDeclareDefaultStructors(AirportItlwmDiagnosticUserClient)
public:
    bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
                      OSDictionary *properties) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn clientClose(void) override;
    IOReturn clientDied(void) override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch = 0,
                            OSObject *target = 0,
                            void *reference = 0) override;

private:
    task_t fTask;
    AirportItlwm *fDriver;
};

bool airportItlwmDiagPublishService(AirportItlwm *driver);
void airportItlwmDiagTerminateService(AirportItlwm *driver);
void airportItlwmDiagClear(void);
void airportItlwmDiagTrace(uint32_t kind, uint32_t path, int32_t command,
                           int32_t requestType, IOReturn result, int32_t arg0,
                           uint64_t arg1, uint64_t arg2, uint32_t requiredMask);
void airportItlwmDiagRecordAssoc(uint32_t path, const uint8_t *ssid,
                                 uint32_t ssidLen, const uint8_t *bssid,
                                 uint32_t authLower, uint32_t authUpper,
                                 uint32_t rsnIeLen, IOReturn result);
void airportItlwmDiagRecordData(uint32_t path, uint32_t length, bool eapol,
                                IOReturn result);
void airportItlwmDiagRecordScanNode(struct ieee80211com *ic,
                                    struct ieee80211_node *ni);
bool airportItlwmDiagShouldBlock(uint32_t blockMask);
void airportItlwmDiagRecordBlock(uint32_t blockMask, uint32_t path,
                                 uint32_t length);
AirportItlwmDiagConfig airportItlwmDiagCopyConfig(void);

#endif /* AirportItlwmDiagnostics_hpp */
