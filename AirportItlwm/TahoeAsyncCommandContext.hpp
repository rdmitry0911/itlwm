//
//  TahoeAsyncCommandContext.hpp
//  AirportItlwm
//

#ifndef TahoeAsyncCommandContext_hpp
#define TahoeAsyncCommandContext_hpp

#include <stdint.h>

struct TahoeAsyncCommandContext {
    uint32_t selector = 0;
    uint32_t owner = 0;
    uint32_t status = 0;
    bool async = false;
    bool completed = false;
};

#endif /* TahoeAsyncCommandContext_hpp */
