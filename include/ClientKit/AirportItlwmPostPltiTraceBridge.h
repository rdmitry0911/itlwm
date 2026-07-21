#ifndef AirportItlwmPostPltiTraceBridge_h
#define AirportItlwmPostPltiTraceBridge_h

#include <stdint.h>
#include <ClientKit/AirportItlwmPostPltiTrace.h>

struct ieee80211com;

#if defined(__IO80211_TARGET) && defined(__MAC_26_0) && \
    __IO80211_TARGET >= __MAC_26_0

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fast-path-safe bridge.  These functions neither allocate, log, publish
 * properties, retain objects, nor inspect or retain frame contents.  An event
 * is ignored unless the currently armed safe trace is bound to `ic`.
 */
void AirportItlwmPostPltiTraceBeginEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceRecord(struct ieee80211com *ic,
                                     uint32_t event);
void AirportItlwmPostPltiTraceCompleteEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceAbortEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceNoteStateRequest(struct ieee80211com *ic,
                                               uint32_t oldState,
                                               uint32_t nextState);

#ifdef __cplusplus
}
#endif

#else

/* Shared net80211/HAL sources compile into pre-Tahoe targets as well. */
static inline void
AirportItlwmPostPltiTraceBeginEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceRecord(struct ieee80211com *ic, uint32_t event)
{
    (void)ic;
    (void)event;
}

static inline void
AirportItlwmPostPltiTraceCompleteEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceAbortEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceNoteStateRequest(struct ieee80211com *ic,
                                          uint32_t oldState,
                                          uint32_t nextState)
{
    (void)ic;
    (void)oldState;
    (void)nextState;
}

#endif

#endif /* AirportItlwmPostPltiTraceBridge_h */
