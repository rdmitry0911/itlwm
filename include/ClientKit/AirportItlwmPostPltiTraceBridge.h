#ifndef AirportItlwmPostPltiTraceBridge_h
#define AirportItlwmPostPltiTraceBridge_h

#include <stdint.h>
#include <ClientKit/AirportItlwmPostPltiTrace.h>

struct ieee80211com;

/*
 * Shared producer sources can reach this header before the Tahoe compatibility
 * headers declare __MAC_26_0.  The Tahoe target alone supplies
 * IO80211FAMILY_V3 in both configurations, so select the real bridge from
 * that build identity instead of silently compiling a local no-op stub.
 */
#if defined(IO80211FAMILY_V3)

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
