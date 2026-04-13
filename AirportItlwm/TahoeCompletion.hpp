//
//  TahoeCompletion.hpp
//  AirportItlwm
//

#ifndef TahoeCompletion_hpp
#define TahoeCompletion_hpp

#include "TahoeAsyncCommandContext.hpp"

namespace TahoeCompletion {

inline void begin(TahoeAsyncCommandContext *ctx,
                  uint32_t token,
                  uint32_t selector,
                  uint32_t owner,
                  uint32_t transport,
                  uint32_t requestBytes,
                  uint32_t responseBytes,
                  bool async)
{
    if (ctx == nullptr)
        return;
    ctx->token = token;
    ctx->selector = selector;
    ctx->owner = owner;
    ctx->transport = transport;
    ctx->requestBytes = requestBytes;
    ctx->responseBytes = responseBytes;
    ctx->steps += 1;
    ctx->async = async;
    ctx->completed = false;
    ctx->timedOut = false;
}

inline void complete(TahoeAsyncCommandContext *ctx, uint32_t status = 0)
{
    if (ctx == nullptr)
        return;
    ctx->status = status;
    ctx->completed = true;
}

inline void timeout(TahoeAsyncCommandContext *ctx, uint32_t status)
{
    if (ctx == nullptr)
        return;
    ctx->status = status;
    ctx->timedOut = true;
    ctx->completed = true;
}

} // namespace TahoeCompletion

#endif /* TahoeCompletion_hpp */
