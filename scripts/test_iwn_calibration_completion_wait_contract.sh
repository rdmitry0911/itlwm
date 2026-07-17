#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hal_hpp="$root/include/HAL/ItlHalService.hpp"
hal_cpp="$root/include/HAL/ItlHalService.cpp"
iwn_cpp="$root/itlwm/hal_iwn/ItlIwn.cpp"

require() {
    local file=$1
    local text=$2
    grep -Fq "$text" "$file" || {
        printf 'missing required calibration-wait contract: %s\n' "$text" >&2
        exit 1
    }
}

require "$hal_hpp" 'void lockTsleep();'
require "$hal_hpp" 'void unlockTsleep();'
require "$hal_hpp" 'int tsleep_nsec_locked(void *ident, int priority, const char *wmesg,'
require "$hal_cpp" 'return msleep(ident, this->inner_lock, priority, wmesg, timeout);'
require "$hal_cpp" 'const int err = tsleep_nsec_locked(ident, priority, wmesg, timo);'
require "$iwn_cpp" 'that->lockTsleep();'
require "$iwn_cpp" 'error = that->tsleep_nsec_locked(sc, PCATCH, "iwncal", remaining_nsec);'
require "$iwn_cpp" 'that->unlockTsleep();'
require "$iwn_cpp" '#include <kern/clock.h>'
require "$iwn_cpp" 'clock_interval_to_deadline(2, kSecondScale, &deadline);'
require "$iwn_cpp" 'while (!(sc->sc_flags & IWN_FLAG_CALIB_DONE)) {'
require "$iwn_cpp" 'clock_get_uptime(&now);'
require "$iwn_cpp" 'absolutetime_to_nanoseconds(deadline - now, &remaining_nsec);'
require "$iwn_cpp" 'if (remaining_nsec == 0) {'
require "$iwn_cpp" 'if (error != 0 && !(sc->sc_flags & IWN_FLAG_CALIB_DONE))'

notify_block=$(awk '
    /case IWN5000_CALIBRATION_DONE:/ { in_block = 1 }
    in_block { print }
    in_block && /^[[:space:]]*break;/ { exit }
' "$iwn_cpp")

for required in 'lockTsleep();' 'sc->sc_flags |= IWN_FLAG_CALIB_DONE;' 'wakeupOn(sc);' 'unlockTsleep();'; do
    printf '%s\n' "$notify_block" | grep -Fq "$required" || {
        printf 'missing notifier step: %s\n' "$required" >&2
        exit 1
    }
done

line_of() {
    local needle=$1
    printf '%s\n' "$notify_block" | grep -Fnm1 "$needle" | cut -d: -f1
}

lock_line=$(line_of 'lockTsleep();')
flag_line=$(line_of 'sc->sc_flags |= IWN_FLAG_CALIB_DONE;')
wake_line=$(line_of 'wakeupOn(sc);')
unlock_line=$(line_of 'unlockTsleep();')
(( lock_line < flag_line && flag_line < wake_line && wake_line < unlock_line )) || {
    printf 'CALIBRATION_DONE notifier does not retain mutex through wakeup\n' >&2
    exit 1
}

printf 'iwn calibration completion wait contract: PASS\n'
