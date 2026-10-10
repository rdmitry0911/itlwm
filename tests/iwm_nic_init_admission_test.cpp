// Execute complete production APM, NIC-init and firmware-start functions.
// MMIO outcomes, PCI, downstream ring setup and firmware delivery are doubles.
// No successful authentication, DMA-idle or firmware-ready receipt is supplied.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registers.inc"
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "iwm-nic-init-test"
#define DELAY(usec) ((void)(usec))

enum iwm_ucode_type { IWM_UCODE_TYPE_INIT, IWM_UCODE_TYPE_REGULAR };
struct iwm_softc {
    int sc_device_family=IWM_DEVICE_FAMILY_9000;
    bool host_interrupt_operation_mode=false;
    bool clockReady=true;
    int rxError=0, txError=0, firmwareError=0;
    unsigned config=0, rx=0, tx=0, firmware=0, shadow=0, handshake=0;
    unsigned unsupported=0, polls=0;
    std::vector<std::string> events;
};
static void internalAccess(iwm_softc *sc) {
    if (!sc->clockReady) ++sc->unsupported;
}
static void csrSet(iwm_softc *sc, uint32_t reg, uint32_t) {
    if (reg==IWM_CSR_MAC_SHADOW_REG_CTRL) {
        ++sc->shadow; internalAccess(sc); sc->events.emplace_back("shadow");
    }
}
static void csrWrite(iwm_softc *sc, uint32_t reg, uint32_t) {
    if (reg==IWM_CSR_UCODE_DRV_GP1_CLR) {
        ++sc->handshake; sc->events.emplace_back("handshake");
    } else {
        assert(reg==IWM_CSR_INT); sc->events.emplace_back("interrupt-clear");
    }
}
#define IWM_SETBITS(sc, reg, bits) csrSet(sc, reg, bits)
#define IWM_WRITE(sc, reg, bits) csrWrite(sc, reg, bits)
class ItlIwm {
public:
    int iwm_apm_init(iwm_softc *);
    int iwm_nic_init(iwm_softc *);
    int iwm_start_fw(iwm_softc *, iwm_ucode_type);
    void iwm_apm_config(iwm_softc *sc) { sc->events.emplace_back("apm-config"); }
    int iwm_poll_bit(iwm_softc *sc, uint32_t reg, uint32_t bits,
        uint32_t mask, unsigned timeout) {
        assert(reg==IWM_CSR_GP_CNTRL);
        assert(bits==IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY && bits==mask);
        assert(timeout==25000); ++sc->polls; sc->events.emplace_back("clock-poll");
        return sc->clockReady;
    }
    bool iwm_nic_lock(iwm_softc *sc) { internalAccess(sc); return sc->clockReady; }
    void iwm_nic_unlock(iwm_softc *) {}
    uint32_t iwm_read_prph(iwm_softc *sc, uint32_t) { internalAccess(sc); return 0; }
    void iwm_write_prph(iwm_softc *sc, uint32_t, uint32_t) { internalAccess(sc); }
    void iwm_set_bits_prph(iwm_softc *sc, uint32_t, uint32_t) { internalAccess(sc); }
    void iwm_set_bits_mask_prph(iwm_softc *sc, uint32_t, uint32_t, uint32_t) {
        internalAccess(sc);
    }
    void iwm_nic_config(iwm_softc *sc) {
        ++sc->config; internalAccess(sc); sc->events.emplace_back("nic-config");
    }
    int iwm_nic_rx_init(iwm_softc *sc) {
        ++sc->rx; internalAccess(sc); sc->events.emplace_back("rx-init");
        return sc->rxError;
    }
    int iwm_nic_tx_init(iwm_softc *sc) {
        ++sc->tx; internalAccess(sc); sc->events.emplace_back("tx-init");
        return sc->txError;
    }
    void iwm_enable_fwload_interrupt(iwm_softc *sc) {
        sc->events.emplace_back("firmware-interrupt");
    }
    int iwm_load_firmware(iwm_softc *sc, iwm_ucode_type) {
        ++sc->firmware; sc->events.emplace_back("firmware-load");
        return sc->firmwareError;
    }
};
#include "init.inc"

static unsigned cases=0;
static int familyValue(const char *name) {
    if (!std::strcmp(name, "7000")) return IWM_DEVICE_FAMILY_7000;
    if (!std::strcmp(name, "8000")) return IWM_DEVICE_FAMILY_8000;
    assert(!std::strcmp(name, "9000")); return IWM_DEVICE_FAMILY_9000;
}
static int invoke(ItlIwm &driver, iwm_softc &sc, bool firmware) {
    return firmware ? driver.iwm_start_fw(&sc, IWM_UCODE_TYPE_REGULAR) :
        driver.iwm_nic_init(&sc);
}
static void clockTimeout(int family, bool oscillator, bool firmware,
    int rxError=0, int txError=0, int firmwareError=0) {
    ItlIwm driver;
    iwm_softc sc;
    sc.sc_device_family=family;
    sc.host_interrupt_operation_mode=oscillator;
    sc.clockReady=false; sc.rxError=rxError; sc.txError=txError;
    sc.firmwareError=firmwareError;
    const int result=invoke(driver, sc, firmware);
    std::printf("clock timeout family=%d oscillator=%d start_fw=%d result=%d "
        "nic_config=%u rx=%u tx=%u firmware=%u unsupported=%u\n",
        family, oscillator, firmware, result, sc.config, sc.rx, sc.tx,
        sc.firmware, sc.unsupported);
    std::fflush(stdout);
    assert(result==ETIMEDOUT);
    assert(sc.polls==1 && sc.config==0 && sc.rx==0 && sc.tx==0 && sc.firmware==0);
    assert(sc.shadow==0 && sc.handshake==0 && sc.unsupported==0);
    ++cases;
}
static void ready(int family, bool oscillator, bool firmware,
    int rxError=0, int txError=0, int firmwareError=0) {
    ItlIwm driver;
    iwm_softc sc;
    sc.sc_device_family=family; sc.host_interrupt_operation_mode=oscillator;
    sc.rxError=rxError; sc.txError=txError; sc.firmwareError=firmwareError;
    const int expected=rxError ? rxError : txError ? txError :
        firmware ? firmwareError : 0;
    assert(invoke(driver, sc, firmware)==expected);
    assert(sc.polls==1 && sc.config==1 && sc.rx==1 && sc.unsupported==0);
    assert(sc.tx==unsigned(rxError==0));
    const bool nicReady=rxError==0 && txError==0;
    assert(sc.shadow==unsigned(nicReady));
    assert(sc.firmware==unsigned(firmware && nicReady));
    assert(sc.handshake==unsigned(firmware && nicReady ? 4 : 0));
    ++cases;
}
static void retry(int family, bool oscillator, bool firmware) {
    ItlIwm driver;
    iwm_softc sc;
    sc.sc_device_family=family; sc.host_interrupt_operation_mode=oscillator;
    sc.clockReady=false;
    assert(invoke(driver, sc, firmware)==ETIMEDOUT);
    assert(sc.config==0 && sc.rx==0 && sc.tx==0 && sc.firmware==0);
    sc.clockReady=true;
    assert(invoke(driver, sc, firmware)==0);
    assert(sc.polls==2 && sc.config==1 && sc.rx==1 && sc.tx==1 && sc.shadow==1);
    assert(sc.firmware==unsigned(firmware) && sc.unsupported==0);
    ++cases;
}
int main(int argc, char **argv) {
    if (argc>1 && !std::strcmp(argv[1], "clock-timeout")) {
        assert(argc==3); clockTimeout(familyValue(argv[2]), false, true); return 0;
    }
    for (int family : {IWM_DEVICE_FAMILY_7000, IWM_DEVICE_FAMILY_8000,
                       IWM_DEVICE_FAMILY_9000})
        for (bool oscillator : {false, true})
            for (bool firmware : {false, true}) {
                clockTimeout(family, oscillator, firmware);
                clockTimeout(family, oscillator, firmware, EBUSY);
                clockTimeout(family, oscillator, firmware, 0, ENOMEM);
                clockTimeout(family, oscillator, firmware, 0, 0, EIO);
                ready(family, oscillator, firmware);
                ready(family, oscillator, firmware, EBUSY);
                ready(family, oscillator, firmware, 0, ENOMEM);
                ready(family, oscillator, firmware, 0, 0, EIO);
                retry(family, oscillator, firmware);
            }
    std::printf("PASS: %u complete IWM APM/NIC/firmware-start admission cases\n", cases);
}
