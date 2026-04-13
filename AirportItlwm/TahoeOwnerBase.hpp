//
//  TahoeOwnerBase.hpp
//  AirportItlwm
//

#ifndef TahoeOwnerBase_hpp
#define TahoeOwnerBase_hpp

#include "TahoeAsyncCommandContext.hpp"
#include "TahoeCompletion.hpp"

class TahoeOwnerBase {
public:
    static inline void completeSync(TahoeAsyncCommandContext *asyncContext,
                                    uint32_t selector,
                                    uint32_t owner)
    {
        if (asyncContext == nullptr)
            return;
        asyncContext->selector = selector;
        asyncContext->owner = owner;
        asyncContext->status = 0;
        asyncContext->async = false;
        asyncContext->completed = true;
    }
};

#endif /* TahoeOwnerBase_hpp */
