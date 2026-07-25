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
/*
 * Starts the distinct IWN physical-WCL scan episode.  The sole positive
 * completion is the exact lower terminal followed by result/DONE publication
 * calls;
 * it never stands for association, credentials, a BSS identity, or traffic.
 */
void AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode(
    struct ieee80211com *ic);
/*
 * Starts the distinct direct-IWN-SAE laboratory episode.  This shares only
 * the preallocated, identity-free recorder mechanics with the older PMK
 * scan-resume trace; its first categorical fact has its own ABI value and
 * never implies that a PMK came from WCL, PLTI, an Agent, or a controller.
 */
void AirportItlwmPostPltiTraceBeginDirectSaeEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceRecord(struct ieee80211com *ic,
                                     uint32_t event);
/* Records only one of the fixed IWN physical-WCL scan event classes while
 * the matching scan episode remains current. */
void AirportItlwmPostPltiTraceRecordWclPhysicalScan(
    struct ieee80211com *ic, uint32_t event);
/*
 * Records the coherent IGTK publication-plus-TX-selection pair in one
 * recorder admission.  `slot` is the fixed IGTK table category (4 or 5),
 * never a descriptor, key byte, BSS identity, or pointer.
 */
void AirportItlwmPostPltiTraceRecordIgtkPublicationSelection(
    struct ieee80211com *ic, uint32_t slot);
void AirportItlwmPostPltiTraceCompleteEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode(
    struct ieee80211com *ic);
void AirportItlwmPostPltiTraceAbortEpisode(struct ieee80211com *ic);
void AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode(
    struct ieee80211com *ic);
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
AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceBeginDirectSaeEpisode(struct ieee80211com *ic)
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
AirportItlwmPostPltiTraceRecordWclPhysicalScan(struct ieee80211com *ic,
                                                uint32_t event)
{
    (void)ic;
    (void)event;
}

static inline void
AirportItlwmPostPltiTraceRecordIgtkPublicationSelection(
    struct ieee80211com *ic, uint32_t slot)
{
    (void)ic;
    (void)slot;
}

static inline void
AirportItlwmPostPltiTraceCompleteEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode(
    struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceAbortEpisode(struct ieee80211com *ic)
{
    (void)ic;
}

static inline void
AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode(
    struct ieee80211com *ic)
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
