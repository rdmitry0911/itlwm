# @category Analysis
# @runtime Jython

from ghidra.app.decompiler import DecompInterface, DecompileOptions
import java.io.FileWriter as FileWriter
import java.io.PrintWriter as PrintWriter
import os


FAMILIES = [
    {
        "id": "01_sleep_owner",
        "title": "BonjourOffload / sleep owner",
        "core_patterns": [
            "AppleBCMWLANCore::getSYSTEM_SLEEP_CONFIG",
        ],
        "io_patterns": [
            "apple80211getSYSTEM_SLEEP_CONFIG",
        ],
    },
    {
        "id": "02_usb_host_notification",
        "title": "USB host notification owner",
        "core_patterns": [
            "AppleBCMWLANCore::setUSB_HOST_NOTIFICATION",
        ],
        "io_patterns": [
            "apple80211setUSB_HOST_NOTIFICATION",
        ],
    },
    {
        "id": "03_hp2p_llw",
        "title": "HP2P / LLW owner",
        "core_patterns": [
            "AppleBCMWLANCore::getHP2P_CTRL",
            "AppleBCMWLANCore::checkForHP2PSupport",
            "AppleBCMWLANCore::isHP2PSupported",
            "AppleBCMWLANCore::setHp2pCtrlCallback",
        ],
        "io_patterns": [
            "apple80211getHP2P_CTRL",
        ],
    },
    {
        "id": "04_join_ie_owner",
        "title": "JoinAdapter IE owner",
        "core_patterns": [
            "AppleBCMWLANCore::setIE",
            "AppleBCMWLANJoinAdapter::setCustomAssocIE",
            "AppleBCMWLANJoinAdapter::setCustomAssocIEAsyncCallback",
            "AppleBCMWLANCore::setVendorIE",
        ],
        "io_patterns": [
            "apple80211setIE",
        ],
    },
    {
        "id": "05_action_frame_injector",
        "title": "NetAdapter action-frame injector",
        "core_patterns": [
            "AppleBCMWLANCore::setWCL_ACTION_FRAME",
            "AppleBCMWLANNetAdapter::sendActionFrame",
            "AppleBCMWLANNetAdapter::sendActionFrameV2",
        ],
        "io_patterns": [
            "apple80211setWCL_ACTION_FRAME",
        ],
    },
    {
        "id": "06_ipv6_ndp_offload",
        "title": "IPv6 / NDP offload owner",
        "core_patterns": [
            "AppleBCMWLANCore::setOFFLOAD_NDP",
            "AppleBCMWLANCore::handleIPv6AddressNotificationGated",
        ],
        "io_patterns": [
            "apple80211setOFFLOAD_NDP",
        ],
    },
    {
        "id": "07_btcoex_profile",
        "title": "BTCoex profile owner",
        "core_patterns": [
            "AppleBCMWLANCore::setBTCOEX_PROFILE",
        ],
        "io_patterns": [
            "apple80211setBTCOEX_PROFILE",
        ],
    },
    {
        "id": "08_btcoex_profile_active",
        "title": "BTCoex active-profile owner",
        "core_patterns": [
            "AppleBCMWLANCore::setBTCOEX_PROFILE_ACTIVE",
        ],
        "io_patterns": [
            "apple80211setBTCOEX_PROFILE_ACTIVE",
        ],
    },
    {
        "id": "09_btcoex_2g_chain_disable",
        "title": "BTCoex 2G chain disable owner",
        "core_patterns": [
            "AppleBCMWLANCore::setBTCOEX_2G_CHAIN_DISABLE",
        ],
        "io_patterns": [
            "apple80211setBTCOEX_2G_CHAIN_DISABLE",
        ],
    },
    {
        "id": "10_ranging_auth",
        "title": "Ranging / proximity auth owner",
        "core_patterns": [
            "AppleBCMWLANCore::setRANGING_AUTHENTICATE",
        ],
        "io_patterns": [
            "apple80211setRANGING_AUTHENTICATE",
        ],
    },
    {
        "id": "11_wow_internal",
        "title": "WoW internal owner",
        "core_patterns": [
            "AppleBCMWLANCore::setWOW_TEST",
            "AppleBCMWLANCore::configureWoWTestModeEntry",
        ],
        "io_patterns": [
            "apple80211setWOW_TEST",
        ],
    },
    {
        "id": "12_tvpm_power_budget",
        "title": "TVPM / power budget owner",
        "core_patterns": [
            "AppleBCMWLANCore::setPOWER_BUDGET",
        ],
        "io_patterns": [
            "apple80211setPOWER_BUDGET",
        ],
    },
    {
        "id": "13_tx_power_cap_bypass",
        "title": "TX power cap bypass sender",
        "core_patterns": [
            "AppleBCMWLANCore::setBYPASS_TX_POWER_CAP",
            "AppleBCMWLANCore::sendTxPowerCapBypassToFirmware",
        ],
        "io_patterns": [
            "apple80211setBYPASS_TX_POWER_CAP",
        ],
    },
]


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def pick_patterns(scope, family):
    if scope == "core":
        return family["core_patterns"]
    if scope == "io80211":
        return family["io_patterns"]
    raise RuntimeError("unsupported scope: " + scope)


def name_matches(name, pattern):
    return name == pattern


def decompile_function(decomp, func):
    res = decomp.decompileFunction(func, 180, monitor)
    if res.decompileCompleted() and res.getDecompiledFunction() is not None:
        return res.getDecompiledFunction().getC(), None
    return None, res.getErrorMessage()


args = getScriptArgs()
if len(args) != 3:
    print("usage: out_dir manifest_path scope(core|io80211)")
    exit(1)

out_dir = args[0]
manifest_path = args[1]
scope = args[2]

ensure_dir(out_dir)

decomp = DecompInterface()
opts = DecompileOptions()
opts.setMaxPayloadMBytes(512)
opts.setMaxInstructions(500000)
decomp.setOptions(opts)
decomp.toggleCCode(True)
decomp.toggleSyntaxTree(True)
decomp.setSimplificationStyle("decompile")
decomp.openProgram(currentProgram)

fm = currentProgram.getFunctionManager()
all_funcs = []
it = fm.getFunctions(True)
while it.hasNext() and not monitor.isCancelled():
    all_funcs.append(it.next())

manifest = PrintWriter(FileWriter(manifest_path, True))
manifest.println("## scope=%s program=%s" % (scope, currentProgram.getName()))

for family in FAMILIES:
    patterns = pick_patterns(scope, family)
    outfile = os.path.join(out_dir, family["id"] + ".c")
    writer = PrintWriter(FileWriter(outfile))
    writer.println("/* family: %s */" % family["title"])
    writer.println("/* scope: %s */" % scope)
    writer.println("/* program: %s */" % currentProgram.getName())
    writer.println("")

    matches = []
    seen = set()
    for func in all_funcs:
        name = func.getName(True)
        for pattern in patterns:
            if name_matches(name, pattern):
                key = "%s@%s" % (name, str(func.getEntryPoint()))
                if key not in seen:
                    matches.append(func)
                    seen.add(key)
                break

    manifest.println("%s\t%s\t%d" % (scope, family["id"], len(matches)))
    for pattern in patterns:
        manifest.println("pattern\t%s\t%s\t%s" % (scope, family["id"], pattern))

    for func in matches:
        code, err = decompile_function(decomp, func)
        writer.println("/* %s @ %s */" % (func.getName(True), str(func.getEntryPoint())))
        if code is not None:
            writer.println(code)
        else:
            writer.println("/* FAILED: %s */" % err)
        writer.println("")
        manifest.println("match\t%s\t%s\t%s\t%s" % (
            scope,
            family["id"],
            str(func.getEntryPoint()),
            func.getName(True),
        ))

    writer.close()

manifest.println("")
manifest.close()
decomp.dispose()
print("owner-family batch done for", scope)
