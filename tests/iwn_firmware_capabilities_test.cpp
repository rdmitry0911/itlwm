#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <zlib.h>
#define __packed __attribute__((packed))
#define XYLog(...) ((void)0)
#define letoh16(x) le16toh(x)
#define letoh32(x) le32toh(x)
#define letoh64(x) le64toh(x)
#include "registers.inc"
#define IWN_FLAG_ENH_SENS (1 << 7)
struct iwn_softc {
    iwn_fw_info fw{};
    struct { const char *dv_xname = "firmware-test"; } sc_dev;
    const char *fwname = "firmware-test";
    uint32_t fw_text_maxsz = IWN5000_FW_TEXT_MAXSZ;
    uint32_t fw_data_maxsz = IWN5000_FW_DATA_MAXSZ;
    uint32_t tlv_feature_flags = 0;
    uint32_t sc_flags = 0;
    uint32_t reset_noise_gain = 0, noise_gain = 0;
};
static std::vector<uint8_t> compressed;
static bool missing = false, failAllocation = false, failInflate = false;
static int dataObjects = 0;
class OSData {
public:
    OSData() { ++dataObjects; }
    ~OSData() { --dataObjects; }
    unsigned getLength() const { return compressed.size(); }
    const void *getBytesNoCopy() const { return compressed.data(); }
};
static OSData *getFWDescByName(const char *) { return missing ? nullptr : new OSData; }
#define OSSafeReleaseNULL(p) do { delete (p); (p) = nullptr; } while (0)
static void *test_allocate(size_t bytes) { return failAllocation ? nullptr : std::malloc(bytes); }
static bool uncompressFirmware(uint8_t *out, unsigned *len, uint8_t *in, unsigned size) {
    if (failInflate) return false;
    uLongf expanded = *len;
    const int result = uncompress(out, &expanded, in, size);
    if (result != Z_OK) return false;
    *len = expanded;
    return true;
}
class ItlIwn {
public:
    int iwn_read_firmware_leg(iwn_softc *, iwn_fw_info *);
    int iwn_read_firmware_tlv(iwn_softc *, iwn_fw_info *, uint16_t);
    int iwn_read_firmware(iwn_softc *);
    int iwn_prepare_firmware_capabilities(iwn_softc *);
    void iwn_release_firmware(iwn_softc *);
};
#define malloc(size, type, flags) test_allocate(size)
#include "production.inc"
#undef malloc
static void image(const std::vector<uint8_t> &raw) {
    uLongf length = compressBound(raw.size());
    compressed.resize(length);
    assert(compress(compressed.data(), &length, raw.data(), raw.size()) == Z_OK);
    compressed.resize(length);
}
static void empty(const iwn_softc &sc) {
    const iwn_fw_info zero{};
    assert(std::memcmp(&sc.fw, &zero, sizeof(zero)) == 0);
    assert(dataObjects == 0);
}
static void failure(ItlIwn &driver, iwn_softc &sc, int expected) {
    sc.tlv_feature_flags = 1;
    sc.sc_flags |= IWN_FLAG_ENH_SENS;
    assert(driver.iwn_prepare_firmware_capabilities(&sc) == expected);
    empty(sc);
    assert(sc.tlv_feature_flags == 0);
    assert((sc.sc_flags & IWN_FLAG_ENH_SENS) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    ItlIwn driver;
    iwn_softc sc;
    std::vector<uint8_t> panImage;
    // Every shipped IWN firmware must survive the real section parser and
    // resource decompression, both at preview and at the later radio upload.
    for (const char *name : {"4965", "5000", "5150", "1000", "6000", "6050",
                            "6030", "6005", "2030", "2000", "135", "105"}) {
        const std::string path = std::string(argv[1]) + "/iwn-" + name;
        std::ifstream input(path, std::ios::binary);
        assert(input.good());
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(input)), {});
        image(raw);
        sc.fw_text_maxsz = std::string(name) == "4965" ? IWN4965_FW_TEXT_MAXSZ : IWN5000_FW_TEXT_MAXSZ;
        sc.fw_data_maxsz = std::string(name) == "4965" ? IWN4965_FW_DATA_MAXSZ : IWN5000_FW_DATA_MAXSZ;
        assert(driver.iwn_prepare_firmware_capabilities(&sc) == 0);
        empty(sc);
        const uint32_t flags = sc.tlv_feature_flags;
        assert(driver.iwn_read_firmware(&sc) == 0);
        assert(sc.fw.data && sc.fw.main.text && sc.fw.init.data);
        assert(sc.tlv_feature_flags == flags);
        auto *owned = sc.fw.data;
        assert(driver.iwn_read_firmware(&sc) == EBUSY);
        assert(sc.fw.data == owned);
        driver.iwn_release_firmware(&sc);
        empty(sc);
        if (std::string(name) == "6030") {
            assert((flags & 1) != 0);
            panImage = raw;
        }
        std::printf("PASS: iwn-%s preview/upload flags=0x%x\n", name, flags);
    }
    assert(!panImage.empty());
    image(panImage);
    missing = true; failure(driver, sc, EINVAL); missing = false;
    failAllocation = true; failure(driver, sc, ENOMEM); failAllocation = false;
    failInflate = true; failure(driver, sc, EINVAL); failInflate = false;
    compressed.clear(); failure(driver, sc, EINVAL);
    compressed = {1, 2, 3, 4}; failure(driver, sc, EINVAL);
    for (size_t length : {0U, 1U, 3U, 4U, 24U, 87U, 88U, 89U}) {
        image(std::vector<uint8_t>(panImage.begin(), panImage.begin() + length));
        failure(driver, sc, EINVAL);
    }
    auto truncated = panImage;
    truncated.pop_back(); image(truncated); failure(driver, sc, EINVAL);
    auto badSignature = panImage;
    badSignature[4] ^= 1; image(badSignature); failure(driver, sc, EINVAL);
    auto badLength = panImage;
    std::memset(badLength.data() + sizeof(iwn_fw_tlv_hdr) + 4, 0xff, 4);
    image(badLength); failure(driver, sc, EINVAL);
    image(panImage);
    assert(driver.iwn_prepare_firmware_capabilities(&sc) == 0);
    empty(sc);
    assert(sc.tlv_feature_flags & 1);
    std::puts("PASS: firmware failure rollback, retry and buffer ownership");
}
