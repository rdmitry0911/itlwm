/*
* Copyright (C) 2026  itlwm contributors
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef IwxApGoCapability_hpp
#define IwxApGoCapability_hpp

#include <stdbool.h>
#include <sys/types.h>

#include "if_iwxvar.h"

/*
 * iwx firmware AP/GO capability classification (fail-closed).
 *
 * The Apple AP/APSTA owner contract requires a capability gate before
 * any AP/GO firmware command may be issued by the lower backend. The
 * upstream Linux iwlwifi driver advertises that capability through a
 * combination of NIC-family configuration tables (`iwl_cfg`,
 * `iwl_cfg_trans_params`) and firmware capability TLVs
 * (`IWL_UCODE_TLV_CAPA_*`). AP-mode power-save handling specifically
 * is gated by `IWL_UCODE_TLV_CAPA_AP_LINK_PS`, and AP/SoftAP MAC
 * context bring-up reuses `IWX_MAC_CONTEXT_CMD` (already implemented
 * locally for STA mode) with the `IWX_FW_MAC_TYPE_GO` MAC type.
 *
 * The local iwx command-interface header `if_iwxreg.h` does not
 * declare AP/SoftAP capability TLV constants. The local port also
 * lacks a per-NIC family configuration table that classifies AP/GO
 * support per firmware image. While both are absent, no specific
 * firmware image in the local `itlwm/firmware/` tree can be proven
 * to support AP/GO MAC contexts; the per-family classification here
 * is therefore deliberately fail-closed.
 *
 * Returning `false` for every device family is the truthful state of
 * the driver, not a fallback. Promoting any per-family entry from
 * `false` to `true` requires a TLV-driven runtime check, the
 * AP-mode arm of `iwx_mac_ctxt_cmd_common`, and HostAP enablement
 * under a scoped opt-out of `IEEE80211_STA_ONLY` — none of which
 * are implemented in this driver.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * iwx_firmware_family_supports_ap_go returns whether the iwx firmware
 * loaded for the given `device_family` is known to support AP/GO MAC
 * contexts and the AP/GO firmware command set.
 *
 * The function returns `false` for every recognized device family
 * because no per-family entry has been promoted to `true`. The
 * `default` arm also returns `false` so unknown future device
 * families fail closed.
 */
static inline bool iwx_firmware_family_supports_ap_go(int device_family)
{
    switch (device_family) {
    case IWX_DEVICE_FAMILY_22000:
        return false;
    case IWX_DEVICE_FAMILY_AX210:
        return false;
    default:
        return false;
    }
}

/*
 * iwx_softc_supports_ap_go returns whether the iwx softc has loaded a
 * firmware image whose advertised TLV capabilities admit AP/GO MAC
 * context bring-up. Defense-in-depth fail-closed surface: the function
 * gates each successive check below the previous one and returns `true`
 * only if every gate passes.
 *
 * Gate 1 — per-family classification. Calls
 *   iwx_firmware_family_supports_ap_go(sc->sc_device_family).
 *   The classification returns `false` for every recognised family
 *   today, so this gate alone keeps the function fail-closed.
 *
 * Gate 2 — IWX_UCODE_TLV_FLAGS_GO_UAPSD (bit 30 of `sc_capaflags`).
 *   The only locally-named AP/GO firmware-capability flag in
 *   `itlwm/hal_iwx/if_iwxreg.h`. Every iwlwifi-*-68.ucode image in
 *   the current `itlwm/firmware/` tree advertises this flag, so a
 *   per-image firmware-side veto on this gate would be unusual but
 *   the gate is still required to keep AP/GO claims contingent on
 *   firmware evidence rather than family alone.
 *
 * Gate 3 — IWX_UCODE_TLV_CAPA_BEACON_STORING (bit 72 of
 *   `sc_enabled_capa`). The only locally-named AP/GO-relevant CAPA
 *   bit. None of the iwlwifi-*-68.ucode images in the current local
 *   firmware inventory advertise this capability, so this gate
 *   forces a `false` answer across the entire current iwx fleet
 *   even if Gate 1 is later promoted to `true` for some family.
 *
 * Macros named above (`IWX_UCODE_TLV_FLAGS_GO_UAPSD`,
 * `IWX_UCODE_TLV_CAPA_BEACON_STORING`, and the `isset()` BSD bitmap
 * accessor) are declared in `itlwm/hal_iwx/if_iwxreg.h` and the
 * BSD-style `<sys/param.h>` chain. Consumers must include
 * `itlwm/hal_iwx/ItlIwx.hpp` (which already pulls `if_iwxreg.h`)
 * before this helper so the macros are in scope when the inline body
 * is expanded.
 */
static inline bool iwx_softc_supports_ap_go(const struct iwx_softc *sc)
{
    if (sc == NULL)
        return false;
    if (!iwx_firmware_family_supports_ap_go(sc->sc_device_family))
        return false;
    if ((sc->sc_capaflags & IWX_UCODE_TLV_FLAGS_GO_UAPSD) == 0)
        return false;
    if (!isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_BEACON_STORING))
        return false;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* IwxApGoCapability_hpp */
