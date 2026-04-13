//
//  TahoeAsyncCommandContext.hpp
//  AirportItlwm
//

#ifndef TahoeAsyncCommandContext_hpp
#define TahoeAsyncCommandContext_hpp

#include <stdint.h>

enum TahoeCommandTransport : uint32_t {
    kTahoeTransportNone = 0,
    kTahoeTransportIOVarSet = 1,
    kTahoeTransportVirtualIOVarSet = 2,
    kTahoeTransportVirtualIOCtlSet = 3,
    kTahoeTransportIssueCommand = 4,
    kTahoeTransportHiddenCallback = 5,
};

struct TahoeAsyncCommandContext {
    uint32_t token = 0;
    uint32_t selector = 0;
    uint32_t owner = 0;
    uint32_t status = 0;
    uint32_t transport = kTahoeTransportNone;
    uint32_t requestBytes = 0;
    uint32_t responseBytes = 0;
    uint32_t steps = 0;
    bool async = false;
    bool completed = false;
    bool timedOut = false;
};

#endif /* TahoeAsyncCommandContext_hpp */
