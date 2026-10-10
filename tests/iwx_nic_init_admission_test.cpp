// Full production APM/NIC/start_fw bodies. MMIO, PCI, RX setup and firmware
// delivery are explicit boundaries, not firmware-ready or radio receipts.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "registers.inc"
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "iwx-nic-init-test"
struct iwx_softc {
    int family=IWX_DEVICE_FAMILY_AX210;
    bool clockReady=true;
    int prepareError=0, rxError=0, firmwareError=0;
    unsigned prepares=0, polls=0, config=0, rx=0, firmware=0, shadow=0;
    unsigned handshake=0, unsupported=0;
};
static void internalAccess(iwx_softc *sc) { if (!sc->clockReady) ++sc->unsupported; }
static void csrSet(iwx_softc *sc, uint32_t reg, uint32_t) {
    if (reg==IWX_CSR_MAC_SHADOW_REG_CTRL) ++sc->shadow;
}
static void csrWrite(iwx_softc *sc, uint32_t reg, uint32_t) {
    if (reg==IWX_CSR_UCODE_DRV_GP1_CLR) ++sc->handshake;
    else assert(reg==IWX_CSR_INT);
}
#define IWX_SETBITS(sc, reg, bits) csrSet(sc, reg, bits)
#define IWX_WRITE(sc, reg, bits) csrWrite(sc, reg, bits)
class ItlIwx {
public:
    int iwx_apm_init(iwx_softc *);
    int iwx_nic_init(iwx_softc *);
    int iwx_start_fw(iwx_softc *);
    void iwx_apm_config(iwx_softc *) {}
    int iwx_poll_bit(iwx_softc *sc, uint32_t reg, uint32_t bits,
        uint32_t mask, unsigned timeout) {
        assert(reg==IWX_CSR_GP_CNTRL);
        assert(bits==IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY && bits==mask);
        assert(timeout==25000); ++sc->polls; return sc->clockReady;
    }
    int iwx_prepare_card_hw(iwx_softc *sc) { ++sc->prepares; return sc->prepareError; }
    void iwx_enable_rfkill_int(iwx_softc *) {}
    void iwx_disable_interrupts(iwx_softc *) {}
    void iwx_nic_config(iwx_softc *sc) { ++sc->config; internalAccess(sc); }
    int iwx_nic_rx_init(iwx_softc *sc) {
        ++sc->rx; internalAccess(sc); return sc->rxError;
    }
    int iwx_load_firmware(iwx_softc *sc) { ++sc->firmware; return sc->firmwareError; }
};
#include "init.inc"
static unsigned cases=0;
static int invoke(ItlIwx &driver, iwx_softc &sc, bool firmware) {
    return firmware ? driver.iwx_start_fw(&sc) : driver.iwx_nic_init(&sc);
}
static void clockTimeout(int family, bool firmware, int rxError=0, int firmwareError=0) {
    ItlIwx driver; iwx_softc sc;
    sc.family=family; sc.clockReady=false; sc.rxError=rxError;
    sc.firmwareError=firmwareError;
    const int result=invoke(driver, sc, firmware);
    std::printf("IWX clock timeout family=%d start_fw=%d result=%d "
        "nic_config=%u rx=%u firmware=%u unsupported=%u\n",
        family, firmware, result, sc.config, sc.rx, sc.firmware, sc.unsupported);
    std::fflush(stdout);
    assert(result==ETIMEDOUT);
    assert(sc.polls==1 && sc.config==0 && sc.rx==0 && sc.firmware==0 && sc.shadow==0);
    assert(sc.unsupported==0 && sc.handshake==unsigned(firmware ? 2 : 0));
    ++cases;
}
static void ready(int family, bool firmware, int rxError=0, int firmwareError=0) {
    ItlIwx driver; iwx_softc sc;
    sc.family=family; sc.rxError=rxError; sc.firmwareError=firmwareError;
    assert(invoke(driver, sc, firmware)==(rxError ? rxError : firmware ? firmwareError : 0));
    assert(sc.polls==1 && sc.config==1 && sc.rx==1 && sc.unsupported==0);
    assert(sc.shadow==unsigned(rxError==0));
    assert(sc.firmware==unsigned(firmware && rxError==0));
    ++cases;
}
static void retry(int family, bool firmware) {
    ItlIwx driver; iwx_softc sc;
    sc.family=family; sc.clockReady=false;
    assert(invoke(driver, sc, firmware)==ETIMEDOUT);
    assert(sc.config==0 && sc.rx==0 && sc.firmware==0);
    sc.clockReady=true;
    assert(invoke(driver, sc, firmware)==0);
    assert(sc.polls==2 && sc.config==1 && sc.rx==1 && sc.shadow==1);
    assert(sc.firmware==unsigned(firmware) && sc.unsupported==0);
    ++cases;
}
static void prepareFailure(int family, bool clockReady, int error) {
    ItlIwx driver; iwx_softc sc;
    sc.family=family; sc.clockReady=clockReady; sc.prepareError=error;
    assert(driver.iwx_start_fw(&sc)==error);
    assert(sc.prepares==1 && sc.polls==0 && sc.config==0 && sc.rx==0 && sc.firmware==0);
    assert(sc.shadow==0 && sc.handshake==0 && sc.unsupported==0);
    ++cases;
}
int main(int argc, char **argv) {
    if (argc>1 && !std::strcmp(argv[1], "clock-timeout")) {
        assert(argc==3);
        const int family=!std::strcmp(argv[2], "22000") ? IWX_DEVICE_FAMILY_22000 :
            IWX_DEVICE_FAMILY_AX210;
        clockTimeout(family, true); return 0;
    }
    if (argc>1 && !std::strcmp(argv[1], "prepare-failure")) {
        prepareFailure(IWX_DEVICE_FAMILY_AX210, false, EIO); return 0;
    }
    for (int family : {IWX_DEVICE_FAMILY_22000, IWX_DEVICE_FAMILY_AX210}) {
        for (bool firmware : {false, true}) {
            clockTimeout(family, firmware);
            clockTimeout(family, firmware, EBUSY);
            clockTimeout(family, firmware, 0, EIO);
            ready(family, firmware);
            ready(family, firmware, EBUSY);
            ready(family, firmware, EIO);
            ready(family, firmware, 0, EIO);
            retry(family, firmware);
        }
        for (bool clockReady : {false, true})
            for (int error : {EIO, ETIMEDOUT}) prepareFailure(family, clockReady, error);
    }
    std::printf("PASS: %u complete IWX APM/NIC/firmware-start admission cases\n", cases);
}
