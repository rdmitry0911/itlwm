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
 * iwx firmware AP/GO capability classification.
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
 * The local backend now owns the complete open-AP command subset.  Admit it
 * only in the Tahoe HostAP opt-out build and only when the loaded firmware
 * advertises the exact queue/station APIs used by that implementation.  This
 * is deliberately narrower than Linux's general AP family support.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * iwx_firmware_family_supports_ap_go returns whether the iwx firmware
 * loaded for the given `device_family` is known to support AP/GO MAC
 * contexts and the AP/GO firmware command set.
 *
 * Both local iwx families use the modern TVQM command transport. Unknown
 * future families remain closed until their command carriers are audited.
 */
static inline bool iwx_firmware_family_supports_ap_go(int device_family)
{
    switch (device_family) {
    case IWX_DEVICE_FAMILY_22000:
    case IWX_DEVICE_FAMILY_AX210:
        return true;
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
 * BEACON_STORING and GO_UAPSD are optional features, not base AP admission
 * bits. Linux iwlwifi does not require either for start_ap; requiring them
 * here would incorrectly hide working AP support. The load-bearing gates are
 * DQA, typed stations, ADD_STA v12+, and the v11/v12 beacon carrier actually
 * emitted by this backend.
 */
static inline uint8_t
iwx_ap_go_command_version(const struct iwx_softc *sc, uint8_t group,
                          uint8_t command)
{
    for (int i = 0; i < sc->n_cmd_versions; i++) {
        const struct iwx_fw_cmd_version *entry = &sc->cmd_versions[i];
        if (entry->group == group && entry->cmd == command)
            return entry->cmd_ver;
    }
    return IWX_FW_CMD_VER_UNKNOWN;
}

static inline bool iwx_softc_supports_ap_go(const struct iwx_softc *sc)
{
#if !defined(IEEE80211_OPT_OUT_STA_ONLY)
    (void)sc;
    return false;
#else
    if (sc == NULL)
        return false;
    if (!iwx_firmware_family_supports_ap_go(sc->sc_device_family))
        return false;
    if (!isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_DQA_SUPPORT) ||
        !isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_STA_TYPE))
        return false;
    const uint8_t addStationVersion = iwx_ap_go_command_version(
        sc, IWX_LONG_GROUP, IWX_ADD_STA);
    if (addStationVersion == IWX_FW_CMD_VER_UNKNOWN ||
        addStationVersion < 12)
        return false;
    const uint8_t txCommandVersion = iwx_ap_go_command_version(
        sc, IWX_LONG_GROUP, IWX_TX_CMD);
    if (txCommandVersion == IWX_FW_CMD_VER_UNKNOWN ||
        txCommandVersion <= 8)
        return false;
    const uint8_t beaconVersion = iwx_ap_go_command_version(
        sc, IWX_LONG_GROUP, IWX_BEACON_TEMPLATE_CMD);
    return beaconVersion == 11 || beaconVersion == 12;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* IwxApGoCapability_hpp */
