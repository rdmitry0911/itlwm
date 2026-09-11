/* SAE worker -> main-workloop timer doorbell. No crypto/peer/node state.
 * The HAL must close and drain its worker before shutdown(); callbacks on
 * the workloop consult the HAL's current value owner, never a captured epoch.
 */
#ifndef ITL_SAE_PEER_TIMER_HPP
#define ITL_SAE_PEER_TIMER_HPP
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>

class ItlSaePeerTimer {
    IOSimpleLock *lock = nullptr;
    IOWorkLoop *loop = nullptr;
    IOInterruptEventSource *source = nullptr;
    IOTimerEventSource *timer = nullptr;
    bool sourceAdded = false;
    bool timerAdded = false;
public:
    bool init(OSObject *owner, IOWorkLoop *workloop,
              IOInterruptEventSource::Action event,
              IOTimerEventSource::Action timeout)
    {
        if (lock != nullptr || workloop == nullptr)
            return false;
        lock = IOSimpleLockAlloc();
        if (lock == nullptr)
            return false;
        loop = workloop;
        source = IOInterruptEventSource::interruptEventSource(owner, event);
        timer = IOTimerEventSource::timerEventSource(owner, timeout);
        if (source == nullptr || timer == nullptr)
            goto fail;
        if (loop->addEventSource(source) != kIOReturnSuccess)
            goto fail;
        sourceAdded = true;
        if (loop->addEventSource(timer) != kIOReturnSuccess)
            goto fail;
        timerAdded = true;
        source->enable();
        timer->enable();
        return true;
    fail:
        shutdown();
        return false;
    }

    /* No runAction, sleep or main-workloop wait from the crypto worker. */
    void signal()
    {
        if (lock == nullptr)
            return;
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
        IOInterruptEventSource *current = source;
        if (current != nullptr)
            current->retain();
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
        if (current != nullptr) {
            current->interruptOccurred(nullptr, nullptr, 0);
            current->release();
        }
    }

    /* Called only on the retained main workloop. Stale firings re-read the
     * current owner/deadline; stop and peer progress need not cancel a timer. */
    void arm(uint64_t deadline, uint64_t now)
    {
        if (loop == nullptr || !loop->inGate() || lock == nullptr ||
            deadline == 0 || deadline <= now)
            return;
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
        IOTimerEventSource *current = timer;
        if (current != nullptr)
            current->retain();
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
        if (current == nullptr)
            return;
        uint64_t ns = 0;
        absolutetime_to_nanoseconds(deadline - now, &ns);
        uint64_t ms = ns / 1000000 + (ns % 1000000 != 0);
        if (ms == 0)
            ms = 1;
        if (ms > UINT32_MAX)
            ms = UINT32_MAX;
        current->setTimeoutMS(static_cast<uint32_t>(ms));
        current->release();
    }

    void shutdown()
    {
        IOInterruptEventSource *oldSource = source;
        IOTimerEventSource *oldTimer = timer;
        if (lock != nullptr) {
            IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
            source = nullptr;
            timer = nullptr;
            IOSimpleLockUnlockEnableInterrupt(lock, irq);
        }
        if (oldSource != nullptr) {
            oldSource->disable();
            if (sourceAdded)
                loop->removeEventSource(oldSource);
            oldSource->release();
        }
        if (oldTimer != nullptr) {
            oldTimer->disable();
            if (timerAdded)
                loop->removeEventSource(oldTimer);
            oldTimer->cancelTimeout();
            oldTimer->release();
        }
        sourceAdded = timerAdded = false;
        loop = nullptr;
        if (lock != nullptr) {
            IOSimpleLockFree(lock);
            lock = nullptr;
        }
    }
};
#endif
